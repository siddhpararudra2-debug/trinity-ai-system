"""Live bridge tests (Item 1, Stage 4c). Skipped unless TRINITY_NATIVE_EXEC is set.

Every mapped op is single-shot with full parameters (no cross-call state
needed), so these mirror --selftest shapes with deterministic assertions.
"""

import os

import pytest
from fastapi.testclient import TestClient

from src.main import app
from tests.pnggen import png_bytes

needs_binary = pytest.mark.skipif(
    not os.getenv("TRINITY_NATIVE_EXEC", ""),
    reason="needs Trinity.exe via TRINITY_NATIVE_EXEC",
)


def _exec(client, engine, operation, parameters):
    resp = client.post(
        "/api/execute",
        json={"engine": engine, "operation": operation, "parameters": parameters},
    )
    assert resp.status_code == 200, resp.text
    return resp.json()


@needs_binary
def test_live_robotics_kinematics_matches_selftest():
    with TestClient(app) as client:
        body = _exec(
            client,
            "robotics",
            "kinematics",
            {"joint_angles": [0.0, 0.0]},
        )
    assert body["success"] is True
    pos = body["result"]["end_effector"]["position"]
    assert abs(pos["x"] - 2.0) < 1e-6 and abs(pos["y"]) < 1e-6


@needs_binary
def test_live_simulation_runs_linear_motion():
    with TestClient(app) as client:
        body = _exec(
            client,
            "simulation",
            "simulate",
            {
                "duration_s": 1.0,
                "initial_velocity_m_s": 2.0,
                "acceleration_m_s2": 1.0,
            },
        )
    assert body["success"] is True
    assert body["result"]["result"]["step_count"] > 0


@needs_binary
def test_live_pcb_generates_board_with_artifacts():
    # create_board is pure JSON (board spec in result); files come from the
    # separate pcb/export op. Assert the board content, not files.
    with TestClient(app) as client:
        body = _exec(
            client,
            "pcb",
            "generate",
            {
                "width_mm": 50.0,
                "height_mm": 40.0,
                "components": [{"part": "esp32"}],
            },
        )
    assert body["success"] is True
    assert body["validation"]["status"] == "VALIDATED"
    assert body["result"]["component_count"] == 1
    assert body["result"]["design"]["components"][0]["ref"] == "U1"


@needs_binary
def test_live_unmapped_op_stays_truthful():
    with TestClient(app) as client:
        body = _exec(client, "firmware", "test", {})
    assert body["success"] is False
    assert body["errors"][0]["code"] == "capability_unavailable"


@needs_binary
def test_live_research_search_empty_index():
    with TestClient(app) as client:
        body = _exec(client, "research", "search", {"query": "no-such-doc"})
    assert body["success"] is True
    assert body["result"]["hit_count"] == 0


@needs_binary
def test_live_firmware_create_then_build_needs_mcu():
    with TestClient(app) as client:
        created = _exec(client, "firmware", "create", {"name": "live-fw"})
    assert created["success"] is True
    project = created["result"]["project"]
    with TestClient(app) as client:
        built = _exec(client, "firmware", "build", {"project": project})
    assert built["success"] is False  # truthful: select_mcu first
    assert built["errors"], "must explain why, not just fail"


@needs_binary
def test_live_firmware_full_chain_generates_sources_and_validates():
    """The whole bridged chain, threading the project step to step.

    This is the path the firmware console runs, so it is asserted end to end
    rather than op by op: each call must return the project the next one
    needs, or the console cannot walk it.
    """
    with TestClient(app) as client:
        project = _exec(client, "firmware", "create", {"name": "chain-fw"})["result"][
            "project"
        ]
        assert project["has_mcu"] is False

        selected = _exec(
            client, "firmware", "select_mcu", {"project": project, "mcu": "ESP32"}
        )
        assert selected["success"] is True
        assert selected["result"]["mcu"] == "ESP32"
        project = selected["result"]["project"]
        assert project["has_mcu"] is True
        assert project["clock_hz"] == 240_000_000

        configured = _exec(
            client,
            "firmware",
            "configure_peripheral",
            {
                "project": project,
                "peripheral": "UART1",
                "kind": "uart",
                "tx": "GPIO17",
                "rx": "GPIO16",
                "baud": 115200,
            },
        )
        assert configured["success"] is True
        project = configured["result"]["project"]
        assert len(project["pin_mappings"]) == 2

        generated = _exec(client, "firmware", "generate", {"project": project})
        assert generated["success"] is True
        # Generated sources are real artifacts, not just a report.
        assert any(a["type"] == "firmware_source" for a in generated["artifacts"])
        project = generated["result"]["project"]
        assert {s["path"] for s in project["sources"]} >= {"main.cpp"}

        validated = _exec(client, "firmware", "validate", {"project": project})
        assert validated["success"] is True
        assert validated["result"]["passed"] is True


@needs_binary
def _pcb_board(client, components):
    body = _exec(
        client,
        "pcb",
        "generate",
        {"width_mm": 50.0, "height_mm": 40.0, "components": components},
    )
    assert body["success"] is True, body.get("errors")
    return body["result"]["design"]


def _auto_net(client, design):
    """One net per distinct power-pin name, mirroring the PCB console."""
    groups: dict[str, list[str]] = {}
    for component in design.get("components", []):
        for pin in component.get("pins", []):
            if pin.get("electrical") in ("power_in", "power_out"):
                groups.setdefault(pin["name"], []).append(
                    f"{component['ref']}.{pin['number']}"
                )
    current = design
    for name, pins in groups.items():
        body = _exec(client, "pcb", "add_net", {"design": current, "name": name, "pins": pins})
        assert body["success"] is True, body.get("errors")
        current = body["result"]["design"]
    return current, groups


@needs_binary
@pytest.mark.parametrize(
    ("label", "components"),
    [
        ("bare", []),
        ("esp32", [{"part": "esp32"}]),
        ("esp32_regulator", [{"part": "esp32"}, {"part": "regulator"}]),
    ],
)
def test_live_pcb_generate_net_validate_export(label, components):
    """The whole console chain, ending in a real KiCad file.

    This is the path that was previously impossible: create_board leaves
    power pins on no net, its own DRC rejects that, and export refuses a
    board that fails. Wiring the nets is what unblocks it.
    """
    with TestClient(app) as client:
        design = _pcb_board(client, components)
        netted, groups = _auto_net(client, design)

        # A board with no power pins must not gain phantom nets.
        if components:
            assert groups, "a component board should have power pins to wire"
            assert len(netted["nets"]) == len(groups)
        else:
            assert groups == {}
            assert netted["nets"] == []

        checked = _exec(client, "pcb", "validate", {"design": netted})
        assert checked["success"] is True
        assert checked["result"]["passed"] is True, checked["result"].get("rules")

        exported = _exec(
            client, "pcb", "export", {"design": netted, "project": f"trinity_{label}"}
        )
        assert exported["success"] is True, exported.get("errors")
        kicad = [a for a in exported["artifacts"] if a["type"] == "kicad_pcb"]
        assert len(kicad) == 1, exported["artifacts"]
        assert kicad[0]["size_bytes"] > 0


@needs_binary
def test_live_pcb_export_refuses_a_board_with_unwired_power_pins():
    """The refusal must survive, or the auto-netting is masking a real check."""
    with TestClient(app) as client:
        design = _pcb_board(client, [{"part": "esp32"}])
        exported = _exec(
            client, "pcb", "export", {"design": design, "project": "trinity_unwired"}
        )
    assert exported["success"] is False
    assert not [a for a in exported["artifacts"] if a["type"] == "kicad_pcb"]
    assert "validation" in exported["errors"][0]["message"].lower()


@needs_binary
def test_live_vision_resolves_an_uploaded_artifact_id():
    """Upload, then inspect with no path at all.

    The native engine refuses a bare artifact_id on purpose; the bridge is
    what resolves it. That resolution only exists on the bridge path, so it
    is only assertable with the binary attached.
    """
    png = png_bytes()
    with TestClient(app) as client:
        uploaded = client.post("/api/uploads", files={"file": ("live.png", png, "image/png")})
        assert uploaded.status_code == 200, uploaded.text
        artifact_id = uploaded.json()["artifact_id"]

        body = _exec(
            client, "vision", "image_inspect", {"artifact_id": artifact_id}
        )
    assert body["success"] is True, body.get("errors")
    assert body["result"]["input"]["metadata"]["width"] == 8
    assert body["result"]["input"]["metadata"]["format"] == "png"


@needs_binary
def test_live_vision_refuses_an_unknown_artifact_id():
    with TestClient(app) as client:
        body = _exec(
            client, "vision", "image_inspect", {"artifact_id": "does-not-exist"}
        )
    assert body["success"] is False
    assert body["errors"][0]["code"] == "request_validation_error"
    assert "does-not-exist" in body["errors"][0]["message"]


@needs_binary
def test_live_firmware_build_refuses_truthfully_without_a_toolchain():
    """Build may legitimately fail on any machine; it must never fake success."""
    with TestClient(app) as client:
        project = _exec(client, "firmware", "create", {"name": "build-fw"})["result"][
            "project"
        ]
        project = _exec(
            client, "firmware", "select_mcu", {"project": project, "mcu": "ESP32"}
        )["result"]["project"]
        project = _exec(client, "firmware", "generate", {"project": project})["result"][
            "project"
        ]
        built = _exec(client, "firmware", "build", {"project": project})

    if built["success"]:
        assert built["result"]["executed"] is True
    else:
        assert built["result"]["executed"] is False
        assert built["result"]["toolchain"], "must name the toolchain it looked for"
        assert built["errors"], "must explain the refusal"


@needs_binary
def test_live_pcb_generate_then_validate():
    with TestClient(app) as client:
        board = _exec(
            client,
            "pcb",
            "generate",
            {"width_mm": 50.0, "height_mm": 40.0, "components": []},
        )
    assert board["success"] is True
    with TestClient(app) as client:
        checked = _exec(
            client, "pcb", "validate", {"design": board["result"]["design"]}
        )
    assert checked["success"] is True
    assert checked["result"]["passed"] is True


@needs_binary
def test_live_vision_missing_file_refuses():
    with TestClient(app) as client:
        body = _exec(
            client, "vision", "image_inspect", {"path": "no-such-image.png"}
        )
    # Without OpenCV: capability refusal. With OpenCV: missing file.
    # Either way success:false with an explanatory error.
    assert body["success"] is False
    assert body["errors"], "must explain why, not just fail"

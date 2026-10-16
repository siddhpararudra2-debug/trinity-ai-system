"""STEP export through a real CAD kernel.

The point of these tests is that the STEP file is a genuine solid, not a
renamed STL. What is actually proven here, and what is not:

PROVEN — the file is valid ISO-10303-21 AP214 carrying ADVANCED_FACE
geometry rather than a mesh dump; every triangle survives as a face; each
connected component becomes its own solid and none are dropped; the
written file round-trips through the kernel with identical volume; and
with no kernel installed every path says so instead of writing a fake.

NOT claimed — that the exported solid is the part's true material volume.
Parts are assembled from interpenetrating boxes, and STEP export keeps
those surfaces rather than booleaning them, so the sewn volume differs
from the mesh's own divergence sum for the enclosure, bracket and plate.
The two tests that do assert exact volume parity restrict themselves to the
parts with disjoint shells, where the two numbers are the same thing.

Kernel tests skip when OpenCascade is absent; the refusal tests run either
way, so a machine without the kernel still proves the honest path.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.engines.cad.adapters.cadquery import CadQueryAdapter
from src.engines.cad.engine import CADEngine, format_reason, unsupported_formats
from src.engines.cad.parts import get_part, list_supported_types
from src.engines.cad.primitives import Mesh, box
from src.engines.cad.step import (
    connected_shells,
    kernel_available,
    kernel_info,
    signed_volume_mm3,
    write_step,
)

needs_kernel = pytest.mark.skipif(
    not kernel_available(), reason="needs OpenCascade (pip install cadquery)"
)

PART_TYPES = list_supported_types()


def _mesh(part_type: str = "quadcopter_frame") -> Mesh:
    part = get_part(part_type)
    return part.builder(part.ir_class.from_request({}))


def _read_back(path: Path):
    """Load a STEP file with the same kernel that wrote it."""
    from OCP.BRepGProp import BRepGProp
    from OCP.GProp import GProp_GProps
    from OCP.Interface import Interface_Static
    from OCP.STEPControl import STEPControl_Reader

    Interface_Static.SetCVal_s("xstep.cascade.unit", "MM")
    reader = STEPControl_Reader()
    status = reader.ReadFile(str(path))
    assert str(status).rsplit(".", 1)[-1] == "IFSelect_RetDone", status
    assert reader.TransferRoots() > 0
    shape = reader.OneShape()
    props = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, props)
    return shape, abs(props.Mass())


# ----------------------------------------------------------- availability ---


def test_kernel_info_always_states_schema_and_representation():
    info = kernel_info()
    assert info["schema"] == "AP214IS"
    assert info["unit"] == "MM"
    assert info["representation"] == "faceted_brep"
    if info["available"]:
        assert info["kernel"].startswith("opencascade")
        assert "reason" not in info
    else:
        assert "CAD_KERNEL_UNAVAILABLE" in info["reason"]


def test_unsupported_formats_tracks_the_kernel_not_a_constant():
    assert unsupported_formats() == (set() if kernel_available() else {"step"})
    if not kernel_available():
        assert "CAD_KERNEL_UNAVAILABLE" in format_reason("step")
    else:
        assert format_reason("step") == ""


def test_adapter_refuses_when_no_kernel(monkeypatch):
    monkeypatch.setattr("src.engines.cad.adapters.cadquery.kernel_info", lambda: {"available": False})
    monkeypatch.setattr(
        "src.engines.cad.adapters.cadquery.kernel_info",
        lambda: {"available": False, "reason": "CAD_KERNEL_UNAVAILABLE: probe"},
    )
    adapter = CadQueryAdapter()
    assert adapter.available is False
    with pytest.raises(CapabilityUnavailableError, match="CAD_KERNEL_UNAVAILABLE"):
        adapter.export_step(_mesh(), "ignored.step")


def test_adapter_writes_when_the_kernel_is_there(tmp_path, monkeypatch):
    if not kernel_available():
        pytest.skip("needs OpenCascade")
    monkeypatch.setattr("src.engines.cad.adapters.cadquery.kernel_info", lambda: {"available": True})
    out = tmp_path / "via_adapter.step"
    stats = CadQueryAdapter().export_step(_mesh(), str(out))
    assert out.stat().st_size > 0
    assert stats["solid"] is True


# ------------------------------------------------------ mesh bookkeeping ---


def test_signed_volume_is_orientation_aware():
    mesh = _mesh()
    volume = signed_volume_mm3(mesh.triangles)
    assert volume != 0
    # Reversing every triangle negates the volume; magnitude is orientation-free.
    flipped = Mesh(triangles=[(t[0], t[2], t[1]) for t in mesh.triangles])
    assert signed_volume_mm3(flipped.triangles) == pytest.approx(-volume, rel=1e-12)


def test_connected_shells_split_disjoint_bodies():
    a = box((0.0, 0.0, 0.0), (10.0, 10.0, 10.0))
    b = box((100.0, 0.0, 0.0), (10.0, 10.0, 10.0))
    joined = Mesh(triangles=a.triangles + b.triangles)
    assert len(connected_shells(joined.triangles)) == 2
    assert len(connected_shells(a.triangles)) == 1


def test_write_step_refuses_an_empty_mesh(tmp_path):
    if not kernel_available():
        pytest.skip("needs OpenCascade")
    with pytest.raises(EngineExecutionError, match="empty mesh"):
        write_step(Mesh(triangles=[]), tmp_path / "empty.step")


# ------------------------------------------------------------ the exports ---


@needs_kernel
@pytest.mark.parametrize("part_type", PART_TYPES)
def test_every_part_writes_a_real_solid(tmp_path, part_type):
    mesh = _mesh(part_type)
    out = tmp_path / f"{part_type}.step"
    stats = write_step(mesh, out)

    assert out.stat().st_size > 0
    assert stats["solid"] is True, "mesh must sew into solids"
    assert stats["degenerate_faces"] == 0
    assert stats["face_count"] == len(mesh.triangles), "every triangle becomes a face"
    assert stats["schema"] == "AP214IS"
    assert stats["volume_mm3"] > 0, "an inverted solid would read as negative"
    # Nothing gets dropped: one sewn solid per connected body in the mesh.
    assert stats["shell_count"] == len(connected_shells(mesh.triangles))


@needs_kernel
@pytest.mark.parametrize("part_type", ["standoff", "quadcopter_frame"])
def test_disjoint_shell_parts_match_their_own_mesh_volume(tmp_path, part_type):
    """Where shells do not interpenetrate, the sewn volume is exact.

    These two parts are built from bodies that only touch, so the solid and
    the triangle soup describe the same volume and must agree. Parts built
    from interpenetrating boxes deliberately are not in this list — their
    sewn volume is a self-check, not a material volume.
    """
    mesh = _mesh(part_type)
    stats = write_step(mesh, tmp_path / f"{part_type}.step")
    assert stats["volume_mm3"] == pytest.approx(abs(signed_volume_mm3(mesh.triangles)), rel=1e-9)


@needs_kernel
def test_step_file_is_iso_10303_ap214(tmp_path):
    out = tmp_path / "frame.step"
    write_step(_mesh(), out)
    text = out.read_text(errors="replace")
    assert text.startswith("ISO-10303-21;")
    assert text.rstrip().endswith("END-ISO-10303-21;")
    assert "AUTOMOTIVE_DESIGN" in text, "expected the AP214 schema"
    # A faceted solid, not a mesh dump: no tessellation entities.
    assert "TRIANGULATED_FACE" not in text
    assert "ADVANCED_FACE" in text


@needs_kernel
def test_step_round_trips_through_the_kernel(tmp_path):
    out = tmp_path / "frame.step"
    mesh = _mesh("enclosure")
    stats = write_step(mesh, out)

    shape, volume = _read_back(out)
    assert not shape.IsNull()
    assert volume == pytest.approx(stats["volume_mm3"], rel=1e-6)
    assert volume > 0


@needs_kernel
@pytest.mark.parametrize("part_type", PART_TYPES)
def test_generate_routes_step_through_the_api(tmp_path, part_type):
    engine = CADEngine()
    result = engine.generate(
        {
            "type": part_type,
            "parameters": {},
            "outputs": ["stl", "step"],
        }
    )
    types = {artifact_type for _, artifact_type in result.pending_artifacts}
    assert types == {"stl", "step"}
    assert "unavailable_formats" not in result.result
    assert result.result["step"]["solid"] is True
    step_path = next(p for p, t in result.pending_artifacts if t == "step")
    assert Path(step_path).read_text(errors="replace").startswith("ISO-10303-21;")
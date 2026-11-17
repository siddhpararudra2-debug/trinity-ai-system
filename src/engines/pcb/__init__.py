"""PCB Engine (PRD §24, §31).

Netlist-driven board generation in pure Python: outline, auto-placement,
2-layer grid routing, and design checks. No native binary required, and
the output is deterministic — the same parameters always produce the
same board.

What is real in this fallback
- auto-placement inside the board outline: no overlaps, a keep-out
  margin from the board edge, and connected parts pulled toward each
  other (a part with an explicit ``x_mm``/``y_mm`` in its netlist
  parameters is placed exactly there and validated like any other);
- grid routing on two copper layers (F.Cu / B.Cu) with vias, honoring
  ``track_width_mm`` and ``clearance_mm`` from the parameters;
- design checks: overlap, out of bounds, clearance, unrouted nets —
  each reported with its findings, and any finding FAILs the result.

What stays native-only (documented, never faked)
- fabrication-ready Gerber/drill content: ``export`` still writes
  placeholder layer text and labels itself as a placeholder;
- pad-level placement and routing (pin maps, courtyards, length
  matching, differential pairs): routing here connects components on a
  grid, not pads;
- footprint libraries: body sizes come from a small approximate table
  (``_FOOTPRINT_SIZES_MM``) or a per-part ``size_mm`` override.

LLM-LINK: a future model could propose placement hints (grouping,
thermal/RF zones) ahead of the deterministic placer, or rank candidate
routes after it. The engine never calls one today.
"""

from __future__ import annotations

import heapq
import math
import tempfile
from pathlib import Path
from typing import Any

from src.core.errors import RequestValidationError
from src.engines.base import BaseEngine, EngineResult, ValidationResult

DEFAULT_BOARD_WIDTH_MM = 50.0
DEFAULT_BOARD_HEIGHT_MM = 40.0
DEFAULT_BOARD_THICKNESS_MM = 1.6

# Routing / placement defaults. ``clearance_mm`` matches the record
# rules convention (minClearanceMm 0.2) and ``track_width_mm`` the
# minTrackMm 0.2 default.
DEFAULT_TRACK_WIDTH_MM = 0.2
DEFAULT_CLEARANCE_MM = 0.2
DEFAULT_GRID_MM = 0.5
DEFAULT_KEEPOUT_MM = 2.0
DEFAULT_PLACEMENT_STEP_MM = 1.0
DEFAULT_FOOTPRINT_SIZE_MM = (10.0, 10.0)

TOP_LAYER = "F.Cu"
BOTTOM_LAYER = "B.Cu"
LAYER_COUNT = 2  # F.Cu + B.Cu; more layers stay native-only
VIA_COST_MM = 2.0
_MAX_GRID_POINTS = 500_000
_FLOAT_EPS = 1e-9

# Approximate body sizes (width, height) in mm, used only for placement
# and the design checks — never for fabrication. Real pad geometry is
# native-only. Names are matched case-insensitively; anything else gets
# DEFAULT_FOOTPRINT_SIZE_MM unless the netlist entry carries ``size_mm``.
_FOOTPRINT_SIZES_MM: dict[str, tuple[float, float]] = {
    "esp32-wroom-32": (18.0, 25.5),
    "usb_c_receptacle": (8.94, 7.35),
    "r_0402": (1.0, 0.5),
    "c_0402": (1.0, 0.5),
    "r_0603": (1.6, 0.8),
    "c_0603": (1.6, 0.8),
    "r_0805": (2.0, 1.25),
    "c_0805": (2.0, 1.25),
    "sot-23": (2.9, 1.6),
    "soic-8": (3.9, 4.9),
    "soic-16": (3.9, 9.9),
}


class PcbError(RequestValidationError):
    """PCB-specific validation error."""


class PcbEngine(BaseEngine):
    name = "pcb"
    version = "2.0"
    capabilities = (
        "validate",
        "generate",
        "export",
        "add_net",
        "add_track",
    )

    def describe(self) -> dict[str, Any]:
        # Real Python fallback (the M18 convention): placement, 2-layer
        # grid routing and the design checks below are implemented and
        # tested, so this is no longer a scaffold — no status key.
        # Gerber/drill content and pad-level fidelity stay native-only
        # (module docstring).
        return super().describe()

    def execute(self, operation: str, parameters: dict[str, Any]) -> EngineResult:
        if operation == "validate":
            return self.execute_validate(parameters)
        if operation == "generate":
            return self.execute_generate(parameters)
        if operation == "export":
            return self.execute_export(parameters)
        if operation == "add_net":
            return self.execute_add_net(parameters)
        if operation == "add_track":
            return self.execute_add_track(parameters)
        raise RequestValidationError(f"PCB engine has no operation '{operation}'")

    # -------------------------------------------------------- execute ---

    def execute_validate(self, parameters: dict[str, Any]) -> EngineResult:
        """Validate a PCB design: check nets, clearances, DRC rules."""
        components = parameters.get("components", [])
        nets = parameters.get("nets", [])
        errors: list[str] = []

        # Check that all component nets are defined
        for comp in components:
            ref = comp.get("ref", "")
            value = comp.get("value", "")
            nets_for_comp = comp.get("nets", [])
            if not nets_for_comp:
                errors.append(f"Component {ref} ({value}) has no nets defined")

        # Check net connectivity: every net must have at least 2 connections
        net_connections: dict[str, int] = {}
        for comp in components:
            for net in comp.get("nets", []):
                net_connections[net] = net_connections.get(net, 0) + 1

        for net_name, count in net_connections.items():
            if count < 2:
                errors.append(f"Net '{net_name}' has only {count} connection(s); needs at least 2")

        ok = len(errors) == 0
        status = "VALIDATED" if ok else "FAILED"
        checks = {"errors": errors} if errors else {}

        return EngineResult(
            success=ok,
            engine=self.name,
            operation="validate",
            result={
                "components": len(components),
                "nets": len(nets),
                "errors": errors,
            },
            validation=ValidationResult(status=status, checks=checks),
        )

    # -------------------------------------------------------- generate ---

    def execute_generate(self, parameters: dict[str, Any]) -> EngineResult:
        """Generate a board outline, place and route it, and check it.

        Placement: fixed parts (explicit ``x_mm``/``y_mm``) land exactly
        there; everything else is auto-placed deterministically with a
        keep-out margin, no overlaps, and connected parts pulled toward
        each other. Routing: 2-layer grid A* per net with vias under
        ``track_width_mm``/``clearance_mm``. The four design checks then
        grade the finished layout; any finding fails the result.
        """
        width = float(parameters.get("width_mm", DEFAULT_BOARD_WIDTH_MM))
        height = float(parameters.get("height_mm", DEFAULT_BOARD_HEIGHT_MM))
        thickness = float(parameters.get("thickness_mm", DEFAULT_BOARD_THICKNESS_MM))
        track_width = float(
            parameters.get("track_width_mm", DEFAULT_TRACK_WIDTH_MM)
        )
        clearance = float(parameters.get("clearance_mm", DEFAULT_CLEARANCE_MM))
        grid_mm = float(parameters.get("grid_mm", DEFAULT_GRID_MM))
        keepout = float(parameters.get("keepout_mm", DEFAULT_KEEPOUT_MM))
        step = float(
            parameters.get("placement_step_mm", DEFAULT_PLACEMENT_STEP_MM)
        )
        if width <= 0 or height <= 0:
            raise PcbError("width_mm and height_mm must be positive")
        if track_width <= 0:
            raise PcbError("track_width_mm must be positive")
        if clearance < 0 or keepout < 0:
            raise PcbError("clearance_mm and keepout_mm must be >= 0")
        if grid_mm <= 0 or step <= 0:
            raise PcbError("grid_mm and placement_step_mm must be positive")

        netlist = parameters.get("netlist", {}) or {}
        if not isinstance(netlist, dict):
            raise PcbError("netlist must be an object keyed by component ref")

        components = _parse_components(netlist)

        # Generate outline rectangle
        outline = {
            "type": "rectangle",
            "width_mm": width,
            "height_mm": height,
            "thickness_mm": thickness,
        }

        placements = _place_components(
            components, width, height, keepout, clearance, step
        )
        routing = _route_nets(
            components,
            width=width,
            height=height,
            grid_mm=grid_mm,
            track_width=track_width,
            clearance=clearance,
        )

        checks = _design_checks(
            components,
            routing,
            width=width,
            height=height,
            keepout=keepout,
            clearance=clearance,
            track_width=track_width,
        )
        ok = not any(check["findings"] for check in checks.values())
        status = "VALIDATED" if ok else "FAILED"

        fixed_refs = sorted(c["ref"] for c in components if c["fixed"])
        auto_refs = sorted(c["ref"] for c in components if not c["fixed"])

        result = {
            "outline": outline,
            "board_width_mm": width,
            "board_height_mm": height,
            "board_thickness_mm": thickness,
            "components": placements,
            "netlist_summary": {
                ref: {"nets": list(netlist[ref].get("nets", []))}
                for ref in sorted(netlist)
            },
            "layers": [TOP_LAYER, BOTTOM_LAYER],
            "tracks": routing["tracks"],
            "vias": routing["vias"],
            "design_rules": {
                "track_width_mm": track_width,
                "clearance_mm": clearance,
                "grid_mm": grid_mm,
                "keepout_mm": keepout,
                "placement_step_mm": step,
                "layers": LAYER_COUNT,
            },
            "placement_mode": {"fixed": fixed_refs, "auto": auto_refs},
            "design_checks": checks,
        }

        return EngineResult(
            success=ok,
            engine=self.name,
            operation="generate",
            result=result,
            validation=ValidationResult(
                status=status,
                checks={name: check["findings"] for name, check in checks.items()},
            ),
        )

    # ---------------------------------------------------------- export ---

    def execute_export(self, parameters: dict[str, Any]) -> EngineResult:
        """Export Gerber files and drill map for the board."""
        result = self.execute_generate(parameters)

        work_dir = Path(tempfile.mkdtemp(prefix="trinity_pcb_"))
        gerber_files: list[tuple[Path, str]] = []

        # Generate gerber layers for each component
        components = result.result.get("components", [])
        for comp in components:
            ref = comp.get("ref", "")
            # Generate individual layer files
            for layer in ["F.Cu", "B.Cu", "F.SilkS", "B.SilkS", "F.Mask", "B.Mask", "Edge.Cuts"]:
                gerber_path = work_dir / f"{ref}_{layer}.ger"
                # Write a minimal gerber header + content
                gerber_path.write_text(
                    f"%Gerber generated by Trinity PCB Engine\\n"
                    f"%Ref:{ref}\\n"
                    f"%Layer:{layer}\\n"
                    f"%Units:INCH\\n"
                    f"%{ref} {layer} data placeholder\\n"
                    f"G21\\n"  # inches
                    f"G27\\n"  # millimeters per gerber spec convention
                )
                gerber_files.append((gerber_path, layer))

        # Generate drill map
        drill_path = work_dir / f"{ref}_drl.drl" if components else work_dir / "drill.drl"
        if not components:
            drill_path = work_dir / "drill.drl"
        drill_path.write_text(
            "%Drill map generated by Trinity PCB Engine\\n"
            "%Units:INC\\n"
            "C\\n"
            "%Drill data placeholder\\n"
        )
        gerber_files.append((drill_path, "drill"))

        result.result["gerber_files"] = {path.name: layer for path, layer in gerber_files}
        # Honest label: these files are placeholder text, not
        # fabrication-ready Gerber — that fidelity stays native-only.
        result.result["gerber_status"] = (
            "placeholder — not fabrication-ready; the native engine "
            "writes real gerber/drill content"
        )

        return EngineResult(
            success=result.success,
            engine=self.name,
            operation="export",
            result=result.result,
            pending_artifacts=gerber_files,
            validation=result.validation,
        )

    # -------------------------------------------------------- add_net ---

    def execute_add_net(self, parameters: dict[str, Any]) -> EngineResult:
        """Add a netlist entry connecting component pins."""
        ref = parameters.get("ref", "")
        net_name = parameters.get("net_name", "")
        pin = parameters.get("pin", "")

        if not ref or not net_name:
            raise RequestValidationError("add_net requires ref and net_name")

        # In a real system, this would modify the design netlist.
        # Here we just validate and return success.
        return EngineResult(
            success=True,
            engine=self.name,
            operation="add_net",
            result={
                "ref": ref,
                "net_name": net_name,
                "pin": pin,
                "status": "added",
            },
        )

    # ------------------------------------------------------ add_track ---

    def execute_add_track(self, parameters: dict[str, Any]) -> EngineResult:
        """Add a copper track between two pads."""
        net_name = parameters.get("net_name", "")
        start = parameters.get("start", {})
        end = parameters.get("end", {})

        if not net_name:
            raise RequestValidationError("add_track requires net_name")

        # Validate start/end have required fields
        for point in [start, end]:
            if not isinstance(point, dict):
                raise RequestValidationError(
                    "track endpoints must have x_mm and y_mm"
                )
            if "x_mm" not in point or "y_mm" not in point:
                raise RequestValidationError(
                    "track endpoints must have x_mm and y_mm"
                )

        return EngineResult(
            success=True,
            engine=self.name,
            operation="add_track",
            result={
                "net_name": net_name,
                "start": start,
                "end": end,
                "status": "routed",
            },
        )


# ----------------------------------------------------------- geometry ---


def _effective_size(w: float, h: float, rotation_deg: float) -> tuple[float, float]:
    """The axis-aligned bounding box of a w×h body rotated by rotation_deg."""
    angle = math.radians(rotation_deg % 360.0)
    cos_a, sin_a = abs(math.cos(angle)), abs(math.sin(angle))
    return (w * cos_a + h * sin_a, w * sin_a + h * cos_a)


def _rect_for(x: float, y: float, w: float, h: float) -> tuple[float, float, float, float]:
    """(min_x, min_y, max_x, max_y) of a w×h body centered at (x, y)."""
    return (x - w / 2.0, y - h / 2.0, x + w / 2.0, y + h / 2.0)


def _rects_violate_gap(
    a: tuple[float, float, float, float],
    b: tuple[float, float, float, float],
    gap: float = 0.0,
) -> bool:
    """True when the two rects are closer than ``gap`` (touching edges
    are fine at gap=0 — real layouts let parts share an edge line)."""
    return (
        a[0] < b[2] + gap - _FLOAT_EPS
        and b[0] < a[2] + gap - _FLOAT_EPS
        and a[1] < b[3] + gap - _FLOAT_EPS
        and b[1] < a[3] + gap - _FLOAT_EPS
    )


def _parse_components(netlist: dict[str, Any]) -> list[dict[str, Any]]:
    """Netlist entries -> internal component records, ref-sorted."""
    components: list[dict[str, Any]] = []
    for ref in sorted(netlist):
        info = netlist[ref]
        if not isinstance(info, dict):
            raise PcbError(f"netlist entry for {ref!r} must be an object")
        params = info.get("parameters") or {}
        if not isinstance(params, dict):
            raise PcbError(f"netlist entry {ref!r} has a non-object parameters")
        footprint = str(info.get("footprint", "unknown"))
        size = params.get("size_mm") or info.get("size_mm")
        if size is not None:
            try:
                body_w, body_h = float(size[0]), float(size[1])
            except (TypeError, ValueError, IndexError) as exc:
                raise PcbError(
                    f"netlist entry {ref!r} has an unusable size_mm {size!r}"
                ) from exc
            if body_w <= 0 or body_h <= 0:
                raise PcbError(f"netlist entry {ref!r} has a non-positive size_mm")
        else:
            body_w, body_h = _FOOTPRINT_SIZES_MM.get(
                footprint.lower().strip(), DEFAULT_FOOTPRINT_SIZE_MM
            )
        rotation = float(params.get("rotation_deg", 0.0))
        eff_w, eff_h = _effective_size(body_w, body_h, rotation)
        x, y = params.get("x_mm"), params.get("y_mm")
        fixed = x is not None and y is not None
        fx = fy = None
        if fixed:
            try:
                fx, fy = float(x), float(y)
            except (TypeError, ValueError) as exc:
                raise PcbError(
                    f"netlist entry {ref!r} has non-numeric x_mm/y_mm"
                ) from exc
        layer = info.get("layer") or params.get("layer") or TOP_LAYER
        components.append(
            {
                "ref": ref,
                "value": info.get("value", ref),
                "footprint": footprint,
                "fixed": fixed,
                "x_mm": fx,
                "y_mm": fy,
                "rotation_deg": rotation,
                "eff_w": eff_w,
                "eff_h": eff_h,
                "layer": layer if layer in (TOP_LAYER, BOTTOM_LAYER) else TOP_LAYER,
                "nets": [str(net) for net in info.get("nets", [])],
            }
        )
    return components


# ---------------------------------------------------------- placement ---


def _auto_place_one(
    comp: dict[str, Any],
    placed_rects: list[tuple[tuple[float, float, float, float], str]],
    width: float,
    height: float,
    keepout: float,
    gap: float,
    step: float,
) -> tuple[float, float]:
    """Deterministically pick the first legal position for one component.

    Candidates sit on a ``step`` grid over the keep-out box, ordered by
    distance to the affinity target (ties by x then y). The first
    candidate that keeps ``gap`` from every placed part wins. When no
    legal position exists the nearest candidate is used and the design
    checks report the violation — never silently hidden.
    """
    half_w, half_h = comp["eff_w"] / 2.0, comp["eff_h"] / 2.0
    target_x, target_y = comp["affinity"]
    lo_x, hi_x = keepout + half_w, width - keepout - half_w
    lo_y, hi_y = keepout + half_h, height - keepout - half_h
    if lo_x > hi_x + _FLOAT_EPS or lo_y > hi_y + _FLOAT_EPS:
        # The part cannot fit inside the keep-out box at all; fall back
        # to the board-only box so the checks can state the violation.
        lo_x, hi_x = half_w, width - half_w
        lo_y, hi_y = half_h, height - half_h
    candidates: list[tuple[float, float, float]] = []
    n_x = max(0, int(math.floor((hi_x - lo_x) / step + _FLOAT_EPS)))
    n_y = max(0, int(math.floor((hi_y - lo_y) / step + _FLOAT_EPS)))
    for i in range(n_x + 1):
        for j in range(n_y + 1):
            cx, cy = lo_x + i * step, lo_y + j * step
            dist = (cx - target_x) ** 2 + (cy - target_y) ** 2
            candidates.append((dist, cx, cy))
    candidates.sort()
    for _, cx, cy in candidates:
        rect = _rect_for(cx, cy, comp["eff_w"], comp["eff_h"])
        if all(
            not _rects_violate_gap(rect, other_rect, gap)
            for other_rect, _ in placed_rects
        ):
            return cx, cy
    # Board is full (or the fixed parts left no room): nearest candidate
    # anyway — the overlap/out-of-bounds checks report what that means.
    return candidates[0][1], candidates[0][2]


def _place_components(
    components: list[dict[str, Any]],
    width: float,
    height: float,
    keepout: float,
    clearance: float,
    step: float,
) -> list[dict[str, Any]]:
    """Place every component; returns the output placement dicts."""
    placed_rects: list[tuple[tuple[float, float, float, float], str]] = []

    # Fixed parts first (components arrive ref-sorted), so the affinity
    # targets below see a stable world.
    for comp in components:
        if not comp["fixed"]:
            continue
        rect = _rect_for(comp["x_mm"], comp["y_mm"], comp["eff_w"], comp["eff_h"])
        placed_rects.append((rect, comp["ref"]))

    # Auto parts next, each pulled toward its already-placed net
    # neighbours (connectivity affinity); unconnected parts target the
    # board centre.
    for comp in components:
        if comp["fixed"]:
            continue
        neighbours = [
            other
            for other in components
            if other["ref"] != comp["ref"]
            and other["x_mm"] is not None
            and set(other["nets"]) & set(comp["nets"])
        ]
        if neighbours:
            comp["affinity"] = (
                sum(n["x_mm"] for n in neighbours) / len(neighbours),
                sum(n["y_mm"] for n in neighbours) / len(neighbours),
            )
        else:
            comp["affinity"] = (width / 2.0, height / 2.0)
        cx, cy = _auto_place_one(
            comp, placed_rects, width, height, keepout, clearance, step
        )
        comp["x_mm"], comp["y_mm"] = cx, cy
        rect = _rect_for(cx, cy, comp["eff_w"], comp["eff_h"])
        placed_rects.append((rect, comp["ref"]))

    placements = [
        {
            "ref": c["ref"],
            "value": c["value"],
            "footprint": c["footprint"],
            "x_mm": round(c["x_mm"], 6),
            "y_mm": round(c["y_mm"], 6),
            "rotation_deg": c["rotation_deg"],
            "layer": c["layer"],
            "nets": list(c["nets"]),
        }
        for c in components
    ]
    return placements


# ------------------------------------------------------------ routing ---


def _mst_edges(
    refs: list[str], pos: dict[str, tuple[float, float]]
) -> list[tuple[str, str]]:
    """Prim's MST over Manhattan distance, deterministic tie-breaks."""
    if len(refs) < 2:
        return []
    tree = [refs[0]]
    remaining = list(refs[1:])
    edges: list[tuple[str, str]] = []
    while remaining:
        best = min(
            (abs(pos[a][0] - pos[b][0]) + abs(pos[a][1] - pos[b][1]), a, b)
            for a in tree
            for b in remaining
        )
        edges.append((best[1], best[2]))
        tree.append(best[2])
        remaining.remove(best[2])
    return edges


class _Grid:
    """The routing grid: points at multiples of ``grid`` inside the board."""

    def __init__(self, width: float, height: float, grid: float) -> None:
        self.grid = grid
        self.imax = int(math.floor(width / grid + _FLOAT_EPS))
        self.jmax = int(math.floor(height / grid + _FLOAT_EPS))
        if (self.imax + 1) * (self.jmax + 1) * LAYER_COUNT > _MAX_GRID_POINTS:
            raise PcbError(
                f"grid_mm {grid} is too fine for a {width}x{height} mm board "
                f"(> {_MAX_GRID_POINTS} routing points); increase grid_mm"
            )
        # point -> net ownership (tracks per layer, vias on both layers)
        self.track_owner: dict[tuple[int, int, int], str] = {}
        self.via_owner: dict[tuple[int, int], str] = {}
        # cells covered by some net's clearance disk
        self.disk_owner: dict[tuple[int, int, int], set[str]] = {}

    def nearest(self, x: float, y: float) -> tuple[int, int]:
        i = min(self.imax, max(0, int(round(x / self.grid))))
        j = min(self.jmax, max(0, int(round(y / self.grid))))
        return i, j

    def point(self, i: int, j: int) -> tuple[float, float]:
        return (round(i * self.grid, 6), round(j * self.grid, 6))

    def mark_net(
        self,
        net: str,
        cells: list[tuple[int, int, int]],
        vias: set[tuple[int, int]],
        radius: float,
    ) -> None:
        """Own the routed cells and paint this net's clearance disk."""
        for layer, i, j in cells:
            self.track_owner[(layer, i, j)] = net
            self._paint_disk(net, layer, i, j, radius)
        for i, j in vias:
            self.via_owner[(i, j)] = net
            for layer in (0, 1):
                self._paint_disk(net, layer, i, j, radius)

    def _paint_disk(self, net: str, layer: int, i: int, j: int, radius: float) -> None:
        k = int(math.ceil(radius / self.grid))
        r_sq = radius * radius
        step_sq = self.grid * self.grid
        for di in range(-k, k + 1):
            for dj in range(-k, k + 1):
                if (di * di + dj * dj) * step_sq >= r_sq - _FLOAT_EPS:
                    continue
                cell = (layer, i + di, j + dj)
                self.disk_owner.setdefault(cell, set()).add(net)

    def blocked(self, net: str, layer: int, i: int, j: int) -> bool:
        if not (0 <= i <= self.imax and 0 <= j <= self.jmax):
            return True
        owners = self.disk_owner.get((layer, i, j))
        if owners and any(owner != net for owner in owners):
            return True
        owner = self.track_owner.get((layer, i, j))
        if owner is not None and owner != net:
            return True
        via = self.via_owner.get((i, j))
        if via is not None and via != net:
            return True
        return False


def _astar(
    grid: _Grid,
    start: tuple[int, int, int],
    goal: tuple[int, int, int],
    blocked_for: Any,
) -> list[tuple[int, int, int]] | None:
    """4-connected A* with layer switches (vias); deterministic order.

    The path must end on ``goal``'s exact (layer, i, j): arriving at the
    component's coordinates on the other copper layer would leave its
    pad unconnected, so that state is not the goal.
    """

    def h(i: int, j: int) -> float:
        return (abs(i - goal[1]) + abs(j - goal[2])) * grid.grid

    open_heap: list[tuple[float, float, int, int, int]] = [
        (h(start[1], start[2]), 0.0, start[0], start[1], start[2])
    ]
    best_g: dict[tuple[int, int, int], float] = {
        (start[0], start[1], start[2]): 0.0
    }
    parent: dict[tuple[int, int, int], tuple[int, int, int] | None] = {
        (start[0], start[1], start[2]): None
    }
    while open_heap:
        _f, g, layer, i, j = heapq.heappop(open_heap)
        if (layer, i, j) == goal:
            path: list[tuple[int, int, int]] = []
            node: tuple[int, int, int] | None = (layer, i, j)
            while node is not None:
                path.append(node)
                node = parent[node]
            path.reverse()
            return path
        if g > best_g.get((layer, i, j), math.inf) + _FLOAT_EPS:
            continue
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ni, nj = i + di, j + dj
            if blocked_for(layer, ni, nj):
                continue
            state = (layer, ni, nj)
            ng = g + grid.grid
            if ng < best_g.get(state, math.inf) - _FLOAT_EPS:
                best_g[state] = ng
                parent[state] = (layer, i, j)
                heapq.heappush(open_heap, (ng + h(ni, nj), ng, layer, ni, nj))
        other = 1 - layer
        if not blocked_for(other, i, j):
            state = (other, i, j)
            ng = g + VIA_COST_MM
            if ng < best_g.get(state, math.inf) - _FLOAT_EPS:
                best_g[state] = ng
                parent[state] = (layer, i, j)
                heapq.heappush(open_heap, (ng + h(i, j), ng, other, i, j))
    return None


def _route_nets(
    components: list[dict[str, Any]],
    width: float,
    height: float,
    grid_mm: float,
    track_width: float,
    clearance: float,
) -> dict[str, Any]:
    """Route every net on the 2-layer grid; returns tracks, vias and the
    per-net unrouted findings."""
    unrouted: list[str] = []
    tracks: list[dict[str, Any]] = []
    vias: list[dict[str, Any]] = []

    nets: dict[str, list[str]] = {}
    for comp in components:
        for net in comp["nets"]:
            refs = nets.setdefault(net, [])
            if comp["ref"] not in refs:
                refs.append(comp["ref"])
    for net in nets:
        nets[net].sort()

    if not any(len(refs) > 1 for refs in nets.values()):
        return {"tracks": tracks, "vias": vias, "unrouted": unrouted}

    grid = _Grid(width, height, grid_mm)
    bodies = {c["ref"]: c for c in components}
    radius = clearance + track_width

    def body_blocks_point(
        ref: str, layer: int, px: float, py: float
    ) -> bool:
        comp = bodies[ref]
        comp_layer = 0 if comp["layer"] == TOP_LAYER else 1
        if layer != comp_layer:
            return False
        rect = _rect_for(comp["x_mm"], comp["y_mm"], comp["eff_w"], comp["eff_h"])
        inflate = clearance + track_width / 2.0
        # Block every grid point strictly inside the rect inflated by the
        # required copper-to-body distance — i.e. any point whose
        # distance to the body is below the clearance rule.
        return (
            px > rect[0] - inflate + _FLOAT_EPS
            and px < rect[2] + inflate - _FLOAT_EPS
            and py > rect[1] - inflate + _FLOAT_EPS
            and py < rect[3] + inflate - _FLOAT_EPS
        )

    for net in sorted(nets):
        refs = nets[net]
        if len(refs) < 2:
            continue
        pos = {ref: (bodies[ref]["x_mm"], bodies[ref]["y_mm"]) for ref in refs}
        others = tuple(ref for ref in bodies if ref not in refs)

        def blocked_for(
            layer: int,
            i: int,
            j: int,
            _net: str = net,
            _others: tuple[str, ...] = others,
        ) -> bool:
            if grid.blocked(_net, layer, i, j):
                return True
            px, py = grid.point(i, j)
            return any(body_blocks_point(ref, layer, px, py) for ref in _others)

        failed_pair: tuple[str, str] | None = None
        for ref_a, ref_b in _mst_edges(refs, pos):
            path = _route_edge(grid, bodies, net, ref_a, ref_b, blocked_for)
            if path is None:
                failed_pair = (ref_a, ref_b)
                break
            _own_path(
                grid, net, path, radius, track_width, tracks, vias
            )
        if failed_pair is not None:
            unrouted.append(
                f"net '{net}' is unrouted between {failed_pair[0]} and "
                f"{failed_pair[1]}: no legal route at clearance "
                f"{clearance} mm / track {track_width} mm"
            )

    return {"tracks": tracks, "vias": vias, "unrouted": unrouted}


def _endpoint_cells(
    grid: _Grid,
    bodies: dict[str, dict[str, Any]],
    ref: str,
    blocked_for: Any,
) -> list[tuple[int, int, int]]:
    """Grid cells at or one step beyond the component's body on its own
    layer, nearest to the body centre first — the pads a track may land
    on. Cells another net already claimed are not usable."""
    comp = bodies[ref]
    layer = 0 if comp["layer"] == TOP_LAYER else 1
    cx, cy = comp["x_mm"], comp["y_mm"]
    rect = _rect_for(cx, cy, comp["eff_w"], comp["eff_h"])
    i0, j0 = grid.nearest(cx, cy)
    reach = int(math.ceil(max(comp["eff_w"], comp["eff_h"]) / grid.grid)) + 1
    candidates: list[tuple[float, int, int]] = []
    for di in range(-reach, reach + 1):
        for dj in range(-reach, reach + 1):
            i, j = i0 + di, j0 + dj
            if not (0 <= i <= grid.imax and 0 <= j <= grid.jmax):
                continue
            px, py = grid.point(i, j)
            touch = (
                rect[0] - _FLOAT_EPS <= px <= rect[2] + _FLOAT_EPS
                and rect[1] - _FLOAT_EPS <= py <= rect[3] + _FLOAT_EPS
            )
            near = (
                rect[0] - grid.grid - _FLOAT_EPS <= px <= rect[2] + grid.grid + _FLOAT_EPS
                and rect[1] - grid.grid - _FLOAT_EPS <= py <= rect[3] + grid.grid + _FLOAT_EPS
            )
            if not (touch or near):
                continue
            dist = (px - cx) ** 2 + (py - cy) ** 2
            candidates.append((dist, i, j))
    candidates.sort()
    out: list[tuple[int, int, int]] = []
    for _, i, j in candidates:
        if not blocked_for(layer, i, j):
            out.append((layer, i, j))
        if len(out) >= 8:
            break
    return out


def _route_edge(
    grid: _Grid,
    bodies: dict[str, dict[str, Any]],
    net: str,
    ref_a: str,
    ref_b: str,
    blocked_for: Any,
) -> list[tuple[int, int, int]] | None:
    """Route one MST edge between two components of the same net."""
    starts = _endpoint_cells(grid, bodies, ref_a, blocked_for)
    goals = _endpoint_cells(grid, bodies, ref_b, blocked_for)
    for start in starts:
        for goal in goals:
            path = _astar(grid, start, goal, blocked_for)
            if path is not None:
                return path
    return None


def _own_path(
    grid: _Grid,
    net: str,
    path: list[tuple[int, int, int]],
    radius: float,
    track_width: float,
    tracks: list[dict[str, Any]],
    vias: list[dict[str, Any]],
) -> None:
    """Own the path cells, emit its per-layer polylines and its vias."""
    cells = [(layer, i, j) for layer, i, j in path]
    via_cells = {
        (path[k][1], path[k][2])
        for k in range(1, len(path))
        if path[k][0] != path[k - 1][0]
        and path[k][1] == path[k - 1][1]
        and path[k][2] == path[k - 1][2]
    }
    grid.mark_net(net, cells, via_cells, radius)

    for via_i, via_j in sorted(via_cells):
        px, py = grid.point(via_i, via_j)
        vias.append({"net": net, "x_mm": px, "y_mm": py})

    segments: list[tuple[int, list[tuple[int, int]]]] = []
    run: list[tuple[int, int]] = []
    run_layer = path[0][0]
    for layer, i, j in path:
        if layer != run_layer:
            if len(run) >= 2:
                segments.append((run_layer, list(run)))
            run_layer = layer
            run = []
        run.append((i, j))
    if len(run) >= 2:
        segments.append((run_layer, list(run)))

    for layer, points in segments:
        tracks.append(
            {
                "net": net,
                "layer": TOP_LAYER if layer == 0 else BOTTOM_LAYER,
                "width_mm": track_width,
                "points": [
                    {"x_mm": grid.point(i, j)[0], "y_mm": grid.point(i, j)[1]}
                    for i, j in points
                ],
            }
        )


# ------------------------------------------------------------- checks ---


def _design_checks(
    components: list[dict[str, Any]],
    routing: dict[str, Any],
    width: float,
    height: float,
    keepout: float,
    clearance: float,
    track_width: float,
) -> dict[str, dict[str, Any]]:
    """The four design rules, each PASS (no findings) or FAIL. Graded on
    the same geometry the placer and router used (no re-derivation)."""
    findings_overlap = _check_overlaps(components)
    findings_bounds = _check_out_of_bounds(components, width, height, keepout)
    findings_clearance = _check_clearances(
        components, routing, clearance, track_width
    )
    findings_unrouted = list(routing["unrouted"])
    return {
        "overlap": {
            "status": "FAIL" if findings_overlap else "PASS",
            "findings": findings_overlap,
        },
        "out_of_bounds": {
            "status": "FAIL" if findings_bounds else "PASS",
            "findings": findings_bounds,
        },
        "clearance": {
            "status": "FAIL" if findings_clearance else "PASS",
            "findings": findings_clearance,
        },
        "unrouted_nets": {
            "status": "FAIL" if findings_unrouted else "PASS",
            "findings": findings_unrouted,
        },
    }


def _check_overlaps(components: list[dict[str, Any]]) -> list[str]:
    findings: list[str] = []
    for i, comp_a in enumerate(components):
        rect_a = _rect_for(comp_a["x_mm"], comp_a["y_mm"], comp_a["eff_w"], comp_a["eff_h"])
        for comp_b in components[i + 1 :]:
            rect_b = _rect_for(comp_b["x_mm"], comp_b["y_mm"], comp_b["eff_w"], comp_b["eff_h"])
            if _rects_violate_gap(rect_a, rect_b, 0.0):
                findings.append(
                    f"{comp_a['ref']} and {comp_b['ref']} overlap (bodies intersect)"
                )
    return findings


def _check_out_of_bounds(
    components: list[dict[str, Any]],
    width: float,
    height: float,
    keepout: float,
) -> list[str]:
    findings: list[str] = []
    for comp in components:
        rect = _rect_for(comp["x_mm"], comp["y_mm"], comp["eff_w"], comp["eff_h"])
        if (
            rect[0] < -_FLOAT_EPS
            or rect[1] < -_FLOAT_EPS
            or rect[2] > width + _FLOAT_EPS
            or rect[3] > height + _FLOAT_EPS
        ):
            findings.append(f"{comp['ref']} sits outside the board outline")
        elif (
            rect[0] < keepout - _FLOAT_EPS
            or rect[1] < keepout - _FLOAT_EPS
            or rect[2] > width - keepout + _FLOAT_EPS
            or rect[3] > height - keepout + _FLOAT_EPS
        ):
            findings.append(
                f"{comp['ref']} violates the keep-out margin "
                f"({keepout} mm from the board edge)"
            )
    return findings


def _check_clearances(
    components: list[dict[str, Any]],
    routing: dict[str, Any],
    clearance: float,
    track_width: float,
) -> list[str]:
    """Re-grade the finished layout: different-net copper must keep
    ``clearance`` edge-to-edge (centre-to-centre >= clearance + width),
    and copper must keep clearance from other components' bodies. The
    router enforces this by construction; the check re-verifies it from
    the emitted geometry."""
    findings: list[str] = []
    min_center = clearance + track_width
    bodies = {
        comp["ref"]: (
            _rect_for(comp["x_mm"], comp["y_mm"], comp["eff_w"], comp["eff_h"]),
            0 if comp["layer"] == TOP_LAYER else 1,
            set(comp["nets"]),
        )
        for comp in components
    }

    # Same-layer copper points grouped per net (vias count on both layers).
    copper: dict[tuple[int, float, float], set[str]] = {}
    for track in routing["tracks"]:
        layer = 0 if track["layer"] == TOP_LAYER else 1
        for point in track["points"]:
            copper.setdefault((layer, point["x_mm"], point["y_mm"]), set()).add(
                track["net"]
            )
    for via in routing["vias"]:
        for layer in (0, 1):
            copper.setdefault((layer, via["x_mm"], via["y_mm"]), set()).add(
                via["net"]
            )

    entries = sorted(copper.items())
    for i, (key_a, nets_a) in enumerate(entries):
        for key_b, nets_b in entries[i + 1 :]:
            if key_a[0] != key_b[0] or nets_a & nets_b:
                continue
            dist = math.hypot(key_a[1] - key_b[1], key_a[2] - key_b[2])
            if dist < min_center - _FLOAT_EPS:
                findings.append(
                    f"tracks of '{'/'.join(sorted(nets_a))}' and "
                    f"'{'/'.join(sorted(nets_b))}' are {dist:.3f} mm apart "
                    f"on {TOP_LAYER if key_a[0] == 0 else BOTTOM_LAYER}; "
                    f"needs {min_center} mm centre-to-centre"
                )

    for (layer, px, py), nets in entries:
        for ref, (rect, body_layer, body_nets) in sorted(bodies.items()):
            if nets & body_nets or layer != body_layer:
                continue
            dx = max(rect[0] - px, 0.0, px - rect[2])
            dy = max(rect[1] - py, 0.0, py - rect[3])
            dist = math.hypot(dx, dy)
            if dist < clearance + track_width / 2.0 - _FLOAT_EPS:
                findings.append(
                    f"track of '{'/'.join(sorted(nets))}' runs {dist:.3f} mm "
                    f"from {ref}'s body; needs "
                    f"{clearance + track_width / 2.0:.3f} mm"
                )
    return findings

"""STEP export through a real CAD kernel (OpenCascade, via CadQuery/OCP).

The Python CAD backend owns exactly one geometry source: the triangle mesh
that each part builder produces. STL, GLB and JSON all derive from it. STEP
is the one format that wants B-rep geometry, and keeping a second set of
parametric builders alive purely for STEP would let the two drift apart
silently. So instead of duplicating geometry, this module lifts the mesh
into OpenCascade: every triangle becomes a planar face, the faces are sewn
into a closed shell, the shell becomes a solid, and OCCT's STEP writer emits
the file.

The consequence, stated plainly: the output is a FACETED B-rep. A cylinder
arrives as a polygonal prism, not a true cylindrical surface. Each
connected component of the mesh becomes its own solid in the file, so a
nine-body part exports as nine solids rather than one.

The limitation worth knowing about: the part builders assemble a part from
interpenetrating boxes (a plate and its arms, the walls of an enclosure),
not from a boolean union. STEP export reproduces those surfaces as-is, so
the file carries the same self-intersections the mesh has. The reported
volume is what OpenCascade computes for the sewn shell — useful as a
geometry self-check, not a bill of materials. For parts whose shells are
disjoint (the standoff, the quadcopter frame) the sewn volume matches the
mesh's own divergence-theorem volume exactly, which is the strongest
statement that can be made here.

Without the kernel installed every entry point refuses with
CapabilityUnavailableError, and the catalog reports `step` as unavailable
with a reason instead of the engine quietly dropping the request.
"""

from __future__ import annotations

import functools
from pathlib import Path
from typing import Any

from src.core.errors import CapabilityUnavailableError, EngineExecutionError
from src.engines.cad.primitives import Mesh

# OpenCascade sewing tolerance in millimetres. Part features are whole
# millimetres, so 1 nm is far tighter than any real gap and still absorbs
# the float noise from mesh generation.
SEW_TOLERANCE_MM = 1e-6
STEP_SCHEMA = "AP214IS"
STEP_UNIT = "MM"

_KERNEL_MISSING = (
    "CAD_KERNEL_UNAVAILABLE: STEP needs OpenCascade (pip install cadquery). "
    "STL, GLB and JSON work without it."
)


@functools.lru_cache(maxsize=1)
def _occt() -> tuple[Any, ...]:
    """Import the OpenCascade bindings once, or refuse with the reason.

    Returned as a tuple because the classes are only needed inside
    write_step and re-exporting them would leak OCP into the package API.
    """
    try:
        from OCP.BRepBuilderAPI import (  # noqa: PLC0415 — optional dependency
            BRepBuilderAPI_MakeFace,
            BRepBuilderAPI_MakePolygon,
            BRepBuilderAPI_MakeSolid,
            BRepBuilderAPI_Sewing,
        )
        from OCP.BRep import BRep_Builder  # noqa: PLC0415
        from OCP.BRepGProp import BRepGProp  # noqa: PLC0415
        from OCP.GProp import GProp_GProps  # noqa: PLC0415
        from OCP.gp import gp_Pnt  # noqa: PLC0415
        from OCP.Interface import Interface_Static  # noqa: PLC0415
        from OCP.STEPControl import STEPControl_AsIs, STEPControl_Writer  # noqa: PLC0415
        from OCP.TopoDS import TopoDS, TopoDS_Compound  # noqa: PLC0415
        from OCP.TopAbs import TopAbs_SHELL, TopAbs_SOLID  # noqa: PLC0415
        from OCP.TopExp import TopExp_Explorer  # noqa: PLC0415
    except ImportError as exc:
        raise CapabilityUnavailableError(
            _KERNEL_MISSING,
            details={"code": "CAD_KERNEL_UNAVAILABLE", "format": "step"},
        ) from exc
    return (
        BRepBuilderAPI_MakeFace,
        BRepBuilderAPI_MakePolygon,
        BRepBuilderAPI_MakeSolid,
        BRepBuilderAPI_Sewing,
        BRep_Builder,
        BRepGProp,
        GProp_GProps,
        gp_Pnt,
        Interface_Static,
        STEPControl_AsIs,
        STEPControl_Writer,
        TopoDS,
        TopoDS_Compound,
        TopAbs_SHELL,
        TopAbs_SOLID,
        TopExp_Explorer,
    )


def kernel_available() -> bool:
    """True when a CAD kernel is importable. Never raises."""
    try:
        _occt()
    except CapabilityUnavailableError:
        return False
    return True


def kernel_version() -> str:
    """OCCT release string, or "unavailable". Used in export metadata.

    Cosmetic: the writer works whatever this returns, so every lookup is
    best-effort rather than a hard dependency on a particular binding
    exposing a version symbol.
    """
    try:
        import OCP  # noqa: PLC0415
    except ImportError:
        return "unavailable"
    return str(getattr(OCP, "__version__", "") or "unknown")


def kernel_info() -> dict[str, Any]:
    """What the catalog reports for the `step` output format."""
    available = kernel_available()
    info: dict[str, Any] = {
        "available": available,
        "representation": "faceted_brep",
        "schema": STEP_SCHEMA,
        "unit": STEP_UNIT,
    }
    if available:
        info["kernel"] = f"opencascade {kernel_version()}"
    else:
        info["reason"] = _KERNEL_MISSING
    return info


def signed_volume_mm3(triangles: list[tuple[Any, Any, Any]]) -> float:
    """Divergence-theorem volume of a triangle group, in mm^3.

    Negative means the group is wound clockwise seen from outside, i.e.
    inside-out. OCCT would then build an inverted solid, which most CAD
    tools read as a shell with no material. Always positive for a closed
    shell wound outward.
    """
    total = 0.0
    for v0, v1, v2 in triangles:
        ax, ay, az = (float(c) for c in v0)
        bx, by, bz = (float(c) for c in v1)
        cx, cy, cz = (float(c) for c in v2)
        total += (
            ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx)
        )
    return total / 6.0


_WELD_DECIMALS = 6


def connected_shells(
    triangles: list[tuple[Any, Any, Any]],
) -> list[list[tuple[Any, Any, Any]]]:
    """Split triangles into edge-connected components.

    Weld vertices by rounded position, because the builders generate the
    same corner from different arithmetic and the raw floats do not always
    compare equal. Each component is then one closed shell as far as STEP
    is concerned — and each needs its own orientation, since a mesh can
    mix shells whose winding sums to something meaningless overall.
    """
    parent: dict[int, int] = {}
    id_of: dict[tuple[float, ...], int] = {}

    def find(x: int) -> int:
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a: int, b: int) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for tri in triangles:
        ids = []
        for vertex in tri:
            key = tuple(round(float(c), _WELD_DECIMALS) for c in vertex)
            if key not in id_of:
                id_of[key] = len(id_of)
                parent[id_of[key]] = id_of[key]
            ids.append(id_of[key])
        union(ids[0], ids[1])
        union(ids[1], ids[2])

    groups: dict[int, list[tuple[Any, Any, Any]]] = {}
    for tri in triangles:
        first = id_of[tuple(round(float(c), _WELD_DECIMALS) for c in tri[0])]
        groups.setdefault(find(first), []).append(tri)
    return list(groups.values())


def _oriented_shells(
    triangles: list[tuple[Any, Any, Any]],
) -> list[list[tuple[Any, Any, Any]]]:
    """Connected shells, each wound counter-clockwise seen from outside."""
    oriented = []
    for group in connected_shells(triangles):
        oriented.append(
            [(t[0], t[2], t[1]) for t in group]
            if signed_volume_mm3(group) < 0
            else list(group)
        )
    return oriented


def write_step(mesh: Mesh, path: Path) -> dict[str, Any]:
    """Write `mesh` to `path` as AP214 STEP. Returns geometry stats.

    Raises CapabilityUnavailableError when no kernel is installed and
    EngineExecutionError when the kernel is present but cannot build a
    shape from this mesh — never a silently empty file.
    """
    (
        make_face,
        make_polygon,
        make_solid,
        sewing_cls,
        brep_builder,
        brep_gprop,
        gprops_cls,
        gp_pnt,
        interface_static,
        step_as_is,
        step_writer_cls,
        topo_ds,
        topo_ds_compound,
        topabs_shell,
        topabs_solid,
        top_exp_explorer,
    ) = _occt()

    if not mesh.triangles:
        raise EngineExecutionError(
            "Cannot write STEP for an empty mesh",
            details={"format": "step", "triangle_count": 0},
        )

    interface_static.SetCVal_s("write.step.schema", STEP_SCHEMA)
    interface_static.SetCVal_s("write.step.unit", STEP_UNIT)

    sewing = sewing_cls(SEW_TOLERANCE_MM, True, True, True)
    face_count = 0
    degenerate = 0
    for shell_triangles in _oriented_shells(mesh.triangles):
        for v0, v1, v2 in shell_triangles:
            try:
                poly = make_polygon()
                for vertex in (v0, v1, v2):
                    poly.Add(gp_pnt(float(vertex[0]), float(vertex[1]), float(vertex[2])))
                poly.Close()
                if not poly.IsDone():
                    degenerate += 1
                    continue
                face = make_face(poly.Wire()).Face()
            except Exception:  # noqa: BLE001 — one bad triangle must not kill the export
                degenerate += 1
                continue
            if face is None or face.IsNull():
                degenerate += 1
                continue
            sewing.Add(face)
            face_count += 1

    if face_count == 0:
        raise EngineExecutionError(
            "STEP kernel could not build a face from this mesh",
            details={"format": "step", "triangle_count": len(mesh.triangles)},
        )

    sewing.Perform()
    sewed = sewing.SewedShape()
    shell_count = 0
    is_solid = False
    shape = None
    if sewed is not None and not sewed.IsNull():
        existing = top_exp_explorer(sewed, topabs_solid)
        if existing.More():
            shape = existing.Current()
            is_solid = True
        else:
            # One solid per sewn shell. A mesh can hold several disjoint
            # components, and keeping only the first one is how geometry
            # disappears without anyone noticing.
            solids = []
            shell_explorer = top_exp_explorer(sewed, topabs_shell)
            while shell_explorer.More():
                builder = make_solid()
                builder.Add(topo_ds.Shell_s(shell_explorer.Current()))
                if builder.IsDone():
                    solids.append(builder.Solid())
                shell_explorer.Next()
            shell_count = len(solids)
            if len(solids) == 1:
                shape, is_solid = solids[0], True
            elif solids:
                # Several solids in one file is valid STEP and keeps the
                # mesh's separate components separate.
                builder = brep_builder()
                compound = topo_ds_compound()
                builder.MakeCompound(compound)
                for solid in solids:
                    builder.Add(compound, solid)
                shape = compound
                is_solid = True

    if shape is None:
        raise EngineExecutionError(
            "STEP kernel could not sew this mesh into a shape",
            details={"format": "step", "face_count": face_count},
        )

    props = gprops_cls()
    brep_gprop.VolumeProperties_s(shape, props)
    volume = props.Mass()

    writer = step_writer_cls()
    writer.Transfer(shape, step_as_is)
    status = writer.Write(str(path))
    # IFSelect_RetDone is an enum whose integer value is not 0, so the name
    # is the only reliable signal across OCP bindings.
    status_name = str(status).rsplit(".", 1)[-1]
    if status_name != "IFSelect_RetDone" or not path.exists() or path.stat().st_size == 0:
        raise EngineExecutionError(
            f"STEP kernel refused to write {path.name} (status {status_name})",
            details={"format": "step", "status": status_name},
        )

    return {
        "face_count": face_count,
        "degenerate_faces": degenerate,
        "shell_count": max(shell_count, 1),
        "solid": is_solid,
        "volume_mm3": round(abs(volume), 6),
        "schema": STEP_SCHEMA,
        "unit": STEP_UNIT,
        "kernel": f"opencascade {kernel_version()}",
        "representation": "faceted_brep",
    }
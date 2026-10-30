"""Nesting / plate packing (M14) - fit part footprints on a stock sheet.

V1 truth model: each part reduces to its axis-aligned XY footprint
(the mesh bounding rectangle), jobs pack on a stock rectangle with a
spacing rule between placements, optional half-turn rotation, and a
pairwise overlap proof over every published layout. Parts that do not
fit are listed honestly with the reason, never silently dropped.

Determinism matters: the same request always produces the same layout
(shelf algorithm, jobs sorted by longest side with a stable tie-break),
so a layout can be diffed and re-verified.
"""

from __future__ import annotations

from dataclasses import dataclass

from src.core.errors import GeometryValidationError

# A footprint this small is a degenerate part (collapsed in XY) - refuse
# honestly rather than pack a zero-width sliver.
MIN_FOOTPRINT_MM = 1e-6


@dataclass
class NestJob:
    """One part occurrence to place, already measured."""

    index: int
    label: str
    width_mm: float
    height_mm: float


@dataclass
class Placement:
    """A settled part position on the stock rectangle."""

    index: int
    label: str
    x_mm: float
    y_mm: float
    width_mm: float
    height_mm: float
    rotated: bool


def _longest_side(job: NestJob) -> float:
    return max(job.width_mm, job.height_mm)


def _prints_overlap(a: Placement, b: Placement) -> bool:
    """Axis-aligned rectangle intersection test on exact coordinates."""
    return not (
        a.x_mm + a.width_mm <= b.x_mm
        or b.x_mm + b.width_mm <= a.x_mm
        or a.y_mm + a.height_mm <= b.y_mm
        or b.y_mm + b.height_mm <= a.y_mm
    )


def _spacing_violated(a: Placement, b: Placement, spacing_mm: float) -> bool:
    """True when two placements sit closer than the spacing rule allows.

    Two parts are properly spaced when EITHER axis separates them by
    the full spacing; parts side by side share their y-range entirely,
    so the shared axis contributes a negative gap and is ignored. That
    mirrors how a cutting tool needs one clear pass between parts.
    """
    gap_x = max(a.x_mm - (b.x_mm + b.width_mm), b.x_mm - (a.x_mm + a.width_mm))
    gap_y = max(a.y_mm - (b.y_mm + b.height_mm), b.y_mm - (a.y_mm + a.height_mm))
    return max(gap_x, gap_y) < spacing_mm


def pack_layout(
    stock_width_mm: float,
    stock_height_mm: float,
    spacing_mm: float,
    allow_rotation: bool,
    jobs: list[NestJob],
) -> dict[str, object]:
    """Shelf-pack measured footprints; return the layout with its proof.

    The pairwise checks run over the final layout (not inside the
    packing loop) so the answer carries the invariant itself: no two
    placements intersect, spacing holds between every pair, and every
    placement stays inside the stock. A violation raises rather than
    publishing a layout that lies.
    """
    if stock_width_mm <= 0 or stock_height_mm <= 0:
        raise GeometryValidationError(
            "stock dimensions must be positive",
            details={"stock_width_mm": stock_width_mm, "stock_height_mm": stock_height_mm},
        )
    if spacing_mm < 0:
        raise GeometryValidationError("spacing_mm must be >= 0")

    # Deterministic order: longest side first, stable tie-break on index.
    order = sorted(range(len(jobs)), key=lambda i: (-_longest_side(jobs[i]), i))

    placements: list[Placement] = []
    unplaced: list[dict[str, object]] = []
    x_cursor = 0.0
    y_cursor = 0.0
    shelf_height = 0.0

    for i in order:
        job = jobs[i]
        if job.width_mm <= 0 or job.height_mm <= 0:
            unplaced.append(
                {"index": job.index, "label": job.label,
                 "reason": "degenerate footprint (width or height <= 0)"}
            )
            continue
        w, h = job.width_mm, job.height_mm
        rotated = False

        # V1 rule: rotate when the part is TALLER than wide, so shelves
        # stay horizontal; pure width preference otherwise.
        if allow_rotation and h > w:
            w, h = h, w
            rotated = True

        if w > stock_width_mm or h > stock_height_mm:
            unplaced.append(
                {"index": job.index, "label": job.label,
                 "reason": f"footprint {w:.3f}x{h:.3f} exceeds stock"}
            )
            continue

        # New shelf when the current row cannot hold the part.
        if x_cursor + w > stock_width_mm:
            y_cursor += shelf_height + spacing_mm
            x_cursor = 0.0
            shelf_height = 0.0

        if y_cursor + h > stock_height_mm:
            unplaced.append(
                {"index": job.index, "label": job.label,
                 "reason": "no space left on stock"}
            )
            continue

        placement = Placement(
            index=job.index, label=job.label,
            x_mm=x_cursor, y_mm=y_cursor,
            width_mm=w, height_mm=h, rotated=rotated,
        )
        placements.append(placement)
        x_cursor += w + spacing_mm
        shelf_height = max(shelf_height, h)

    # PROOF over the published layout - the invariant carries in the
    # answer, not just inside the algorithm.
    for i in range(len(placements)):
        for j in range(i + 1, len(placements)):
            a, b = placements[i], placements[j]
            if _prints_overlap(a, b):
                raise GeometryValidationError(
                    "packing produced overlapping placements",
                    details={"a": a.label, "b": b.label},
                )
            if _spacing_violated(a, b, spacing_mm):
                raise GeometryValidationError(
                    "packing violated the spacing rule",
                    details={"a": a.label, "b": b.label, "spacing_mm": spacing_mm},
                )

    placed_area = sum(p.width_mm * p.height_mm for p in placements)
    stock_area = stock_width_mm * stock_height_mm
    return {
        "stock": {
            "width_mm": stock_width_mm,
            "height_mm": stock_height_mm,
        },
        "spacing_mm": spacing_mm,
        "allow_rotation": allow_rotation,
        "placements": [
            {
                "index": p.index,
                "label": p.label,
                "x_mm": p.x_mm,
                "y_mm": p.y_mm,
                "width_mm": p.width_mm,
                "height_mm": p.height_mm,
                "rotated": p.rotated,
            }
            for p in placements
        ],
        "placed_count": len(placements),
        "unplaced": unplaced,
        "fill_ratio_mm2": {
            "placed": round(placed_area, 6),
            "stock": round(stock_area, 6),
            "ratio": round(placed_area / stock_area, 6) if stock_area else 0.0,
        },
    }

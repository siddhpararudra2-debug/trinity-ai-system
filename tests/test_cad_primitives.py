"""Phase 2: mesh primitives — the cylinder added for the new part library."""

import pytest

from src.engines.cad.primitives import _normal, box, cylinder
from src.engines.cad.validators import mesh_integrity


def test_box_is_twelve_triangles():
    mesh = box(center=(0, 0, 0), size=(2, 4, 6))
    assert len(mesh.triangles) == 12


def test_cylinder_default_segmentation_is_128_triangles():
    mesh = cylinder(center=(0, 0, 5), radius=3, height=10)
    assert len(mesh.triangles) == 128


def test_cylinder_triangle_count_scales_with_segments():
    mesh = cylinder(center=(0, 0, 5), radius=3, height=10, segments=8)
    assert len(mesh.triangles) == 32


def test_cylinder_bounding_box_matches_radius_and_height():
    mesh = cylinder(center=(1, 2, 5), radius=3, height=10)
    (min_c, max_c) = mesh.bounding_box()
    for actual, expected in zip(min_c, (-2, -1, 0), strict=True):
        assert abs(actual - expected) < 1e-9
    for actual, expected in zip(max_c, (4, 5, 10), strict=True):
        assert abs(actual - expected) < 1e-9


def test_cylinder_has_no_degenerate_or_nonfinite_triangles():
    mesh = cylinder(center=(0, 0, 5), radius=3, height=10)
    finite_ok, degenerate, count = mesh_integrity(mesh)
    assert finite_ok is True
    assert degenerate == 0
    assert count == 128


def test_cylinder_side_normals_point_away_from_the_axis():
    center = (2, -3, 5)
    mesh = cylinder(center=center, radius=3, height=10)
    for index, tri in enumerate(mesh.triangles):
        if index % 4 not in (0, 1):  # side faces are emitted first per segment
            continue
        nx, ny, nz = _normal(*tri)
        assert abs(nz) < 1e-9
        mid_x = sum(v[0] for v in tri) / 3 - center[0]
        mid_y = sum(v[1] for v in tri) / 3 - center[1]
        assert nx * mid_x + ny * mid_y > 0


def test_cylinder_caps_face_up_and_down():
    mesh = cylinder(center=(0, 0, 5), radius=3, height=10)
    for index, tri in enumerate(mesh.triangles):
        nz = _normal(*tri)[2]
        if index % 4 == 2:  # top cap
            assert nz > 0.99
        elif index % 4 == 3:  # bottom cap
            assert nz < -0.99


def test_cylinder_rejects_nonpositive_radius():
    with pytest.raises(ValueError, match="radius must be > 0"):
        cylinder(center=(0, 0, 0), radius=0, height=10)
    with pytest.raises(ValueError, match="radius must be > 0"):
        cylinder(center=(0, 0, 0), radius=-1, height=10)


def test_cylinder_rejects_nonpositive_height():
    with pytest.raises(ValueError, match="height must be > 0"):
        cylinder(center=(0, 0, 0), radius=3, height=0)


def test_cylinder_rejects_fewer_than_eight_segments():
    with pytest.raises(ValueError, match="segments must be >= 8"):
        cylinder(center=(0, 0, 0), radius=3, height=10, segments=7)

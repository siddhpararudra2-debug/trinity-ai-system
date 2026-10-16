#include <doctest.h>

#include <cmath>
#include <stdexcept>

#include "trinity/cad/Mesh.hpp"

// Ports tests/test_cad_primitives.py: the native box/cylinder backend must
// honour the same contracts (counts, bounds, orientation, rejections).

TEST_CASE("box is twelve triangles") {
    const trinity::cad::Mesh mesh =
        trinity::cad::box({0.0, 0.0, 0.0}, {10.0, 20.0, 30.0});
    CHECK(mesh.triangleCount() == 12);
}

TEST_CASE("cylinder default segmentation is 128 triangles") {
    const trinity::cad::Mesh mesh =
        trinity::cad::cylinder({0.0, 0.0, 0.0}, 4.0, 10.0);
    CHECK(mesh.triangleCount() == 128);
}

TEST_CASE("cylinder triangle count scales with segments") {
    CHECK(trinity::cad::cylinder({0.0, 0.0, 0.0}, 4.0, 10.0, 8).triangleCount() ==
          32);
    CHECK(trinity::cad::cylinder({0.0, 0.0, 0.0}, 4.0, 10.0, 16).triangleCount() ==
          64);
}

TEST_CASE("cylinder bounding box matches radius and height") {
    const trinity::cad::Mesh mesh =
        trinity::cad::cylinder({1.0, 2.0, 3.0}, 4.0, 10.0);
    const trinity::cad::BoundingBox bbox = mesh.boundingBox();
    CHECK(bbox.min[0] == doctest::Approx(-3.0));
    CHECK(bbox.min[1] == doctest::Approx(-2.0));
    CHECK(bbox.min[2] == doctest::Approx(-2.0));
    CHECK(bbox.max[0] == doctest::Approx(5.0));
    CHECK(bbox.max[1] == doctest::Approx(6.0));
    CHECK(bbox.max[2] == doctest::Approx(8.0));
}

TEST_CASE("cylinder has no degenerate or nonfinite triangles") {
    const trinity::cad::Mesh mesh =
        trinity::cad::cylinder({0.0, 0.0, 0.0}, 4.0, 10.0);
    for (const auto& tri : mesh.triangles()) {
        for (const auto& v : tri) {
            for (double c : v) {
                CHECK(std::isfinite(c));
            }
        }
        CHECK(trinity::cad::triangleArea(tri) >= 1e-9);
    }
}

TEST_CASE("cylinder side normals point away from the axis") {
    // Sides are triangles [4*i] and [4*i+1]; centroid radial direction
    // dotted with the face normal must be ~1 for every side quad.
    const trinity::cad::Mesh mesh =
        trinity::cad::cylinder({0.0, 0.0, 5.0}, 4.0, 10.0);
    const auto& tris = mesh.triangles();
    for (int i = 0; i < 32; ++i) {
        for (int k = 0; k < 2; ++k) {
            const auto& tri = tris[static_cast<size_t>(4 * i + k)];
            const trinity::cad::Vec3 n = trinity::cad::triangleNormal(tri);
            const double cx = (tri[0][0] + tri[1][0] + tri[2][0]) / 3.0;
            const double cy = (tri[0][1] + tri[1][1] + tri[2][1]) / 3.0;
            const double r = std::sqrt(cx * cx + cy * cy);
            REQUIRE(r > 0.0);
            const double radial = (n[0] * cx + n[1] * cy) / r;
            CHECK(radial > 0.9);
        }
    }
}

TEST_CASE("cylinder caps face up and down") {
    // Caps are triangles [4*i+2] (top, +Z) and [4*i+3] (bottom, -Z).
    const trinity::cad::Mesh mesh =
        trinity::cad::cylinder({0.0, 0.0, 0.0}, 4.0, 10.0);
    const auto& tris = mesh.triangles();
    for (int i = 0; i < 32; ++i) {
        const trinity::cad::Vec3 up =
            trinity::cad::triangleNormal(tris[static_cast<size_t>(4 * i + 2)]);
        const trinity::cad::Vec3 down =
            trinity::cad::triangleNormal(tris[static_cast<size_t>(4 * i + 3)]);
        CHECK(up[2] > 0.99);
        CHECK(down[2] < -0.99);
    }
}

TEST_CASE("cylinder rejects nonpositive radius") {
    CHECK_THROWS_AS(trinity::cad::cylinder({0.0, 0.0, 0.0}, 0.0, 10.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(trinity::cad::cylinder({0.0, 0.0, 0.0}, -1.0, 10.0),
                    std::invalid_argument);
}

TEST_CASE("cylinder rejects nonpositive height") {
    CHECK_THROWS_AS(trinity::cad::cylinder({0.0, 0.0, 0.0}, 4.0, 0.0),
                    std::invalid_argument);
}

TEST_CASE("cylinder rejects fewer than eight segments") {
    CHECK_THROWS_AS(trinity::cad::cylinder({0.0, 0.0, 0.0}, 4.0, 10.0, 7),
                    std::invalid_argument);
}

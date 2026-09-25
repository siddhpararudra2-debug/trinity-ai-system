#include <doctest.h>

#include <cmath>
#include <limits>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/FrameParams.hpp"
#include "trinity/cad/Mesh.hpp"
#include "trinity/viewer/ArtifactLoader.hpp"
#include "trinity/viewer/Measure.hpp"
#include "trinity/viewer/RenderData.hpp"
#include "trinity/viewer/ViewerState.hpp"

namespace {

trinity::cad::Mesh makeFrame() {
    const auto params =
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
    return trinity::cad::buildQuadcopterFrame(params);
}

}  // namespace

TEST_CASE("viewer: mesh-to-render-data conversion keeps all triangles") {
    const auto mesh = makeFrame();
    REQUIRE(mesh.triangleCount() == 108);
    const auto render = trinity::viewer::buildRenderData(mesh);
    CHECK(render.valid);
    CHECK(render.error.empty());
    CHECK(render.triangleCount() == 108);
    CHECK(render.vertexCount() > 0);
    CHECK(render.positions.size() == render.normals.size());
    CHECK(render.indices.size() == 108 * 3);
    // Normals are unit length (flat shading from triangleNormal).
    for (size_t i = 0; i < render.vertexCount(); ++i) {
        const double nx = render.normals[i * 3 + 0];
        const double ny = render.normals[i * 3 + 1];
        const double nz = render.normals[i * 3 + 2];
        CHECK(std::sqrt(nx * nx + ny * ny + nz * nz) == doctest::Approx(1.0));
    }
}

TEST_CASE("viewer: invalid meshes never claim valid") {
    const trinity::cad::Mesh empty;
    const auto bad = trinity::viewer::buildRenderData(empty);
    CHECK_FALSE(bad.valid);
    CHECK_FALSE(bad.error.empty());

    trinity::cad::Mesh nonFinite;
    nonFinite.addTriangle({{{0, 0, 0},
                            {1, 0, 0},
                            {std::numeric_limits<double>::quiet_NaN(), 0, 0}}});
    const auto nan = trinity::viewer::buildRenderData(nonFinite);
    CHECK_FALSE(nan.valid);

    trinity::cad::Mesh degenerate;
    degenerate.addTriangle({{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}});
    const auto deg = trinity::viewer::buildRenderData(degenerate);
    CHECK_FALSE(deg.valid);
}

TEST_CASE("viewer: bounding box comes from the actual mesh") {
    const auto mesh = makeFrame();
    const auto box = mesh.boundingBox();
    const double expected = std::sqrt(2.0) * (50.0 / 2.0 + 6.0 * 1.4);
    CHECK(box.spanX() == doctest::Approx(expected));
    CHECK(box.spanY() == doctest::Approx(expected));
    CHECK(box.spanZ() == doctest::Approx(1.5 * 1.5));
    CHECK(box.min[0] == doctest::Approx(-box.max[0]));
    CHECK(box.min[1] == doctest::Approx(-box.max[1]));
    // Width/Height/Depth shown in UI are these spans, not hardcoded values.
    CHECK(box.spanX() > 40.0);
    CHECK(box.spanY() > 40.0);
    CHECK(box.spanZ() > 1.0);
}

TEST_CASE("viewer: camera fit targets the model center") {
    trinity::viewer::ViewerState state;
    state.setMeshOnly(makeFrame());
    REQUIRE(state.hasMesh());
    REQUIRE(state.hasBoundingBox());
    const auto& box = *state.boundingBox();
    const auto& cam = state.camera();
    CHECK(cam.target[0] == doctest::Approx((box.min[0] + box.max[0]) / 2.0));
    CHECK(cam.target[1] == doctest::Approx((box.min[1] + box.max[1]) / 2.0));
    CHECK(cam.target[2] == doctest::Approx((box.min[2] + box.max[2]) / 2.0));
    const double dx = cam.position[0] - cam.target[0];
    const double dy = cam.position[1] - cam.target[1];
    const double dz = cam.position[2] - cam.target[2];
    CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) > box.spanX());

    // Reset keeps the model framed even after the camera is disturbed.
    auto disturbed = state.camera();
    disturbed.position = {0.0, 0.0, 5000.0};
    state.camera() = disturbed;
    state.resetCamera();
    CHECK(state.camera().target[0] == doctest::Approx(cam.target[0]));
}

TEST_CASE("viewer: state modes round-trip without the CAD engine") {
    trinity::viewer::ViewerState state;
    CHECK(static_cast<int>(state.projection()) ==
          static_cast<int>(trinity::viewer::ProjectionMode::Perspective));
    CHECK(static_cast<int>(state.renderMode()) ==
          static_cast<int>(trinity::viewer::RenderMode::Solid));
    CHECK(state.gridEnabled());
    CHECK(state.axesEnabled());
    CHECK_FALSE(state.loading());
    CHECK_FALSE(state.hasError());

    state.setProjection(trinity::viewer::ProjectionMode::Orthographic);
    state.setRenderMode(trinity::viewer::RenderMode::Wireframe);
    state.setGrid(false);
    state.setAxes(false);
    state.setLoading(true);
    state.setError("gpu exploded");
    CHECK(static_cast<int>(state.projection()) ==
          static_cast<int>(trinity::viewer::ProjectionMode::Orthographic));
    CHECK(static_cast<int>(state.renderMode()) ==
          static_cast<int>(trinity::viewer::RenderMode::Wireframe));
    CHECK_FALSE(state.gridEnabled());
    CHECK_FALSE(state.axesEnabled());
    CHECK(state.loading());
    CHECK(state.hasError());
    state.clearError();
    state.setLoading(false);
    CHECK_FALSE(state.hasError());
}

TEST_CASE("viewer: measurement uses actual model coordinates") {
    const trinity::cad::Vec3 a{0.0, 0.0, 0.0};
    const trinity::cad::Vec3 b{3.0, 4.0, 12.0};
    CHECK(trinity::viewer::measureDistance(a, b) == doctest::Approx(13.0));
    const auto d = trinity::viewer::measureDelta(a, b);
    CHECK(d[0] == doctest::Approx(3.0));
    CHECK(d[1] == doctest::Approx(4.0));
    CHECK(d[2] == doctest::Approx(12.0));
    CHECK(trinity::viewer::measureDistance(b, b) == doctest::Approx(0.0));
}

TEST_CASE("viewer: robot mesh synthesized from IR FK frames") {
    using trinity::core::Json;
    auto pose = [](double x, double y, double z) {
        return Json{{"position", {{"x", x}, {"y", y}, {"z", z}}},
                    {"rotation",
                     Json::array({Json::array({1.0, 0.0, 0.0}),
                                  Json::array({0.0, 1.0, 0.0}),
                                  Json::array({0.0, 0.0, 1.0})})}};
    };
    const Json result = {
        {"frame_order", Json::array({"world", "base_link", "link_1", "link_2", "tool0"})},
        {"frames",
         {{"world", pose(0.0, 0.0, 0.0)},
          {"base_link", pose(0.0, 0.0, 0.0)},
          {"link_1", pose(0.0, 0.0, 0.0)},
          {"link_2", pose(0.086603, 0.05, 0.0)},
          {"tool0", pose(0.183195, 0.075882, 0.0)}}},
        {"end_effector", pose(0.183195, 0.075882, 0.0)}};
    const auto mesh = trinity::viewer::buildRobotMeshFromResult(result);
    CHECK_FALSE(mesh.empty());
    const auto bbox = mesh.boundingBox();
    CHECK(bbox.max[0] >= 0.18);
    CHECK(bbox.min[0] <= 0.0);
    CHECK(bbox.max[1] >= 0.07);
    // The mesh flows through the same render path as CAD geometry.
    const auto render = trinity::viewer::buildRenderData(mesh);
    CHECK(render.valid);
    CHECK(render.triangleCount() == mesh.triangleCount());
}

TEST_CASE("viewer: robot mesh synthesized from legacy DH frames") {
    using trinity::core::Json;
    const Json result = {
        {"frames",
         Json::array(
             {Json{{"index", 1},
                   {"name", "link_1"},
                   {"position", {{"x", 1.0}, {"y", 0.0}, {"z", 0.0}}}},
              Json{{"index", 2},
                   {"name", "link_2"},
                   {"position", {{"x", 2.0}, {"y", 0.0}, {"z", 0.0}}}}})},
        {"end_effector",
         {{"position", {{"x", 2.0}, {"y", 0.0}, {"z", 0.0}}},
          {"rotation",
           Json::array({Json::array({1.0, 0.0, 0.0}), Json::array({0.0, 1.0, 0.0}),
                        Json::array({0.0, 0.0, 1.0})})}}}};
    const auto mesh = trinity::viewer::buildRobotMeshFromResult(result);
    CHECK_FALSE(mesh.empty());
    const auto bbox = mesh.boundingBox();
    CHECK(bbox.max[0] >= 2.0);
    CHECK(bbox.min[0] <= 0.0);
}

TEST_CASE("viewer: robot mesh synthesis rejects bad input truthfully") {
    using trinity::core::Json;
    CHECK_THROWS(trinity::viewer::buildRobotMeshFromResult(Json::object()));
    CHECK_THROWS(trinity::viewer::buildRobotMeshFromResult(Json{{"frames", Json::object()}}));
    CHECK_THROWS(trinity::viewer::buildRobotMeshFromResult(Json{{"frames", Json::array()}}));
    Json singleFrame = Json::object();
    Json onlyFrame = Json::object();
    onlyFrame["position"] = Json{{"x", 1.0}, {"y", 0.0}, {"z", 0.0}};
    singleFrame["frames"] = Json::object();
    singleFrame["frames"]["only"] = onlyFrame;
    singleFrame["frame_order"] = Json::array({"only"});
    CHECK_THROWS(trinity::viewer::buildRobotMeshFromResult(singleFrame));
}

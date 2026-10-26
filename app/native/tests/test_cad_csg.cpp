#include <doctest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/Csg.hpp"
#include "trinity/cad/Mesh.hpp"
#include "trinity/cad/Parts.hpp"
#include "trinity/cad/StepWriter.hpp"
#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/Engine.hpp"
#include "trinity/core/Error.hpp"

namespace {

trinity::cad::Mesh cube(double offsetX) {
    return trinity::cad::box(trinity::cad::Vec3{offsetX, 0.0, 0.0},
                             trinity::cad::Vec3{10.0, 10.0, 10.0});
}

int occurrencesOf(const std::string& text, const std::string& keyword) {
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(keyword, pos)) != std::string::npos) {
        ++count;
        pos += 1;
    }
    return count;
}

// Naive CARTESIAN_POINT coordinate dump: the round-trip proof that the
// step file carries the mesh's geometry through foreign-tool eyes.
std::vector<std::array<double, 3>> stepCoordinates(const std::string& text) {
    std::vector<std::array<double, 3>> points;
    const std::string marker = "CARTESIAN_POINT('',(";
    size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        std::istringstream stream(text.substr(pos + marker.size()));
        std::array<double, 3> point{};
        char separator = 0;
        stream >> point[0] >> separator >> point[1] >> separator >> point[2];
        points.push_back(point);
        pos += marker.size();
    }
    return points;
}

}  // namespace

// Composite part builders glue overlapping closed shells into one triangle
// soup with Mesh::extend (a standoff is a body cylinder plus a coaxial foot,
// a plate is a box plus four raised bosses). BSP clipping needs operands
// that behave like single solids, so boolean operands are decomposed over
// shared quantized edges and folded pairwise into one watertight shell.
TEST_CASE("composite parts normalize into one watertight shell") {
    const auto* standoffDef = trinity::cad::findPart("standoff");
    const auto* plateDef = trinity::cad::findPart("mounting_plate");
    const auto standoff =
        trinity::cad::unionMeshComponents(standoffDef->builder(
            standoffDef->fromRequest(trinity::core::Json::object())));
    const auto plate =
        trinity::cad::unionMeshComponents(plateDef->builder(
            plateDef->fromRequest(trinity::core::Json::object())));

    CHECK(trinity::cad::isWatertight(standoff));
    CHECK(trinity::cad::isWatertight(plate));

    // Faceted-prism volumes: the builders use 32-facet cylinders, whose
    // cross-section area is 0.5 * 32 * r^2 * sin(2pi/32) (an inscribed
    // polygon, slightly under the analytic circle). The standoff is the
    // r=3 prism (h=10) united with a coaxial r=5 foot (h=1.5) - the foot's
    // top disc inside the body is deduplicated by the union, so the union
    // volume is body + annulus, not body + foot.
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kSegments = 32.0;
    const double facetFactor =
        0.5 * kSegments * std::sin(2.0 * kPi / kSegments);
    const double bodyArea = facetFactor * 9.0;   // r = 3
    const double footArea = facetFactor * 25.0;  // r = 5
    CHECK(trinity::cad::signedVolume(standoff) ==
          doctest::Approx(bodyArea * 10.0 + (footArea - bodyArea) * 1.5)
              .epsilon(0.001));

    // Plate: exact 100 x 60 x 3 box plus four r=4, h=2 bosses standing on
    // its top face.
    const double bossArea = facetFactor * 16.0;  // r = 4
    CHECK(trinity::cad::signedVolume(plate) ==
          doctest::Approx(100.0 * 60.0 * 3.0 + 4.0 * bossArea * 2.0)
              .epsilon(0.001));

    // Subtracting the plate removes everything below its top face and
    // leaves the body prism above it.
    const auto cut = trinity::cad::csgSubtract(standoff, plate);
    CHECK(trinity::cad::isWatertight(cut));
    CHECK(trinity::cad::signedVolume(cut) ==
          doctest::Approx(bodyArea * 7.0).epsilon(0.001));
}

TEST_CASE("csg primitives are watertight with analytic signed volumes") {
    const auto big = cube(0.0);
    CHECK(big.triangleCount() == 12);
    CHECK(trinity::cad::isWatertight(big));
    CHECK(trinity::cad::signedVolume(big) == doctest::Approx(1000.0));

    const auto post = trinity::cad::cylinder(trinity::cad::Vec3{0.0, 0.0, 0.0},
                                             5.0, 10.0, 32);
    CHECK(trinity::cad::isWatertight(post));
    CHECK(trinity::cad::signedVolume(post) ==
          doctest::Approx(3.141592653589793 * 25.0 * 10.0).epsilon(0.02));

    // Builder parts are closed even though they overlap primitives without
    // a boolean union: every triangle keeps a complete shared edge pair.
    const auto frame = trinity::cad::buildQuadcopterFrame(
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object()));
    CHECK(trinity::cad::isWatertight(frame));
    CHECK(trinity::cad::signedVolume(frame) > 0.0);

    // Orientation awareness: flipping triangles negates the divergence sum.
    trinity::cad::Mesh flipped;
    for (const auto& tri : big.triangles()) {
        flipped.addTriangle(trinity::cad::Triangle{tri[0], tri[2], tri[1]});
    }
    CHECK(trinity::cad::signedVolume(flipped) ==
          doctest::Approx(-1000.0).epsilon(1e-9));
}

TEST_CASE("boolean subtract/intersect/union hit analytic volumes") {
    // Two half-overlapping cubes: overlap is 5 x 10 x 10 = 500 mm^3.
    const auto a = cube(0.0);
    const auto b = cube(5.0);
    const auto cut = trinity::cad::csgSubtract(a, b);
    CHECK(cut.triangleCount() > 0);
    CHECK(trinity::cad::isWatertight(cut));
    CHECK(trinity::cad::signedVolume(cut) == doctest::Approx(500.0));

    const auto both = trinity::cad::csgIntersect(a, b);
    CHECK(trinity::cad::isWatertight(both));
    CHECK(trinity::cad::signedVolume(both) == doctest::Approx(500.0));

    const auto joined = trinity::cad::csgUnion(a, b);
    CHECK(trinity::cad::isWatertight(joined));
    CHECK(trinity::cad::signedVolume(joined) == doctest::Approx(1500.0));

    // Disjoint operands: union adds, subtract leaves nothing to cut,
    // intersect is the honest empty result.
    const auto far = cube(20.0);
    CHECK(trinity::cad::signedVolume(trinity::cad::csgUnion(a, far)) ==
          doctest::Approx(2000.0));
    const auto untouched = trinity::cad::csgSubtract(a, far);
    CHECK(trinity::cad::isWatertight(untouched));
    CHECK(trinity::cad::signedVolume(untouched) == doctest::Approx(1000.0));

    const auto empty = trinity::cad::csgIntersect(a, far);
    CHECK(empty.empty());

    // A hollow cavity: subtract an inner cube and the divergence sum is
    // exactly the remaining material.
    const auto cavity = cube(0.0);
    trinity::cad::Mesh inner =
        trinity::cad::box(trinity::cad::Vec3{0.0, 0.0, 0.0},
                          trinity::cad::Vec3{6.0, 6.0, 6.0});
    const auto hollow = trinity::cad::csgSubtract(cavity, inner);
    CHECK(trinity::cad::isWatertight(hollow));
    CHECK(trinity::cad::signedVolume(hollow) ==
          doctest::Approx(1000.0 - 216.0));

    // Empty operands get identity semantics, not an exception.
    trinity::cad::Mesh nothing;
    CHECK(trinity::cad::csgSubtract(a, nothing).triangleCount() == 12);
    CHECK(trinity::cad::csgUnion(nothing, b).triangleCount() == 12);
    CHECK(trinity::cad::csgIntersect(nothing, b).empty());
}

TEST_CASE("step export writes an ISO-10303-21 faceted brep") {
    const auto box = cube(0.0);
    const std::string step =
        trinity::cad::encodeStep(box, "test_box");

    CHECK(step.compare(0, 13, "ISO-10303-21;") == 0);
    CHECK(step.find("FILE_SCHEMA((AUTOMOTIVE_DESIGN") != std::string::npos);
    CHECK(step.find("SI_UNIT(.MILLI.,.METRE.)") != std::string::npos);
    CHECK(step.find("END-ISO-10303-21;") + 18 == step.size());

    // Entity counts: 8 unique cube corners (+ one global origin point),
    // one face per triangle, one shell, one faceted brep.
    CHECK(occurrencesOf(step, "CARTESIAN_POINT(") == 9);
    CHECK(occurrencesOf(step, "VERTEX_POINT(") == 8);
    CHECK(occurrencesOf(step, "POLY_LOOP(") == 12);
    CHECK(occurrencesOf(step, "FACE_SURFACE(") == 12);
    CHECK(occurrencesOf(step, "CLOSED_SHELL(") == 1);
    CHECK(occurrencesOf(step, "FACETED_BREP(") == 1);
    CHECK(occurrencesOf(step, "ADVANCED_BREP_SHAPE_REPRESENTATION(") == 1);
    CHECK(occurrencesOf(step, "SHAPE_DEFINITION_REPRESENTATION(") == 1);

    // Coordinate round trip: every step coordinate lands inside (and spans
    // the corners of) the source mesh's bounding box.
    const auto coords = stepCoordinates(step);
    CHECK(coords.size() >= 8);
    const auto bbox = box.boundingBox();
    std::array<double, 3> min{0.0, 0.0, 0.0};
    std::array<double, 3> max{0.0, 0.0, 0.0};
    for (const auto& point : coords) {
        for (int axis = 0; axis < 3; ++axis) {
            min[static_cast<size_t>(axis)] =
                std::min(min[static_cast<size_t>(axis)],
                         point[static_cast<size_t>(axis)]);
            max[static_cast<size_t>(axis)] =
                std::max(max[static_cast<size_t>(axis)],
                         point[static_cast<size_t>(axis)]);
        }
    }
    CHECK(min[0] == doctest::Approx(bbox.min[0]).epsilon(1e-6));
    CHECK(min[1] == doctest::Approx(bbox.min[1]).epsilon(1e-6));
    CHECK(min[2] == doctest::Approx(bbox.min[2]).epsilon(1e-6));
    CHECK(max[0] == doctest::Approx(bbox.max[0]).epsilon(1e-6));
    CHECK(max[1] == doctest::Approx(bbox.max[1]).epsilon(1e-6));
    CHECK(max[2] == doctest::Approx(bbox.max[2]).epsilon(1e-6));

    trinity::cad::Mesh nothing;
    CHECK_THROWS_AS(trinity::cad::encodeStep(nothing, "x"), std::runtime_error);
}

TEST_CASE("engine boolean runs watertight parts with structured checks") {
    trinity::engines::CadEngine engine;

    trinity::engines::EngineRequest req;
    req.engine = "cad";
    req.operation = "boolean";
    req.parameters = {
        {"mode", "subtract"},
        {"a", {{"type", "standoff"}}},
        {"b", {{"type", "mounting_plate"}}}};
    const auto result = engine.execute(req);
    CHECK(result.success);
    CHECK(result.result.value("outcome", "") == "solid");
    CHECK(result.result.value("triangle_count", 0) > 0);
    const double volumeA = result.result["volumes"].value("a", 0.0);
    const double volumeR = result.result["volumes"].value("result", 0.0);
    CHECK(volumeR <= volumeA + 1e-6 * (1.0 + std::fabs(volumeA)));
    // Volume truth: the survivors are the 32-facet r=3 prism above the
    // plate's top face (height 7), pre-union normalization minus cap.
    constexpr double kPi = 3.14159265358979323846;
    const double facetFactor = 16.0 * std::sin(2.0 * kPi / 32.0);
    CHECK(volumeR == doctest::Approx(facetFactor * 9.0 * 7.0).epsilon(0.001));
    REQUIRE(result.metadata.contains("cad_checks"));
    CHECK(result.metadata["cad_checks"].value("result_watertight", false));
    CHECK(result.metadata["cad_checks"].value("result_finite", false));
    REQUIRE(result.validation.has_value());
    CHECK(result.validation->passed());

    // A structured refusal: unknown mode, missing operands, unknown part
    // throw out of execute() with the engine's typed error surface.
    trinity::engines::EngineRequest badMode;
    badMode.engine = "cad";
    badMode.operation = "boolean";
    badMode.parameters = {
        {"mode", "blend"},
        {"a", {{"type", "standoff"}}},
        {"b", {{"type", "standoff"}}}};
    CHECK_THROWS_AS(engine.execute(badMode), trinity::core::RequestValidationError);

    trinity::engines::EngineRequest missing;
    missing.engine = "cad";
    missing.operation = "boolean";
    missing.parameters = {{"mode", "subtract"},
                          {"a", {{"type", "standoff"}}}};
    CHECK_THROWS_AS(engine.execute(missing), trinity::core::RequestValidationError);

    trinity::engines::EngineRequest unknownPart;
    unknownPart.engine = "cad";
    unknownPart.operation = "boolean";
    unknownPart.parameters = {{"mode", "subtract"},
                              {"a", {{"type", "warp_drive"}}},
                              {"b", {{"type", "standoff"}}}};
    CHECK_THROWS_AS(engine.execute(unknownPart),
                    trinity::core::RequestValidationError);

    // The describe contract carries the new operation and version.
    trinity::engines::EngineRequest describe;
    describe.engine = "cad";
    describe.operation = "describe";
    const auto described = engine.execute(describe);
    CHECK(described.success);
    bool hasBoolean = false;
    for (const auto& cap : described.result["capabilities"]) {
        if (cap.get<std::string>() == "boolean") {
            hasBoolean = true;
        }
    }
    CHECK(hasBoolean);
    CHECK(described.result.value("version", "") == "0.4.0");
}

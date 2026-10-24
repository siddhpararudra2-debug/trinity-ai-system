#include <doctest.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
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

// TEMPORARY M4 DEBUG: dump unpaired edges of failing CSG results.
#include <map>
TEST_CASE("DEBUG unpaired edges") {
    // Composite operands exactly like the engine test: standoff over the
    // plate, on the bench zero-plane.
    const auto* defA = trinity::cad::findPart("standoff");
    const auto* defB = trinity::cad::findPart("mounting_plate");
    const auto standoff = defA->builder(defA->fromRequest(trinity::core::Json::object()));
    const auto plate = defB->builder(defB->fromRequest(trinity::core::Json::object()));
    std::cout << "standoff tris=" << standoff.triangleCount()
              << " watertight=" << trinity::cad::isWatertight(standoff) << "\n";
    std::cout << "plate tris=" << plate.triangleCount()
              << " watertight=" << trinity::cad::isWatertight(plate) << "\n";
    const auto cut = trinity::cad::csgSubtract(standoff, plate);
    std::cout << "cut tris=" << cut.triangleCount()
              << " watertight=" << trinity::cad::isWatertight(cut)
              << " vol=" << trinity::cad::signedVolume(cut) << "\n";
    {
        std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>,
                 int>
            edgeCount;
        for (const auto& tri : cut.triangles()) {
            std::array<std::array<long long, 3>, 3> q{};
            for (size_t i = 0; i < 3; ++i) {
                for (size_t axis = 0; axis < 3; ++axis) {
                    q[i][axis] = std::llround(tri[i][axis] * 1e6);
                }
            }
            for (size_t i = 0; i < 3; ++i) {
                auto key = std::make_pair(q[i], q[(i + 1) % 3]);
                auto rev = std::make_pair(q[(i + 1) % 3], q[i]);
                if (rev < key) {
                    std::swap(key, rev);
                }
                ++edgeCount[key];
            }
        }
        std::ofstream dump("csg_dedges.txt");
        int unpaired = 0;
        for (const auto& entry : edgeCount) {
            if (entry.second != 2) {
                ++unpaired;
                dump << "use=" << entry.second << " ["
                     << entry.first.first[0] << "," << entry.first.first[1]
                     << "," << entry.first.first[2] << "] -> ["
                     << entry.first.second[0] << "," << entry.first.second[1]
                     << "," << entry.first.second[2] << "]\n";
            }
        }
        dump << "total distinct=" << edgeCount.size()
             << " use!=2=" << unpaired << "\n";
    }
    // Triangles touching the rim p0..q..p1 chain.
    struct NoProbe {};
    const std::vector<std::array<long long, 3>> probes = {
        {-3000000, 0, 3000000},          // p0  (facet vertex at 180 deg)
        {-2942356, -585271, 3000000},    // p1  (facet vertex at ~186 deg)
        {-2982707, -175581, 3000000}};   // q   (cap-side split at t=0.3)
    int triId = 0;
    for (const auto& tri : cut.triangles()) {
        int touched = 0;
        for (const auto& want : probes) {
            for (int i = 0; i < 3; ++i) {
                std::array<long long, 3> key{};
                for (int axis = 0; axis < 3; ++axis) {
                    key[axis] = std::llround(tri[i][axis] * 1e6);
                }
                if (key == want) {
                    ++touched;
                }
            }
        }
        if (touched == 0) {
            continue;
        }
        std::ofstream out;
        out.open("csg_dedges.txt", std::ios::app);
        out << "tri" << ++triId << " ("
            << (touched == 1 ? "one probe" : "two probes") << "): "
            << "(" << tri[0][0] << "," << tri[0][1] << "," << tri[0][2] << ") "
            << "(" << tri[1][0] << "," << tri[1][1] << "," << tri[1][2] << ") "
            << "(" << tri[2][0] << "," << tri[2][1] << "," << tri[2][2]
            << ")\n";
    }
    // Edge incidence around the rim probes.
    {
        std::map<std::pair<std::array<long long, 3>, std::array<long long, 3>>,
                 int>
            edges;
        for (const auto& tri : cut.triangles()) {
            std::array<std::array<long long, 3>, 3> q3{};
            for (size_t i = 0; i < 3; ++i) {
                for (size_t axis = 0; axis < 3; ++axis) {
                    q3[i][axis] = std::llround(tri[i][axis] * 1e6);
                }
            }
            for (size_t i = 0; i < 3; ++i) {
                auto key = std::make_pair(q3[i], q3[(i + 1) % 3]);
                auto rev = std::make_pair(q3[(i + 1) % 3], q3[i]);
                if (rev < key) {
                    std::swap(key, rev);
                }
                ++edges[key];
            }
        }
        std::ofstream out;
        out.open("csg_dedges.txt", std::ios::app);
        for (const auto& want : probes) {
            out << "--- incident edges of probe\n";
            for (const auto& e : edges) {
                if (e.first.first == want || e.first.second == want) {
                    out << "  use=" << e.second << " ["
                        << e.first.first[0] << "," << e.first.first[1] << ","
                        << e.first.first[2] << "] -> ["
                        << e.first.second[0] << "," << e.first.second[1] << ","
                        << e.first.second[2] << "]\n";
                }
            }
        }
    }
    CHECK(true);
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

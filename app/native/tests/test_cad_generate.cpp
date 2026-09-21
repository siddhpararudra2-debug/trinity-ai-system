#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/cad/Builder.hpp"
#include "trinity/cad/FrameParams.hpp"
#include "trinity/cad/StlWriter.hpp"
#include "trinity/cad/Validators.hpp"
#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Database.hpp"

TEST_CASE("cad defaults build a 108-triangle frame that validates") {
    const trinity::cad::FrameParams params =
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
    CHECK(params.parameters.at("overall_size") == 50.0);
    const trinity::cad::Mesh mesh = trinity::cad::buildQuadcopterFrame(params);
    CHECK(mesh.triangleCount() == 108);
    const auto validation = trinity::cad::validateQuadcopterFrame(params, mesh);
    CHECK(validation.ok);
    CHECK(validation.checks.value("dimensions_within_tolerance", false));
    CHECK(validation.checks.value("all_vertices_finite", false));
    CHECK(validation.checks.value("degenerate_triangle_count", -1) == 0);
    CHECK(validation.checks.value("arm_length_positive", false));
    CHECK(validation.checks.value("manufacturable_min_feature", false));
}

TEST_CASE("cad rejects bad parameters with structured errors") {
    CHECK_THROWS_AS(trinity::cad::FrameParams::fromRequest(
                        trinity::core::Json{{"overall_size", 20}, {"center_plate_size", 26}}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(
        trinity::cad::FrameParams::fromRequest(trinity::core::Json{{"warp_drive", 1}}),
        trinity::core::RequestValidationError);
    CHECK_THROWS_AS(
        trinity::cad::FrameParams::fromRequest(trinity::core::Json{{"motor_count", 6}}),
        trinity::core::RequestValidationError);

    trinity::engines::CadEngine cad;
    trinity::engines::EngineRequest bad;
    bad.engine = "cad";
    bad.operation = "generate";
    bad.parameters = {{"type", "starship"}};
    CHECK_THROWS_AS(cad.execute(bad), trinity::core::RequestValidationError);
}

TEST_CASE("cad generate produces stl+json artifacts through jobs") {
    namespace fs = std::filesystem;
    const std::string dir =
        (fs::temp_directory_path() / "trinity-test-cad-gen").string();
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir + "/artifacts", ec);
    {
        auto db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        auto registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<trinity::engines::CadEngine>());
        auto artifacts =
            std::make_shared<trinity::artifacts::ArtifactManager>(db, dir + "/artifacts");
        trinity::jobs::JobManager jobs(db, registry, artifacts);

        const trinity::core::Json response = jobs.runSync(
            "cad", "generate",
            {{"type", "quadcopter_frame"},
             {"parameters", {{"overall_size", 50}, {"motor_count", 4}}},
             {"outputs", {"stl", "json"}}});
        CHECK(response.value("success", false));
        CHECK(response["validation"].value("status", "") == "VALIDATED");
        CHECK(response["result"].value("triangle_count", 0) == 108);
        REQUIRE(response["artifacts"].is_array());
        CHECK(response["artifacts"].size() == 2);

        std::vector<std::string> types;
        for (const auto& artifact : response["artifacts"]) {
            types.push_back(artifact.value("type", ""));
            CHECK(artifact.value("size_bytes", 0LL) > 0);
            CHECK(fs::is_regular_file(artifact.value("path", ""), ec));
        }
        CHECK(std::find(types.begin(), types.end(), "stl") != types.end());
        CHECK(std::find(types.begin(), types.end(), "json") != types.end());

        // STL header starts with our name; size matches 84 + 50 * triangles.
        for (const auto& artifact : response["artifacts"]) {
            if (artifact.value("type", "") == "stl") {
                std::ifstream stl(artifact.value("path", ""), std::ios::binary);
                char header[6] = {0};
                stl.read(header, 5);
                CHECK(std::string(header) == "trini");
                stl.close();
                CHECK(artifact.value("size_bytes", 0LL) == 84 + 50 * 108);
            }
        }

        const std::string jobId = response.value("job_id", "");
        CHECK_FALSE(jobId.empty());
        CHECK(trinity::jobs::toString(jobs.get(jobId).status) == "completed");
    }
    fs::remove_all(dir, ec);
}

TEST_CASE("cad generate reports step as unavailable, not failure") {
    trinity::engines::CadEngine cad;
    trinity::engines::EngineRequest req;
    req.engine = "cad";
    req.operation = "generate";
    req.parameters = {{"type", "quadcopter_frame"},
                      {"parameters", {{"overall_size", 50}}},
                      {"outputs", {"stl", "step"}}};
    const auto result = cad.execute(req);
    CHECK(result.success);
    CHECK(result.result.contains("unavailable_formats"));
    CHECK(result.result["unavailable_formats"].value("step", "").find("CAD_KERNEL_UNAVAILABLE") ==
          0);
}

TEST_CASE("cad stl encoding is deterministic") {
    const trinity::cad::FrameParams params =
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
    const trinity::cad::Mesh first = trinity::cad::buildQuadcopterFrame(params);
    const trinity::cad::Mesh second = trinity::cad::buildQuadcopterFrame(params);
    CHECK(trinity::cad::encodeBinaryStl(first) == trinity::cad::encodeBinaryStl(second));
}

TEST_CASE("golden: standard 50 mm frame produces stable reference geometry") {
    const trinity::cad::FrameParams params =
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
    REQUIRE(params.parameters.at("overall_size") == 50.0);
    REQUIRE(params.parameters.at("motor_count") == 4.0);
    const trinity::cad::Mesh mesh = trinity::cad::buildQuadcopterFrame(params);

    // Reference metadata: 9 boxes (plate + 4 arms + 4 bosses) x 12 tris.
    CHECK(mesh.triangleCount() == 108);
    CHECK(mesh.triangles().size() * 3 == 324);

    const trinity::cad::BoundingBox bbox = mesh.boundingBox();
    // Centered symmetric frame: min mirrors max on every axis.
    CHECK(bbox.min[0] == doctest::Approx(-bbox.max[0]));
    CHECK(bbox.min[1] == doctest::Approx(-bbox.max[1]));
    CHECK(bbox.min[2] == doctest::Approx(-bbox.max[2]));
    CHECK(bbox.spanX() == doctest::Approx(bbox.spanY()));
    // Z span is driven by the raised motor bosses (1.5x plate thickness).
    CHECK(bbox.spanZ() == doctest::Approx(1.5 * 1.5));
    // X/Y span matches the projected diagonal footprint formula.
    const double expected = std::sqrt(2.0) * (50.0 / 2.0 + 6.0 * 1.4);
    CHECK(bbox.spanX() == doctest::Approx(expected));
    CHECK(bbox.spanY() == doctest::Approx(expected));

    // STL bytes are stable: header + count + fixed 50-byte records.
    const auto bytes = trinity::cad::encodeBinaryStl(mesh);
    CHECK(bytes.size() == 84 + 50 * 108);
    CHECK(trinity::cad::encodeBinaryStl(trinity::cad::buildQuadcopterFrame(params)) ==
          bytes);
}

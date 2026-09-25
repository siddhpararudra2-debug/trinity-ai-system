#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "trinity/engines/SimulationEngine.hpp"
#include "trinity/simulation/Exports.hpp"
#include "trinity/simulation/Integrator.hpp"

namespace fs = std::filesystem;
using trinity::engines::EngineRequest;
using trinity::engines::SimulationEngine;
using trinity::simulation::integrate;
using trinity::simulation::SimulationProject;
using trinity::simulation::writeCsv;
using trinity::simulation::writeJson;

namespace {

SimulationProject makeProject() {
    SimulationProject p;
    p.projectId = "export-test";
    p.name = "export";
    p.type = "kinematics";
    p.model = "linear_motion";
    p.dt = 0.05;
    p.durationS = 0.5;
    p.initial.velocity.x = 3.0;
    p.initial.acceleration.x = 1.0;
    return p;
}

std::string readAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("writeCsv emits header and one row per sample") {
    const auto project = makeProject();
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    const fs::path dir = fs::temp_directory_path() / "trinity-sim-export-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "out.csv").string();
    writeCsv(path, outcome.result);
    const std::string content = readAll(path);
    CHECK(content.find(
              "time_s,position_x,position_y,position_z,velocity_x,velocity_y,velocity_z,"
              "acceleration_x,acceleration_y,acceleration_z") == 0);
    // Header + one line per sample (11 samples for 0.5/0.05).
    size_t lines = 0;
    for (char c : content) {
        if (c == '\n') ++lines;
    }
    CHECK(lines == outcome.result.samples.size() + 1);
    CHECK(content.find("0,") != std::string::npos);
    CHECK(content.find("3,") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("writeJson embeds project, result, and validation") {
    const auto project = makeProject();
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    const fs::path dir = fs::temp_directory_path() / "trinity-sim-export-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "out.json").string();
    const trinity::core::Json validation = {{"ok", true}, {"message", "checks passed"}};
    writeJson(path, project, outcome.result, validation);
    const std::string content = readAll(path);
    CHECK(content.find("\"project\"") != std::string::npos);
    CHECK(content.find("\"result\"") != std::string::npos);
    CHECK(content.find("\"validation\"") != std::string::npos);
    CHECK(content.find("\"artifacts\"") != std::string::npos);
    CHECK(content.find("export-test") != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("engine export_results attaches CSV and JSON pending artifacts") {
    SimulationEngine engine;
    const auto project = makeProject();
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);

    EngineRequest request;
    request.engine = "simulation";
    request.operation = "export_results";
    request.parameters = {{"result", outcome.result.toJson()},
                          {"project", project.toJson()}};
    const auto result = engine.execute(request);
    REQUIRE(result.success);
    REQUIRE(result.pendingArtifacts.size() == 2);
    int csvCount = 0;
    int jsonCount = 0;
    for (const auto& [path, type] : result.pendingArtifacts) {
        CHECK(fs::exists(path));
        CHECK(fs::file_size(path) > 0);
        if (type == "csv") ++csvCount;
        if (type == "json") ++jsonCount;
    }
    CHECK(csvCount == 1);
    CHECK(jsonCount == 1);
    for (const auto& [path, type] : result.pendingArtifacts) {
        std::error_code ec;
        fs::remove_all(fs::path(path).parent_path(), ec);
    }
}

TEST_CASE("engine run_simulation with write_artifacts true produces both files") {
    SimulationEngine engine;
    EngineRequest request;
    request.engine = "simulation";
    request.operation = "simulate_linear_motion";
    request.parameters = {{"duration_s", 1.0},
                          {"dt", 0.01},
                          {"initial_velocity_m_s", 2.0},
                          {"acceleration_m_s2", 1.0},
                          {"write_artifacts", true}};
    const auto result = engine.execute(request);
    REQUIRE(result.success);
    REQUIRE(result.pendingArtifacts.size() == 2);
    for (const auto& [path, type] : result.pendingArtifacts) {
        CHECK(fs::exists(path));
        CHECK(fs::file_size(path) > 0);
    }
    std::error_code ec;
    fs::remove_all(fs::path(result.pendingArtifacts.front().first).parent_path(), ec);
}

TEST_CASE("engine run_simulation with write_artifacts false has no pending files") {
    SimulationEngine engine;
    EngineRequest request;
    request.engine = "simulation";
    request.operation = "simulate_linear_motion";
    request.parameters = {{"duration_s", 0.5},
                          {"dt", 0.01},
                          {"initial_velocity_m_s", 2.0},
                          {"write_artifacts", false}};
    const auto result = engine.execute(request);
    REQUIRE(result.success);
    CHECK(result.pendingArtifacts.empty());
}

TEST_CASE("writeJson emits spec export keys and bounded sample preview") {
    auto project = makeProject();
    project.parameters = {{"dt", 0.05, "s"}, {"duration_s", 0.5, "s"}};
    const auto outcome = integrate(project);
    REQUIRE(outcome.success);
    const fs::path dir = fs::temp_directory_path() / "trinity-sim-export-keys";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "out.json").string();

    const trinity::core::Json validation = {
        {"ok", true},
        {"rules", trinity::core::Json::array({{{"rule", "project.time_step_within_duration"},
                                               {"passed", true},
                                               {"message", "dt within duration"}}})}};
    const trinity::core::Json refs = trinity::core::Json::array(
        {{{"artifact_id", "art-1"}, {"type", "csv"}, {"path", "a.csv"}}});
    writeJson(path, project, outcome.result, validation, refs);

    const trinity::core::Json doc = trinity::core::Json::parse(readAll(path));

    REQUIRE(doc.contains("parameters"));
    REQUIRE(doc["parameters"].is_array());
    REQUIRE(doc["parameters"].size() == 2);
    CHECK(doc["parameters"][0]["key"] == "dt");
    CHECK(doc["parameters"][0]["value"].get<double>() == doctest::Approx(0.05));

    REQUIRE(doc.contains("initial_conditions"));
    CHECK(doc["initial_conditions"]["velocity"]["x"] == doctest::Approx(3.0));
    CHECK(doc["initial_conditions"]["acceleration"]["x"] == doctest::Approx(1.0));

    REQUIRE(doc.contains("integration_method"));
    CHECK(doc["integration_method"] == "closed_form");

    REQUIRE(doc.contains("time_step_s"));
    CHECK(doc["time_step_s"].get<double>() == doctest::Approx(0.05));
    REQUIRE(doc.contains("duration_s"));
    CHECK(doc["duration_s"].get<double>() == doctest::Approx(0.5));

    REQUIRE(doc.contains("result"));
    CHECK(doc["result"]["method"] == "closed_form");
    REQUIRE(doc.contains("result_metadata"));
    CHECK(doc["result_metadata"].contains("sample_count"));
    CHECK(doc["result_metadata"]["sample_count"] == outcome.result.samples.size());

    REQUIRE(doc.contains("validation"));
    CHECK(doc["validation"]["ok"] == true);
    REQUIRE(doc["validation"].contains("rules"));

    REQUIRE(doc.contains("artifacts"));
    REQUIRE(doc.contains("artifact_references"));
    REQUIRE(doc["artifact_references"].is_array());
    REQUIRE(doc["artifact_references"].size() == 1);
    CHECK(doc["artifact_references"][0]["artifact_id"] == "art-1");

    // Large time series live in the CSV, not the JSON: the embedded
    // preview is a bounded, strided window of the full series.
    REQUIRE(doc.contains("sample_preview"));
    REQUIRE(doc["sample_preview"].is_array());
    CHECK(doc["sample_preview"].size() <= 500);
    CHECK(doc["sample_preview"].size() <= outcome.result.samples.size());
    REQUIRE(doc["sample_preview"].size() >= 2);
    CHECK(doc["sample_preview"].front().contains("time_s"));

    fs::remove_all(dir);
}

TEST_CASE("writeJson sample preview is bounded for long runs") {
    SimulationProject p = makeProject();
    p.dt = 0.001;
    p.durationS = 10.0;  // 10001 samples
    const auto outcome = integrate(p);
    REQUIRE(outcome.success);
    REQUIRE(outcome.result.samples.size() > 500);

    const fs::path dir = fs::temp_directory_path() / "trinity-sim-export-long";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "out.json").string();
    writeJson(path, p, outcome.result, trinity::core::Json::object());

    const trinity::core::Json doc = trinity::core::Json::parse(readAll(path));
    REQUIRE(doc.contains("sample_preview"));
    const size_t preview = doc["sample_preview"].size();
    CHECK(preview <= 500);
    CHECK(preview >= 2);
    // Full sample count still reported in metadata for honesty.
    CHECK(doc["result_metadata"]["sample_count"] == outcome.result.samples.size());

    fs::remove_all(dir);
}

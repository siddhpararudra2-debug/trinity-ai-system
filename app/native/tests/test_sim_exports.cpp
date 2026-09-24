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

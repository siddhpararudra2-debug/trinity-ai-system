#include <doctest.h>

#include <algorithm>
#include <filesystem>
#include <memory>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/artifacts/Checksum.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/SimulationEngine.hpp"
#include "trinity/engines/StubEngines.hpp"
#include "trinity/intelligence/RequestPipeline.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Database.hpp"

namespace {

struct Fixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;
    std::shared_ptr<trinity::intelligence::RequestPipeline> pipeline;

    Fixture() {
        dir = (std::filesystem::temp_directory_path() / "trinity-test-sim-pipeline").string();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        trinity::engines::registerAllEngines(*registry);
        artifacts = std::make_shared<trinity::artifacts::ArtifactManager>(db, dir + "/artifacts");
        jobs = std::make_shared<trinity::jobs::JobManager>(db, registry, artifacts);
        pipeline = std::make_shared<trinity::intelligence::RequestPipeline>(jobs, registry);
    }
    ~Fixture() {
        pipeline.reset();
        jobs.reset();
        artifacts.reset();
        registry.reset();
        db.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("pipeline routes and runs a full simulation request end to end") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Simulate linear motion for 2 seconds with initial velocity 5 m/s and acceleration 2 m/s^2");
    CHECK(out.success);
    CHECK(out.routing.routed);
    CHECK(out.routing.engine == "simulation");
    CHECK(out.routing.operation == "simulate_linear_motion");
    CHECK_FALSE(out.jobId.empty());
    CHECK(out.engineEnvelope["success"] == true);
    const auto& result = out.engineEnvelope["result"];
    REQUIRE(result.contains("result"));
    CHECK(result["result"]["method"] == "closed_form");
    CHECK(result["result"]["step_count"] == 200);
    CHECK(result["checks"]["ok"] == true);
    auto job = fx.jobs->get(out.jobId);
    CHECK(trinity::jobs::toString(job.status) == "completed");
    CHECK_FALSE(job.startedAt.empty());
    CHECK_FALSE(job.completedAt.empty());
}

TEST_CASE("pipeline routes projectile simulation when projectile is mentioned") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Simulate a projectile for 3 seconds with initial velocity 20 m/s at 45 degrees");
    CHECK(out.success);
    CHECK(out.routing.operation == "simulate_projectile");
    CHECK(out.engineEnvelope["success"] == true);
    const auto& result = out.engineEnvelope["result"];
    REQUIRE(result.contains("result"));
    CHECK(result["result"]["method"] == "closed_form");
    CHECK(result["checks"]["ok"] == true);
}

TEST_CASE("pipeline routes dynamics when force and mass are present") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Simulate dynamics for 1 second with mass 1000 g and force 10 N");
    CHECK(out.success);
    CHECK(out.routing.operation == "simulate_dynamics");
    CHECK(out.engineEnvelope["success"] == true);
    const auto& result = out.engineEnvelope["result"];
    REQUIRE(result.contains("result"));
    CHECK(result["result"]["method"] == "semi_implicit_euler");
    CHECK(result["checks"]["ok"] == true);
}

TEST_CASE("incomplete simulation request is rejected without creating a job") {
    Fixture fx;
    // Projectile without duration or velocity must not invent defaults.
    auto out = fx.pipeline->executeSync("Simulate a projectile");
    CHECK_FALSE(out.success);
    CHECK(out.jobId.empty());
    CHECK(fx.jobs->listRecent(10).empty());
}

TEST_CASE("explicit simulation engine keyword routes through simulation") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Using simulation engine: linear motion for 1 second with velocity 3 m/s");
    // Either routed to simulation or rejected as incomplete; never to math.
    if (out.routing.routed) {
        CHECK(out.routing.engine == "simulation");
    } else {
        CHECK(out.jobId.empty());
    }
}

TEST_CASE("spec request: 1 kg object under 10 N for 5 seconds runs dynamics") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Simulate a 1 kg object under 10 N of force for 5 seconds");
    REQUIRE(out.success);
    CHECK(out.routing.routed);
    CHECK(out.routing.engine == "simulation");
    CHECK(out.routing.operation == "simulate_dynamics");
    const auto& result = out.engineEnvelope["result"];
    REQUIRE(result.contains("project"));
    REQUIRE(result.contains("result"));
    CHECK(result["project"]["mass_kg"] == doctest::Approx(1.0));
    CHECK(result["project"]["force_N"]["x"] == doctest::Approx(10.0));
    CHECK(result["project"]["duration_s"] == doctest::Approx(5.0));
    CHECK(result["result"]["method"] == "semi_implicit_euler");
    // a = F/m = 10 m/s^2; semi-implicit Euler velocity is exact for
    // constant acceleration: v = a * t = 10 * 5 = 50 m/s.
    CHECK(result["result"]["final_state"]["velocity"]["x"] == doctest::Approx(50.0));
    CHECK(result["checks"]["ok"] == true);
    auto job = fx.jobs->get(out.jobId);
    CHECK(trinity::jobs::toString(job.status) == "completed");
}

TEST_CASE("spec request: 5 m/s for 10 seconds runs linear motion") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Simulate an object moving with 5 m/s velocity for 10 seconds");
    REQUIRE(out.success);
    CHECK(out.routing.operation == "simulate_linear_motion");
    const auto& result = out.engineEnvelope["result"];
    REQUIRE(result.contains("result"));
    CHECK(result["result"]["method"] == "closed_form");
    CHECK(result["result"]["final_state"]["position"]["x"] == doctest::Approx(50.0));
    CHECK(result["result"]["final_state"]["velocity"]["x"] == doctest::Approx(5.0));
    CHECK(result["checks"]["ok"] == true);
}

TEST_CASE("spec request: projectile with velocity but no duration reports missing") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Simulate projectile motion with initial velocity 20 m/s");
    CHECK_FALSE(out.success);
    CHECK(out.jobId.empty());
    CHECK(fx.jobs->listRecent(10).empty());
    CHECK(trinity::intelligence::toString(out.intent.status) == "INCOMPLETE");
    const auto& missing = out.intent.missing;
    const bool hasDuration =
        std::find(missing.begin(), missing.end(), "duration_s") != missing.end();
    CHECK(hasDuration);
    // Explicit input was extracted, never invented away.
    CHECK(out.intent.parameters.contains("initial_velocity_m_s"));
    CHECK(out.intent.parameters["initial_velocity_m_s"] == doctest::Approx(20.0));
}

TEST_CASE("spec request: 3D motion with 0.01 s time step extracts dt and reports missing") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Run a 3D motion simulation with 0.01 second time step");
    CHECK_FALSE(out.success);
    CHECK(out.jobId.empty());
    CHECK(trinity::intelligence::toString(out.intent.status) == "INCOMPLETE");
    const auto& missing = out.intent.missing;
    const bool hasDuration =
        std::find(missing.begin(), missing.end(), "duration_s") != missing.end();
    CHECK(hasDuration);
    REQUIRE(out.intent.parameters.contains("dt_s"));
    CHECK(out.intent.parameters["dt_s"] == doctest::Approx(0.01));
}

TEST_CASE("simulation job persists artifacts with SHA-256 and validations in SQLite") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Simulate linear motion for 1 second with initial velocity 5 m/s");
    REQUIRE(out.success);
    const auto& artifacts = out.engineEnvelope["artifacts"];
    REQUIRE(artifacts.is_array());
    REQUIRE(artifacts.size() == 2);

    int csvSeen = 0;
    int jsonSeen = 0;
    for (const auto& entry : artifacts) {
        const std::string artifactId = entry.value("artifact_id", "");
        REQUIRE_FALSE(artifactId.empty());
        const auto artifact = fx.artifacts->get(artifactId);
        CHECK(artifact.artifactId == artifactId);
        CHECK(artifact.jobId == out.jobId);
        REQUIRE(std::filesystem::exists(artifact.path));
        const long long onDisk = static_cast<long long>(
            std::filesystem::file_size(artifact.path));
        CHECK(artifact.sizeBytes == onDisk);
        CHECK(onDisk > 0);
        // SHA-256 stored at registration must match the bytes on disk.
        CHECK(artifact.checksum == trinity::artifacts::sha256File(artifact.path));
        CHECK(artifact.checksum.size() == 64);
        CHECK(entry.value("checksum", "") == artifact.checksum);
        if (artifact.type == "csv") ++csvSeen;
        if (artifact.type == "json") ++jsonSeen;
    }
    CHECK(csvSeen == 1);
    CHECK(jsonSeen == 1);

    // SQLite persistence: artifacts + validations rows exist.
    const auto artifactRows = fx.db->queryParams(
        "SELECT artifact_id, checksum FROM artifacts WHERE job_id = ?", {out.jobId});
    CHECK(artifactRows.size() == 2);
    const auto validationRows =
        fx.db->queryParams("SELECT status FROM validations WHERE job_id = ?",
                           {out.jobId});
    REQUIRE(validationRows.size() == 1);
    // Real verification checks ran (finite + monotonic + closed form).
    CHECK(validationRows[0][0] == "VERIFIED");
}

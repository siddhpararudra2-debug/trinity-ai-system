#include <doctest.h>

#include <filesystem>
#include <memory>

#include "trinity/artifacts/Artifact.hpp"
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

#include <doctest.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/SimulationEngine.hpp"
#include "trinity/engines/StubEngines.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/jobs/JobWorker.hpp"
#include "trinity/simulation/Integrator.hpp"
#include "trinity/storage/Database.hpp"

namespace {

struct Fixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;

    Fixture() {
        dir = (std::filesystem::temp_directory_path() / "trinity-test-sim-cancel").string();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        trinity::engines::registerAllEngines(*registry);
        artifacts = std::make_shared<trinity::artifacts::ArtifactManager>(db, dir + "/artifacts");
        jobs = std::make_shared<trinity::jobs::JobManager>(db, registry, artifacts);
    }
    ~Fixture() {
        jobs.reset();
        artifacts.reset();
        registry.reset();
        db.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("engine cancelCheck immediately returns non-success cancelled result") {
    trinity::engines::SimulationEngine engine;
    trinity::engines::EngineRequest request;
    request.engine = "simulation";
    request.operation = "simulate_linear_motion";
    // Enough steps that the cancel probe runs at i==0 before sampling.
    request.parameters = {{"duration_s", 10.0},
                          {"dt", 0.01},
                          {"initial_velocity_m_s", 1.0},
                          {"write_artifacts", false}};
    request.cancelCheck = []() { return true; };
    const auto result = engine.execute(request);
    CHECK_FALSE(result.success);
    REQUIRE_FALSE(result.errors.empty());
    std::string message = result.errors.front().value("message", "");
    CHECK(message.find("cancelled") != std::string::npos);
    CHECK(result.result.is_object());
    CHECK(result.pendingArtifacts.empty());
}

TEST_CASE("cancelCheck false still completes successfully") {
    trinity::engines::SimulationEngine engine;
    trinity::engines::EngineRequest request;
    request.engine = "simulation";
    request.operation = "simulate_linear_motion";
    request.parameters = {{"duration_s", 1.0},
                          {"dt", 0.01},
                          {"initial_velocity_m_s", 1.0},
                          {"write_artifacts", false}};
    request.cancelCheck = []() { return false; };
    const auto result = engine.execute(request);
    CHECK(result.success);
    CHECK(result.result["result"]["method"] == "closed_form");
}

TEST_CASE("cancelProbe on integrator stops mid-run without samples claimed as success") {
    trinity::simulation::SimulationProject project;
    project.projectId = "cancel";
    project.name = "cancel";
    project.type = "kinematics";
    project.model = "linear_motion";
    project.dt = 0.01;
    project.durationS = 5.0;
    project.initial.velocity.x = 1.0;

    std::atomic<int> calls{0};
    const auto outcome = trinity::simulation::integrate(project, [&]() {
        return calls.fetch_add(1) >= 0;  // always cancel once checked
    });
    CHECK_FALSE(outcome.success);
    CHECK(outcome.cancelled);
    CHECK(outcome.error.find("cancelled") != std::string::npos);
    CHECK(outcome.result.samples.empty());
}

TEST_CASE("JobManager cancel before start marks job cancelled without engine result") {
    Fixture fx;
    auto job = fx.jobs->createJob("simulation", "simulate_linear_motion",
                                  {{"duration_s", 1.0},
                                   {"dt", 0.01},
                                   {"initial_velocity_m_s", 1.0},
                                   {"write_artifacts", false}});
    CHECK(fx.jobs->cancel(job.jobId));
    CHECK(trinity::jobs::toString(fx.jobs->get(job.jobId).status) == "cancelled");
    // Cancelling a terminal job is a no-op.
    CHECK_FALSE(fx.jobs->cancel(job.jobId));
}

TEST_CASE("worker token cancellation is visible via isCancellationRequested") {
    Fixture fx;
    auto job = fx.jobs->createJob("simulation", "simulate_linear_motion",
                                  {{"duration_s", 0.1},
                                   {"dt", 0.01},
                                   {"initial_velocity_m_s", 1.0},
                                   {"write_artifacts", false}});
    auto token = std::make_shared<trinity::jobs::CancellationToken>();
    CHECK_FALSE(fx.jobs->isCancellationRequested(job.jobId));
    // Simulate a worker cancelling mid-flight.
    fx.jobs->cancel(job.jobId);
    CHECK(fx.jobs->isCancellationRequested(job.jobId));
    const auto response = fx.jobs->executeJob(job.jobId, token);
    CHECK(response["success"] == false);
    CHECK(trinity::jobs::toString(fx.jobs->get(job.jobId).status) == "cancelled");
}

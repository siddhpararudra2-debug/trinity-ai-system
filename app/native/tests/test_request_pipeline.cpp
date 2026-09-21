#include <doctest.h>

#include <filesystem>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/MathEngine.hpp"
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
        dir = (std::filesystem::temp_directory_path() / "trinity-test-pipeline").string();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<trinity::engines::MathEngine>());
        registry->registerEngine(std::make_shared<trinity::engines::CadEngine>());
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

TEST_CASE("full pipeline executes math request end to end") {
    Fixture fx;
    auto out = fx.pipeline->executeSync("Calculate 25 * 8");
    CHECK(out.success);
    CHECK(out.routing.routed);
    CHECK(out.routing.engine == "math");
    CHECK_FALSE(out.jobId.empty());
    CHECK(out.engineEnvelope["success"] == true);
    CHECK(out.engineEnvelope["result"]["value"] == doctest::Approx(200.0));
    // Job persisted with lifecycle timestamps.
    auto job = fx.jobs->get(out.jobId);
    CHECK(trinity::jobs::toString(job.status) == "completed");
    CHECK_FALSE(job.startedAt.empty());
    CHECK_FALSE(job.completedAt.empty());
}

TEST_CASE("pipeline never bypasses validation") {
    Fixture fx;
    auto out = fx.pipeline->executeSync("Create a drone frame");
    CHECK_FALSE(out.success);
    // No job created for invalid requests.
    CHECK(out.jobId.empty());
    CHECK(fx.jobs->listRecent(10).empty());
}

TEST_CASE("unsupported CAD returns truthful structured failure") {
    Fixture fx;
    // Plate is parsed VALID but has no implemented engine path: it must be
    // rejected truthfully — either at validation/routing (no job) or as a
    // failed job with no fake geometry. Both are honest; neither may invent
    // artifacts.
    auto out = fx.pipeline->executeSync("Generate a 100 mm plate with 4 mm thickness");
    CHECK_FALSE(out.success);
    if (!out.jobId.empty()) {
        auto job = fx.jobs->get(out.jobId);
        CHECK(trinity::jobs::toString(job.status) == "failed");
        CHECK(job.artifactIds.empty());
    } else {
        CHECK_FALSE(out.validation.passed());
    }
}

TEST_CASE("CAD unknown operation fails without artifacts") {
    Fixture fx;
    auto resp = fx.jobs->runSync("cad", "generate_quadcopter_frame", {});
    CHECK(resp["success"] == false);
    CHECK(resp["artifacts"].empty());
}

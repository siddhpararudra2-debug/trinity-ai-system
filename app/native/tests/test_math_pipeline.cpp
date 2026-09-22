#include <doctest.h>

#include <filesystem>

#include "trinity/artifacts/Artifact.hpp"
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
        dir = (std::filesystem::temp_directory_path() / "trinity-test-math-pipeline")
                  .string();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<trinity::engines::MathEngine>());
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

void checkPersisted(Fixture& fx, const std::string& jobId, bool completed) {
    REQUIRE_FALSE(jobId.empty());
    const auto job = fx.jobs->get(jobId);
    CHECK(job.engine == "math");
    CHECK(trinity::jobs::toString(job.status) ==
          (completed ? "completed" : "failed"));
    CHECK_FALSE(job.startedAt.empty());
    CHECK_FALSE(job.completedAt.empty());
    CHECK_FALSE(job.result.is_null());
}

}  // namespace

TEST_CASE("pipeline executes math evaluate end to end with persistence") {
    Fixture fx;
    auto out = fx.pipeline->executeSync("Calculate 2*x + 5 with x = 10");
    CHECK(out.success);
    CHECK(out.routing.engine == "math");
    CHECK(out.engineEnvelope["result"]["value"] == doctest::Approx(25.0));
    checkPersisted(fx, out.jobId, true);
}

TEST_CASE("pipeline executes linear and quadratic solvers end to end") {
    Fixture fx;
    auto lin = fx.pipeline->executeSync("Solve linear with a = 2, b = 4");
    CHECK(lin.success);
    CHECK(lin.engineEnvelope["result"]["solution"] == doctest::Approx(-2.0));
    checkPersisted(fx, lin.jobId, true);

    auto quad = fx.pipeline->executeSync("Solve quadratic with a = 1, b = -5, c = 6");
    CHECK(quad.success);
    REQUIRE(quad.engineEnvelope["result"]["solutions"].size() == 2);
    CHECK(quad.engineEnvelope["result"]["solutions"][0].get<double>() ==
          doctest::Approx(2.0));
    CHECK(quad.engineEnvelope["result"]["solutions"][1].get<double>() ==
          doctest::Approx(3.0));
    checkPersisted(fx, quad.jobId, true);
}

TEST_CASE("pipeline executes convert and formula end to end") {
    Fixture fx;
    auto conv = fx.pipeline->executeSync("Convert 10 cm to mm");
    CHECK(conv.success);
    CHECK(conv.engineEnvelope["result"]["value"] == doctest::Approx(100.0));
    checkPersisted(fx, conv.jobId, true);

    auto deg = fx.pipeline->executeSync("Convert 180 deg to rad");
    CHECK(deg.success);
    CHECK(deg.engineEnvelope["result"]["value"] ==
          doctest::Approx(3.141592653589793));
    checkPersisted(fx, deg.jobId, true);

    auto force = fx.pipeline->executeSync("Formula force with m = 2, a = 3");
    CHECK(force.success);
    CHECK(force.engineEnvelope["result"]["outputs"]["F"] == doctest::Approx(6.0));
    checkPersisted(fx, force.jobId, true);

    auto ohm = fx.pipeline->executeSync("Formula ohm with V = 12, R = 6");
    CHECK(ohm.success);
    CHECK(ohm.engineEnvelope["result"]["outputs"]["I"] == doctest::Approx(2.0));
    checkPersisted(fx, ohm.jobId, true);
}

TEST_CASE("pipeline fails invalid math truthfully with persisted jobs") {
    Fixture fx;
    // Dimensional mismatch reaches the engine and fails the job.
    auto mismatch = fx.pipeline->executeSync("Convert 10 kg to mm");
    CHECK_FALSE(mismatch.success);
    if (!mismatch.jobId.empty()) {
        checkPersisted(fx, mismatch.jobId, false);
    }
    // Malformed expressions fail rather than returning success=true.
    auto bad = fx.pipeline->executeSync("Calculate 1 / 0");
    CHECK_FALSE(bad.success);
    if (!bad.jobId.empty()) {
        checkPersisted(fx, bad.jobId, false);
    }
}

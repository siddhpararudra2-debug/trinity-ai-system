#include <doctest.h>

#include <filesystem>
#include <thread>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/MathEngine.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/jobs/JobWorker.hpp"
#include "trinity/storage/Database.hpp"

namespace {

struct Fixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;

    Fixture() {
        dir = (std::filesystem::temp_directory_path() / "trinity-test-lifecycle").string();
        std::filesystem::remove_all(dir);
        const std::string dbPath = dir + "/trinity.db";
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dbPath);
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<trinity::engines::MathEngine>());
        registry->registerEngine(std::make_shared<trinity::engines::CadEngine>());
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

TEST_CASE("job creation populates lifecycle fields") {
    Fixture fx;
    auto job = fx.jobs->createJob("math", "evaluate_expression", {{"expression", "25 * 8"}});
    CHECK_FALSE(job.jobId.empty());
    CHECK_FALSE(job.requestId.empty());
    CHECK(job.engine == "math");
    CHECK(job.operation == "evaluate_expression");
    CHECK(trinity::jobs::toString(job.status) == "queued");
    CHECK_FALSE(job.createdAt.empty());
    CHECK(job.startedAt.empty());
    CHECK(job.completedAt.empty());
    CHECK(job.input["expression"] == "25 * 8");
    CHECK(job.request["expression"] == "25 * 8");
}

TEST_CASE("job state transitions enforce lifecycle") {
    CHECK(trinity::jobs::canTransition(trinity::jobs::JobStatus::Queued,
                                       trinity::jobs::JobStatus::Running));
    CHECK(trinity::jobs::canTransition(trinity::jobs::JobStatus::Queued,
                                       trinity::jobs::JobStatus::Cancelled));
    CHECK(trinity::jobs::canTransition(trinity::jobs::JobStatus::Running,
                                       trinity::jobs::JobStatus::Completed));
    CHECK(trinity::jobs::canTransition(trinity::jobs::JobStatus::Running,
                                       trinity::jobs::JobStatus::Failed));
    CHECK_FALSE(trinity::jobs::canTransition(trinity::jobs::JobStatus::Queued,
                                             trinity::jobs::JobStatus::Completed));
    CHECK_FALSE(trinity::jobs::canTransition(trinity::jobs::JobStatus::Completed,
                                             trinity::jobs::JobStatus::Running));
    CHECK_FALSE(trinity::jobs::canTransition(trinity::jobs::JobStatus::Failed,
                                             trinity::jobs::JobStatus::Cancelled));
    // Future states reserved: transitions rejected for now.
    CHECK_FALSE(trinity::jobs::canTransition(trinity::jobs::JobStatus::Running,
                                             trinity::jobs::JobStatus::Retrying));
    CHECK_FALSE(trinity::jobs::canTransition(trinity::jobs::JobStatus::Running,
                                             trinity::jobs::JobStatus::WaitingApproval));
    CHECK(trinity::jobs::isTerminal(trinity::jobs::JobStatus::Completed));
    CHECK_FALSE(trinity::jobs::isTerminal(trinity::jobs::JobStatus::Running));
}

TEST_CASE("invalid state transitions throw") {
    Fixture fx;
    auto job = fx.jobs->createJob("math", "evaluate_expression", {{"expression", "1+1"}});
    // QUEUED -> COMPLETED directly is invalid.
    CHECK_THROWS_AS(fx.jobs->updateStatus(job.jobId, trinity::jobs::JobStatus::Completed),
                    trinity::core::RequestValidationError);
}

TEST_CASE("job persistence round-trips lifecycle fields") {
    Fixture fx;
    trinity::jobs::JobCreateOptions opts;
    opts.requestId = "req-1";
    opts.workflowId = "wf-1";
    opts.timeoutMs = 5000;
    auto job = fx.jobs->createJob("math", "evaluate_expression", {{"expression", "2+2"}}, opts);
    auto loaded = fx.jobs->get(job.jobId);
    CHECK(loaded.requestId == "req-1");
    CHECK(loaded.workflowId == "wf-1");
    CHECK(loaded.timeoutMs == 5000);
    fx.jobs->updateResult(job.jobId, {{"value", 4}});
    fx.jobs->updateError(job.jobId, nullptr);
    fx.jobs->attachArtifacts(job.jobId, {"art-1"});
    auto updated = fx.jobs->get(job.jobId);
    CHECK(updated.result["value"] == 4);
    CHECK(updated.artifactIds.size() == 1);
    CHECK(updated.artifactIds[0] == "art-1");
    CHECK(fx.jobs->listRecent(10).size() == 1);
}

TEST_CASE("cancel queued job is reliable") {
    Fixture fx;
    auto job = fx.jobs->createJob("math", "evaluate_expression", {{"expression", "1+1"}});
    CHECK(fx.jobs->cancel(job.jobId));
    CHECK(trinity::jobs::toString(fx.jobs->get(job.jobId).status) == "cancelled");
    // Terminal: second cancel returns false.
    CHECK_FALSE(fx.jobs->cancel(job.jobId));
}

TEST_CASE("math execution completes only on real success") {
    Fixture fx;
    auto resp = fx.jobs->runSync("math", "evaluate_expression", {{"expression", "25 * 8"}});
    CHECK(resp["success"] == true);
    CHECK(resp["result"]["value"] == doctest::Approx(200.0));
    auto job = fx.jobs->get(resp["job_id"].get<std::string>());
    CHECK(trinity::jobs::toString(job.status) == "completed");
    CHECK_FALSE(job.startedAt.empty());
    CHECK_FALSE(job.completedAt.empty());
}

TEST_CASE("unsupported CAD execution fails truthfully without artifacts") {
    Fixture fx;
    auto resp = fx.jobs->runSync("cad", "generate_quadcopter_frame", {});
    CHECK(resp["success"] == false);
    CHECK_FALSE(resp["errors"].empty());
    CHECK(resp["artifacts"].empty());
    auto job = fx.jobs->get(resp["job_id"].get<std::string>());
    CHECK(trinity::jobs::toString(job.status) == "failed");
}

TEST_CASE("recoverOnStartup re-queues interrupted running jobs") {
    Fixture fx;
    auto job = fx.jobs->createJob("math", "evaluate_expression", {{"expression", "1+1"}});
    // Simulate crash mid-run: force RUNNING row directly.
    fx.db->execute("UPDATE jobs SET status='running' WHERE job_id=?;", {job.jobId});
    const int n = fx.jobs->recoverOnStartup();
    CHECK(n >= 1);
    CHECK(trinity::jobs::toString(fx.jobs->get(job.jobId).status) == "queued");
}

TEST_CASE("worker executes off-thread and UI can poll") {
    Fixture fx;
    auto worker = std::make_shared<trinity::jobs::JobWorker>(fx.jobs, 1);
    worker->start();
    const std::string id =
        worker->submit("math", "evaluate_expression", {{"expression", "25 * 8"}});
    // Poll like the Qt QTimer does.
    trinity::jobs::Job polled = fx.jobs->get(id);
    CHECK((trinity::jobs::toString(polled.status) == "queued" ||
           trinity::jobs::toString(polled.status) == "running" ||
           trinity::jobs::toString(polled.status) == "completed"));
    for (int i = 0; i < 100; ++i) {
        polled = fx.jobs->get(id);
        if (trinity::jobs::isTerminal(polled.status)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(trinity::jobs::toString(fx.jobs->get(id).status) == "completed");
    CHECK(fx.jobs->get(id).result["value"] == doctest::Approx(200.0));
    worker->stop();
}

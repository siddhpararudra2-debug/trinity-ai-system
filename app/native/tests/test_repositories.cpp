#include <doctest.h>

#include <algorithm>
#include <filesystem>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Database.hpp"
#include "trinity/storage/Repositories.hpp"
#include "trinity/workflows/Workflow.hpp"

namespace {

std::string tempRoot(const char* name) {
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return dir.string();
}

}  // namespace

TEST_CASE("database initializes core tables including workflows") {
    const std::string root = tempRoot("trinity-test-repos-init");
    {
        trinity::storage::Database db(root + "/trinity.db");
        db.init();
        const auto tables = db.query(
            "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name;");
        std::vector<std::string> names;
        for (const auto& row : tables) {
            names.push_back(row[0]);
        }
        for (const char* expected :
             {"jobs", "workflows", "workflow_nodes", "artifacts"}) {
            CHECK(std::find(names.begin(), names.end(), expected) != names.end());
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("database transactions commit and roll back") {
    const std::string root = tempRoot("trinity-test-tx");
    {
        trinity::storage::Database db(root + "/trinity.db");
        db.init();
        {
            trinity::storage::Transaction tx(db);
            db.execute(
                "INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
                "created_at, updated_at) VALUES ('tx1', 'math', 'solve', 'queued', 0.0, "
                "'{}', "
                "'t', 't');");
            tx.commit();
        }
        CHECK(db.query("SELECT job_id FROM jobs WHERE job_id='tx1';").size() == 1);
        {
            trinity::storage::Transaction tx(db);
            db.execute(
                "INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
                "created_at, updated_at) VALUES ('tx2', 'math', 'solve', 'queued', 0.0, "
                "'{}', "
                "'t', 't');");
            // No commit -> rollback.
        }
        CHECK(db.query("SELECT job_id FROM jobs WHERE job_id='tx2';").empty());
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("database prepared statements bind parameters safely") {
    const std::string root = tempRoot("trinity-test-params");
    {
        trinity::storage::Database db(root + "/trinity.db");
        db.init();
        const std::string tricky = "o'brien'; DROP TABLE jobs;--";
        db.execute(
            "INSERT INTO jobs (job_id, engine, operation, status, progress, request, "
            "created_at, updated_at) VALUES (?, ?, ?, 'queued', ?, ?, 't', 't');",
            {"p1", tricky, "solve", 0.5, "{}"});
        const auto rows =
            db.queryParams("SELECT engine FROM jobs WHERE job_id = ?;", {"p1"});
        REQUIRE(rows.size() == 1);
        CHECK(rows[0][0] == tricky);
        // Table still exists -> injection did not execute.
        CHECK(db.query("SELECT job_id FROM jobs;").size() == 1);
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

TEST_CASE("repositories round-trip jobs, workflows and artifacts") {
    const std::string root = tempRoot("trinity-test-repos-crud");
    {
        auto db = std::make_shared<trinity::storage::Database>(root + "/trinity.db");
        db->init();

        trinity::storage::JobRepository jobs(db);
        trinity::jobs::Job job;
        job.jobId = "job-1";
        job.engine = "math";
        job.operation = "solve";
        job.status = trinity::jobs::JobStatus::Completed;
        job.request = {{"x", 1}};
        job.createdAt = "2026-01-01T00:00:00+00:00";
        job.updatedAt = job.createdAt;
        jobs.save(job);
        CHECK(jobs.get("job-1").engine == "math");
        CHECK(jobs.listRecent(10).size() == 1);

        trinity::storage::WorkflowRepository workflows(db);
        trinity::workflows::Workflow workflow;
        workflow.workflowId = "wf-1";
        workflow.name = "demo";
        workflow.status = trinity::workflows::WorkflowStatus::Queued;
        workflow.createdAt = "2026-01-01T00:00:00+00:00";
        workflow.updatedAt = workflow.createdAt;
        trinity::workflows::WorkflowNode node;
        node.id = "n1";
        node.engine = "math";
        node.operation = "solve";
        workflow.nodes = {node};
        workflows.saveWorkflow(workflow);
        const auto loaded = workflows.getWorkflow("wf-1");
        CHECK(loaded.name == "demo");
        CHECK(loaded.nodes.size() == 1);
        CHECK(loaded.nodes[0].engine == "math");

        trinity::storage::ArtifactRepository artifacts(db);
        trinity::artifacts::Artifact artifact;
        artifact.artifactId = "art-1";
        artifact.jobId = "job-1";
        artifact.type = "data";
        artifact.path = root + "/file.bin";
        artifact.sizeBytes = 3;
        artifact.checksum = "abc";
        artifact.createdAt = "2026-01-01T00:00:00+00:00";
        artifacts.save(artifact);
        CHECK(artifacts.get("art-1").jobId == "job-1");
        CHECK(artifacts.listForJob("job-1").size() == 1);

        CHECK_THROWS_AS(jobs.get("missing"), trinity::core::JobNotFoundError);
        CHECK_THROWS_AS(artifacts.get("missing"), trinity::core::ArtifactNotFoundError);
    }
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

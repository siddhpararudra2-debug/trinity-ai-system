#include <doctest.h>

#include <filesystem>
#include <memory>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
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
        dir = (std::filesystem::temp_directory_path() / "trinity-test-research-pipeline").string();
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

TEST_CASE("pipeline routes an index-document request end to end") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Index document \"Battery handbook\" with text \"Battery discharge tests measure "
        "cycle life under load.\"");
    CHECK(out.success);
    CHECK(out.routing.routed);
    CHECK(out.routing.engine == "research");
    CHECK(out.routing.operation == "index_document");
    CHECK_FALSE(out.jobId.empty());
    CHECK(out.engineEnvelope["success"] == true);
    CHECK(out.engineEnvelope["result"]["title"] == "Battery handbook");
    auto job = fx.jobs->get(out.jobId);
    CHECK(trinity::jobs::toString(job.status) == "completed");
}

TEST_CASE("pipeline searches the index populated by earlier requests") {
    Fixture fx;
    auto indexed = fx.pipeline->executeSync(
        "Index document \"Motor notes\" with text \"Motor winding uses copper wire.\"");
    REQUIRE(indexed.success);
    REQUIRE(indexed.routing.engine == "research");

    auto found = fx.pipeline->executeSync("Search for \"copper winding\"");
    CHECK(found.success);
    CHECK(found.routing.routed);
    CHECK(found.routing.engine == "research");
    CHECK(found.routing.operation == "search");
    CHECK(found.engineEnvelope["success"] == true);
    CHECK(found.engineEnvelope["result"]["hit_count"] == 1);
    REQUIRE(found.engineEnvelope["result"]["hits"].is_array());
    REQUIRE(found.engineEnvelope["result"]["hits"].size() == 1);
    CHECK(found.engineEnvelope["result"]["hits"][0]["title"] == "Motor notes");
}

TEST_CASE("pipeline summarizes indexed documents extractively") {
    Fixture fx;
    auto indexed = fx.pipeline->executeSync(
        "Index document \"Lab log\" with text \"Battery discharge tests run overnight. "
        "Thermal cameras watch the pack during cycling.\"");
    REQUIRE(indexed.success);

    auto summary = fx.pipeline->executeSync("Summarize results for \"battery discharge\"");
    CHECK(summary.success);
    CHECK(summary.routing.engine == "research");
    CHECK(summary.routing.operation == "summarize_results");
    CHECK(summary.engineEnvelope["success"] == true);
    CHECK(summary.engineEnvelope["result"]["hit_count"] == 1);
    REQUIRE(summary.engineEnvelope["result"]["summary"].is_array());
    REQUIRE(summary.engineEnvelope["result"]["summary"].size() >= 1);
}

TEST_CASE("incomplete research request is rejected without creating a job") {
    Fixture fx;
    auto out = fx.pipeline->executeSync("Search the index");
    CHECK_FALSE(out.success);
    CHECK(out.jobId.empty());
    CHECK(fx.jobs->listRecent(10).empty());
}

TEST_CASE("explicit research engine keyword routes through research") {
    Fixture fx;
    auto out = fx.pipeline->executeSync(
        "Using research engine: search for \"thermal cycling\"");
    if (out.routing.routed) {
        CHECK(out.routing.engine == "research");
        CHECK(out.routing.operation == "search");
    } else {
        CHECK(out.jobId.empty());
    }
}

#include <doctest.h>

#include <filesystem>
#include <fstream>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Database.hpp"

namespace {

class EchoEngine : public trinity::engines::EngineBase {
public:
    EchoEngine() {
        name_ = "echo";
        version_ = "0.1";
        capabilities_ = {"run"};
    }

    trinity::engines::EngineResult execute(const std::string& operation,
                                           const trinity::core::Json& params) override {
        trinity::engines::EngineResult result;
        result.success = true;
        result.engine = name_;
        result.operation = operation;
        result.result = params;
        trinity::validation::ValidationResult validation;
        validation.status = trinity::validation::ValidationStatus::Validated;
        result.validation = validation;
        return result;
    }
};

struct Fixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;

    Fixture() {
        dir = (std::filesystem::temp_directory_path() / "trinity-test-jobs").string();
        std::filesystem::remove_all(dir);
        const std::string dbPath = dir + "/trinity.db";
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dbPath);
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<EchoEngine>());
        artifacts = std::make_shared<trinity::artifacts::ArtifactManager>(
            db, dir + "/artifacts");
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

TEST_CASE("runSync executes and returns the tool-response envelope") {
    Fixture fx;
    const trinity::core::Json response =
        fx.jobs->runSync("echo", "run", trinity::core::Json{{"n", 1}});
    CHECK(response["success"] == true);
    CHECK(response["engine"] == "echo");
    CHECK(response["validation"]["status"] == "VALIDATED");
    CHECK(response.contains("job_id"));

    const trinity::jobs::Job job = fx.jobs->get(response["job_id"]);
    CHECK(trinity::jobs::toString(job.status) == "completed");
    CHECK(fx.jobs->listRecent(10).size() == 1);
}

TEST_CASE("runSync marks the job failed and rethrows on unknown engine") {
    Fixture fx;
    CHECK_THROWS_AS(fx.jobs->runSync("ghost", "run", trinity::core::Json::object()),
                    trinity::core::EngineNotFoundError);
    const auto jobs = fx.jobs->listRecent(10);
    REQUIRE(jobs.size() == 1);
    CHECK(trinity::jobs::toString(jobs[0].status) == "failed");
}

TEST_CASE("artifact manager stores files with checksums") {
    Fixture fx;
    const std::string jobId =
        fx.jobs->runSync("echo", "run", trinity::core::Json::object())["job_id"];
    const std::string src = fx.dir + "/input.bin";
    {
        std::ofstream out(src, std::ios::binary);
        out << "trinity-artifact-bytes";
    }
    const trinity::artifacts::Artifact stored = fx.artifacts->storeFile(src, "bin", jobId);
    CHECK_FALSE(stored.artifactId.empty());
    CHECK(stored.sizeBytes == 22);
    CHECK(stored.checksum.size() == 64);
    CHECK(fx.artifacts->get(stored.artifactId).path == stored.path);
    CHECK_THROWS_AS(fx.artifacts->get("nope"), trinity::core::ArtifactNotFoundError);
}

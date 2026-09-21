#include <doctest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"
#include "trinity/intelligence/Planner.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Database.hpp"

namespace {

/// Test-only provider emitting a canned plan. Lives in tests/ so it can
/// never leak into the shipped factory registry.
class ScriptedProvider : public trinity::intelligence::IModelProvider {
public:
    explicit ScriptedProvider(trinity::intelligence::ModelResponse script)
        : script_(std::move(script)) {}

    trinity::intelligence::ModelProviderInfo info() const override {
        trinity::intelligence::ModelProviderInfo info;
        info.providerId = "scripted-test";
        info.displayName = "Scripted test provider";
        info.available = true;
        info.configured = true;
        return info;
    }

    trinity::intelligence::ModelResponse generate(
        const trinity::intelligence::ModelRequest&) override {
        return script_;
    }

    trinity::core::Result<trinity::intelligence::ModelResponse> generatePlan(
        const trinity::intelligence::ModelRequest& request) override {
        return trinity::core::Result<trinity::intelligence::ModelResponse>::ok(
            generate(request));
    }

    void streamPlan(const trinity::intelligence::ModelRequest&,
                    StreamCallback callback) override {
        callback(script_.text, true);
    }

    trinity::core::Status configure(const trinity::core::Json&) override {
        return trinity::core::okStatus();
    }

private:
    trinity::intelligence::ModelResponse script_;
};

trinity::intelligence::ModelResponse refusalScript() {
    trinity::intelligence::ModelResponse response;
    response.success = false;
    response.error = trinity::core::Json{{"code", "capability_unavailable"},
                                        {"message", "no model"},
                                        {"details", trinity::core::Json::object()}};
    return response;
}

struct PlannerFixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;

    PlannerFixture() {
        dir = (std::filesystem::temp_directory_path() / "trinity-test-planner").string();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir + "/artifacts", ec);
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        trinity::engines::registerAllEngines(*registry);
        artifacts = std::make_shared<trinity::artifacts::ArtifactManager>(
            db, dir + "/artifacts");
        jobs = std::make_shared<trinity::jobs::JobManager>(db, registry, artifacts);
    }

    ~PlannerFixture() {
        jobs.reset();
        artifacts.reset();
        registry.reset();
        db.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("planner executes model tool calls through real engines") {
    PlannerFixture fx;
    trinity::intelligence::ModelResponse script;
    script.success = true;
    trinity::intelligence::ToolCall math;
    math.engine = "math";
    math.operation = "evaluate_expression";
    math.parameters = {{"expression", "2 + 3 * 4"}};
    trinity::intelligence::ToolCall math2;
    math2.engine = "math";
    math2.operation = "evaluate_expression";
    math2.parameters = {{"expression", "10 + 5"}};
    script.toolCalls = {math, math2};

    ScriptedProvider model(script);
    trinity::intelligence::Planner planner(model, *fx.jobs, *fx.registry);
    trinity::intelligence::ModelRequest request;
    request.prompt = "demo";
    const auto plan = planner.planAndExecute(request);
    CHECK(plan.success);
    REQUIRE(plan.steps.size() == 2);
    CHECK(plan.steps[0].executed);
    CHECK_FALSE(plan.steps[0].jobId.empty());
    CHECK(plan.steps[0].response.value("success", false));
    CHECK(plan.steps[0].response["result"].value("value", 0.0) == 14.0);
    CHECK(plan.steps[1].executed);
    CHECK(plan.steps[1].success);
    CHECK(plan.steps[1].response["result"].value("value", 0.0) == 15.0);
    // Round-trips for persistence/logs.
    CHECK(trinity::intelligence::PlanResult::fromJson(plan.toJson()).success);
}

TEST_CASE("planner refuses unknown engine without executing") {
    PlannerFixture fx;
    trinity::intelligence::ModelResponse script;
    script.success = true;
    trinity::intelligence::ToolCall bad;
    bad.engine = "ghost";
    bad.operation = "run";
    trinity::intelligence::ToolCall good;
    good.engine = "math";
    good.operation = "evaluate_expression";
    good.parameters = {{"expression", "1 + 1"}};
    script.toolCalls = {bad, good};

    ScriptedProvider model(script);
    trinity::intelligence::Planner planner(model, *fx.jobs, *fx.registry);
    trinity::intelligence::ModelRequest request;
    const auto plan = planner.planAndExecute(request);
    CHECK_FALSE(plan.success);
    REQUIRE(plan.steps.size() == 2);
    CHECK_FALSE(plan.steps[0].executed);
    CHECK(plan.steps[0].jobId.empty());
    CHECK(plan.steps[1].skipped);
    CHECK_FALSE(plan.steps[1].executed);
}

TEST_CASE("planner executes nothing on model refusal") {
    PlannerFixture fx;
    ScriptedProvider model(refusalScript());
    trinity::intelligence::Planner planner(model, *fx.jobs, *fx.registry);
    trinity::intelligence::ModelRequest request;
    const auto plan = planner.planAndExecute(request);
    CHECK_FALSE(plan.success);
    CHECK(plan.steps.empty());
    CHECK(fx.jobs->listRecent(10).empty());
}

TEST_CASE("planner with no tool calls succeeds trivially") {
    PlannerFixture fx;
    trinity::intelligence::ModelResponse script;
    script.success = true;
    script.text = "nothing to do";
    ScriptedProvider model(script);
    trinity::intelligence::Planner planner(model, *fx.jobs, *fx.registry);
    trinity::intelligence::ModelRequest request;
    const auto plan = planner.planAndExecute(request);
    CHECK(plan.success);
    CHECK(plan.steps.empty());
}

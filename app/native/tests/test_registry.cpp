#include <doctest.h>

#include <algorithm>

#include "trinity/engines/EngineRegistry.hpp"

namespace {

class StubEngine : public trinity::engines::EngineBase {
public:
    StubEngine(std::string name, std::vector<std::string> caps) {
        name_ = std::move(name);
        version_ = "1.0";
        capabilities_ = std::move(caps);
    }

    trinity::engines::EngineResult execute(
        const trinity::engines::EngineRequest& request) override {
        requireCapability(request);
        return successResult(request, request.parameters);
    }
};

}  // namespace

TEST_CASE("registry registers, finds, checks, and lists engines") {
    trinity::engines::EngineRegistry registry;
    CHECK_FALSE(registry.has("math"));
    CHECK(registry.list().empty());

    registry.registerEngine(std::make_shared<StubEngine>("math", std::vector<std::string>{"solve"}));
    CHECK(registry.has("math"));
    CHECK_FALSE(registry.has("cad"));

    auto engine = registry.get("math");
    CHECK(engine->describe().name == "math");
    CHECK(engine->describe().version == "1.0");

    const auto listed = registry.list();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].capabilities == std::vector<std::string>{"solve"});
}

TEST_CASE("registry miss names the engine and available set") {
    trinity::engines::EngineRegistry registry;
    registry.registerEngine(std::make_shared<StubEngine>("math", std::vector<std::string>{"solve"}));
    try {
        registry.get("cad");
        FAIL("expected EngineNotFoundError");
    } catch (const trinity::core::EngineNotFoundError& err) {
        CHECK(err.toJson()["code"] == "engine_not_found");
        CHECK(err.toJson()["details"]["available"] ==
              trinity::core::Json::array({"math"}));
    }
}

TEST_CASE("planned engine catalogue covers the eight future engines") {
    const auto& names = trinity::engines::EngineRegistry::plannedEngineNames();
    CHECK(names.size() == 8);
    CHECK(std::find(names.begin(), names.end(), "cad") != names.end());
}

TEST_CASE("registry rejects duplicate registration") {
    trinity::engines::EngineRegistry registry;
    registry.registerEngine(std::make_shared<StubEngine>("math", std::vector<std::string>{"solve"}));
    CHECK_THROWS_AS(
        registry.registerEngine(
            std::make_shared<StubEngine>("math", std::vector<std::string>{"solve"})),
        trinity::core::RequestValidationError);
}

TEST_CASE("registry unregister removes engines") {
    trinity::engines::EngineRegistry registry;
    registry.registerEngine(std::make_shared<StubEngine>("math", std::vector<std::string>{"solve"}));
    CHECK(registry.unregisterEngine("math"));
    CHECK_FALSE(registry.has("math"));
    CHECK_FALSE(registry.unregisterEngine("math"));
}

TEST_CASE("registry lists capabilities per engine") {
    trinity::engines::EngineRegistry registry;
    registry.registerEngine(std::make_shared<StubEngine>(
        "math", std::vector<std::string>{"evaluate_expression", "describe"}));
    CHECK(registry.listCapabilities("math") ==
          std::vector<std::string>{"evaluate_expression", "describe"});
    CHECK_THROWS_AS(registry.listCapabilities("ghost"), trinity::core::EngineNotFoundError);
}

TEST_CASE("registry routes requests through capability check and validate") {
    trinity::engines::EngineRegistry registry;
    registry.registerEngine(std::make_shared<StubEngine>("math", std::vector<std::string>{"solve"}));
    trinity::engines::EngineRequest ok;
    ok.engine = "math";
    ok.operation = "solve";
    const auto result = registry.execute(ok);
    CHECK(result.success);
    CHECK(result.operation == "solve");
    CHECK(result.metadata.contains("duration_ms"));

    trinity::engines::EngineRequest badOp;
    badOp.engine = "math";
    badOp.operation = "fly";
    CHECK_THROWS_AS(registry.execute(badOp), trinity::core::CapabilityUnavailableError);

    trinity::engines::EngineRequest unknown;
    unknown.engine = "ghost";
    unknown.operation = "run";
    CHECK_THROWS_AS(registry.execute(unknown), trinity::core::EngineNotFoundError);

    trinity::engines::EngineRequest invalid;
    invalid.engine = "math";
    invalid.operation = "";
    CHECK_THROWS_AS(registry.execute(invalid), trinity::core::RequestValidationError);
}

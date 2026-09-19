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

    trinity::engines::EngineResult execute(const std::string& operation,
                                           const trinity::core::Json& params) override {
        trinity::engines::EngineResult result;
        result.success = true;
        result.engine = name_;
        result.operation = operation;
        result.result = params;
        return result;
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

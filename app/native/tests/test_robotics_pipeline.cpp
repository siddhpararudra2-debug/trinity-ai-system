#include <doctest.h>

#include <cmath>

#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"
#include "trinity/intelligence/IntentRouter.hpp"
#include "trinity/intelligence/RequirementParser.hpp"

using trinity::engines::EngineRegistry;
using trinity::engines::registerAllEngines;
using trinity::intelligence::IntentRouter;
using trinity::intelligence::ParseStatus;
using trinity::intelligence::RequirementParser;

namespace {

struct IntentFixture {
    EngineRegistry registry;
    RequirementParser parser;
    IntentRouter router;

    IntentFixture() {
        registerAllEngines(registry);
    }
};

}  // namespace

TEST_CASE("robotics pipeline parses a forward-kinematics request") {
    IntentFixture f;
    const auto parsed = f.parser.parse("forward kinematics for joint angles 0, 0");
    const bool valid = parsed.status == ParseStatus::Valid;
    REQUIRE(valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "forward_kinematics");
    CHECK(parsed.intent.parameters.contains("joint_angles"));
    CHECK(parsed.intent.parameters["joint_angles"].size() == 2);

    const auto routed = f.router.route(parsed.intent, f.registry);
    REQUIRE(routed.routed);
    CHECK(routed.engine == "robotics");
    CHECK(routed.toolCall.operation == "forward_kinematics");
    CHECK(f.registry.has("robotics"));

    trinity::engines::EngineRequest req;
    req.engine = routed.toolCall.engine;
    req.operation = routed.toolCall.operation;
    req.parameters = routed.toolCall.parameters;
    const auto out = f.registry.execute(req);
    REQUIRE(out.success);
    const auto& pos = out.result["end_effector"]["position"];
    CHECK(std::fabs(pos.value("x", 0.0) - 2.0) < 1e-6);
}

TEST_CASE("robotics pipeline rejects an incomplete trajectory request") {
    IntentFixture f;
    const auto parsed = f.parser.parse("plan a robot joint trajectory");
    const bool incomplete = parsed.status == ParseStatus::Incomplete;
    REQUIRE(incomplete);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "plan_trajectory");
    REQUIRE(parsed.intent.missing.size() >= 2);
    CHECK(parsed.intent.missing[0] == "joint_start");
    CHECK(parsed.intent.missing[1] == "joint_goal");
    CHECK(parsed.errors.size() == 1);
}

TEST_CASE("robotics pipeline parses a complete trajectory request") {
    IntentFixture f;
    const auto parsed = f.parser.parse(
        "plan a joint trajectory from 0,0 to 1,0.5 over 2 seconds");
    const bool valid = parsed.status == ParseStatus::Valid;
    REQUIRE(valid);
    CHECK(parsed.intent.domain == "robotics");
    CHECK(parsed.intent.operation == "plan_trajectory");
    REQUIRE(parsed.intent.parameters.contains("joint_start"));
    REQUIRE(parsed.intent.parameters.contains("joint_goal"));
    CHECK(parsed.intent.parameters["joint_start"].size() == 2);
    CHECK(parsed.intent.parameters["joint_goal"].size() == 2);
    REQUIRE(parsed.intent.parameters.contains("duration_s"));
    CHECK(std::fabs(parsed.intent.parameters["duration_s"].get<double>() - 2.0) < 1e-9);
}

TEST_CASE("robotics pipeline does not steal non-robotics requests") {
    IntentFixture f;
    const auto search = f.parser.parse("search for papers about robot kinematics");
    CHECK(search.intent.domain == "research");
    const auto math = f.parser.parse("calculate the derivative of x^2");
    CHECK(math.intent.domain == "math");
}

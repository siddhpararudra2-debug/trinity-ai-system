#include <doctest.h>

#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/MathEngine.hpp"
#include "trinity/engines/PcbEngine.hpp"
#include "trinity/intelligence/IntentRouter.hpp"
#include "trinity/intelligence/IntentValidator.hpp"
#include "trinity/intelligence/RequirementParser.hpp"

using trinity::intelligence::IntentRouter;
using trinity::intelligence::IntentValidator;
using trinity::intelligence::ParseStatus;
using trinity::intelligence::RequirementParser;

namespace {

trinity::engines::EngineRegistry makeRegistry() {
    trinity::engines::EngineRegistry registry;
    registry.registerEngine(std::make_shared<trinity::engines::MathEngine>());
    registry.registerEngine(std::make_shared<trinity::engines::CadEngine>());
    return registry;
}

}  // namespace

TEST_CASE("CAD quadcopter intent routes to cad/generate without executing") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm quadcopter frame with 2 mm thick arms.");
    REQUIRE(trinity::intelligence::toString(parsed.status) == "VALID");

    IntentValidator validator;
    auto registry = makeRegistry();
    REQUIRE(validator.validate(parsed.intent, registry).passed());

    IntentRouter router;
    const auto route = router.route(parsed.intent, registry);
    CHECK(route.routed);
    CHECK(route.engine == "cad");
    CHECK(route.operation == "generate");
    CHECK(route.capability == "generate");
    CHECK(route.status == "ROUTED");
    CHECK(route.toolCall.engine == "cad");
    CHECK(route.toolCall.operation == "generate");
    CHECK(route.toolCall.parameters["type"] == "quadcopter_frame");
    CHECK(route.toolCall.parameters["parameters"]["overall_size"].get<double>() ==
          doctest::Approx(50.0));
    CHECK(route.toolCall.parameters["parameters"]["arm_width"].get<double>() ==
          doctest::Approx(2.0));
}

TEST_CASE("math evaluate intent routes to math engine") {
    RequirementParser parser;
    const auto parsed = parser.parse("Calculate 25 * 8");
    IntentValidator validator;
    auto registry = makeRegistry();
    REQUIRE(validator.validate(parsed.intent, registry).passed());

    IntentRouter router;
    const auto route = router.route(parsed.intent, registry);
    CHECK(route.routed);
    CHECK(route.engine == "math");
    CHECK(route.toolCall.engine == "math");
    CHECK(route.toolCall.parameters["expression"].get<std::string>() == "25 * 8");
}

TEST_CASE("math solve intent routes to math/solve") {
    RequirementParser parser;
    const auto parsed = parser.parse("Solve x^2 + 2x + 1");
    auto registry = makeRegistry();
    IntentRouter router;
    const auto route = router.route(parsed.intent, registry);
    CHECK(route.routed);
    CHECK(route.engine == "math");
    CHECK(route.operation == "solve");
    CHECK(route.capability == "solve");
}

TEST_CASE("plate intent is route-rejected with capability reason") {
    RequirementParser parser;
    const auto parsed = parser.parse("Generate a 100 mm plate with 4 mm thickness");
    REQUIRE(trinity::intelligence::toString(parsed.status) == "VALID");

    auto registry = makeRegistry();
    IntentRouter router;
    const auto route = router.route(parsed.intent, registry);
    CHECK_FALSE(route.routed);
    CHECK(route.engine == "cad");
    CHECK(route.status == "CAPABILITY_UNAVAILABLE");
    CHECK_FALSE(route.reason.empty());
    CHECK(route.reason.find("plate") != std::string::npos);
}

TEST_CASE("unsupported domain is route-rejected") {
    trinity::intelligence::Intent intent;
    intent.domain = "poetry";
    intent.operation = "generate";
    intent.object = "sonnet";
    intent.status = ParseStatus::Valid;
    auto registry = makeRegistry();
    IntentRouter router;
    const auto route = router.route(intent, registry);
    CHECK_FALSE(route.routed);
    CHECK(route.status == "REJECTED");
    CHECK_FALSE(route.reason.empty());
}

TEST_CASE("unsupported operation is route-rejected") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm quadcopter frame");
    auto tampered = parsed.intent;
    tampered.operation = "teleport";
    auto registry = makeRegistry();
    IntentRouter router;
    const auto route = router.route(tampered, registry);
    CHECK_FALSE(route.routed);
    CHECK(route.status == "REJECTED");
}

TEST_CASE("incomplete intent never routes") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a drone frame");
    REQUIRE(trinity::intelligence::toString(parsed.status) == "INCOMPLETE");
    auto registry = makeRegistry();
    IntentRouter router;
    const auto route = router.route(parsed.intent, registry);
    CHECK_FALSE(route.routed);
}

TEST_CASE("router translates intent keys into FrameParams naming") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm quadcopter frame with 2 mm thick arms.");
    const auto frameParams = IntentRouter::translateCadParams(parsed.intent);
    CHECK(frameParams["overall_size"].get<double>() == doctest::Approx(50.0));
    CHECK(frameParams["arm_width"].get<double>() == doctest::Approx(2.0));
    CHECK_FALSE(frameParams.contains("overall_size_mm"));
    CHECK_FALSE(frameParams.contains("arm_thickness_mm"));
}

TEST_CASE("route result round-trips through JSON") {
    RequirementParser parser;
    const auto parsed = parser.parse("Calculate 25 * 8");
    auto registry = makeRegistry();
    IntentRouter router;
    const auto route = router.route(parsed.intent, registry);
    const auto back =
        trinity::intelligence::RouteResult::fromJson(route.toJson());
    CHECK(back.routed == route.routed);
    CHECK(back.engine == route.engine);
    CHECK(back.operation == route.operation);
    CHECK(back.toolCall.engine == "math");
}

TEST_CASE("LLM-produced intent uses the same validation and routing path") {
    // An LLM emits an Intent JSON blob; it must pass the identical
    // validator + router gate as the deterministic parser.
    trinity::core::Json llmJson = {
        {"intent_id", "llm-1"},
        {"domain", "math"},
        {"operation", "evaluate_expression"},
        {"object", "expression"},
        {"parameters", {{"expression", "25 * 8"}}},
        {"constraints", trinity::core::Json::object()},
        {"units", ""},
        {"outputs", trinity::core::Json::array()},
        {"priority", "normal"},
        {"assumptions", trinity::core::Json::array()},
        {"missing_requirements", trinity::core::Json::array()},
        {"confidence", 0.85},
        {"source", "llm"},
        {"timestamp", "2026-01-01T00:00:00+00:00"},
        {"raw_request", "Calculate 25 * 8"},
        {"raw_metadata", trinity::core::Json::object()},
        {"status", "VALID"},
    };
    const auto intent = trinity::intelligence::Intent::fromJson(llmJson);
    CHECK(intent.source == "llm");
    auto registry = makeRegistry();
    IntentValidator validator;
    CHECK(validator.validate(intent, registry).passed());
    IntentRouter router;
    const auto route = router.route(intent, registry);
    CHECK(route.routed);
    CHECK(route.engine == "math");
}

TEST_CASE("new math operations route with full parameters") {
    RequirementParser parser;
    IntentValidator validator;
    auto registry = makeRegistry();
    IntentRouter router;

    const auto linear = parser.parse("Solve linear with a = 2, b = 4");
    REQUIRE(validator.validate(linear.intent, registry).passed());
    const auto linearRoute = router.route(linear.intent, registry);
    CHECK(linearRoute.routed);
    CHECK(linearRoute.engine == "math");
    CHECK(linearRoute.operation == "solve_linear");
    CHECK(linearRoute.toolCall.parameters["a"].get<double>() == doctest::Approx(2.0));

    const auto convert = parser.parse("Convert 10 cm to mm");
    REQUIRE(validator.validate(convert.intent, registry).passed());
    const auto convertRoute = router.route(convert.intent, registry);
    CHECK(convertRoute.routed);
    CHECK(convertRoute.operation == "convert");
    CHECK(convertRoute.toolCall.parameters["to"].get<std::string>() == "mm");

    const auto formula = parser.parse("Formula ohm with V = 12, R = 6");
    REQUIRE(validator.validate(formula.intent, registry).passed());
    const auto formulaRoute = router.route(formula.intent, registry);
    CHECK(formulaRoute.routed);
    CHECK(formulaRoute.operation == "formula");
    CHECK(formulaRoute.toolCall.parameters["inputs"]["V"].get<double>() ==
          doctest::Approx(12.0));

    const auto vars = parser.parse("Calculate 2*x + 5 with x = 10");
    REQUIRE(validator.validate(vars.intent, registry).passed());
    const auto varsRoute = router.route(vars.intent, registry);
    CHECK(varsRoute.routed);
    CHECK(varsRoute.toolCall.parameters["variables"]["x"].get<double>() ==
          doctest::Approx(10.0));
}

TEST_CASE("pcb board intent routes to pcb/create_board") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm x 40 mm PCB");
    REQUIRE(trinity::intelligence::toString(parsed.status) == "VALID");
    IntentValidator validator;
    auto registry = makeRegistry();
    registry.registerEngine(std::make_shared<trinity::engines::PcbEngine>());
    REQUIRE(validator.validate(parsed.intent, registry).passed());
    IntentRouter router;
    const auto route = router.route(parsed.intent, registry);
    CHECK(route.routed);
    CHECK(route.engine == "pcb");
    CHECK(route.operation == "create_board");
    CHECK(route.toolCall.parameters["width_mm"].get<double>() == doctest::Approx(50.0));
}

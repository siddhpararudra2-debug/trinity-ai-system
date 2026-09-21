#include <doctest.h>

#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/MathEngine.hpp"
#include "trinity/intelligence/IntentValidator.hpp"
#include "trinity/intelligence/RequirementParser.hpp"

using trinity::intelligence::Intent;
using trinity::intelligence::IntentValidator;
using trinity::intelligence::ParseStatus;
using trinity::intelligence::RequirementParser;
using trinity::validation::ValidationStatus;

namespace {

trinity::engines::EngineRegistry makeRegistry() {
    trinity::engines::EngineRegistry registry;
    registry.registerEngine(std::make_shared<trinity::engines::MathEngine>());
    registry.registerEngine(std::make_shared<trinity::engines::CadEngine>());
    return registry;
}

}  // namespace

TEST_CASE("valid CAD intent passes validation") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm quadcopter frame with 2 mm thick arms.");
    IntentValidator validator;
    const auto result = validator.validate(parsed.intent);
    CHECK(result.passed());
    CHECK(trinity::validation::toString(result.status) == "VALIDATED");
}

TEST_CASE("valid CAD intent passes capability check against registry") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm quadcopter frame");
    IntentValidator validator;
    auto registry = makeRegistry();
    const auto result = validator.validate(parsed.intent, registry);
    CHECK(result.passed());
}

TEST_CASE("valid math intent passes validation") {
    RequirementParser parser;
    IntentValidator validator;
    auto registry = makeRegistry();

    const auto calc = parser.parse("Calculate 25 * 8");
    CHECK(validator.validate(calc.intent, registry).passed());

    const auto solve = parser.parse("Solve x^2 + 2x + 1");
    CHECK(validator.validate(solve.intent, registry).passed());
}

TEST_CASE("incomplete intent fails validation and never routes") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a drone frame");
    CHECK(trinity::intelligence::toString(parsed.status) == "INCOMPLETE");
    IntentValidator validator;
    const auto result = validator.validate(parsed.intent);
    CHECK_FALSE(result.passed());
    CHECK(trinity::validation::toString(result.status) == "INVALID");
    bool hasCompleteness = false;
    for (const auto& msg : result.messages) {
        if (msg.rule == "intent.completeness") {
            hasCompleteness = true;
            CHECK_FALSE(msg.passed);
        }
    }
    CHECK(hasCompleteness);
}

TEST_CASE("invalid domain fails validation") {
    Intent intent;
    intent.domain = "poetry";
    intent.operation = "generate";
    intent.object = "sonnet";
    intent.status = ParseStatus::Valid;
    IntentValidator validator;
    const auto result = validator.validate(intent);
    CHECK_FALSE(result.passed());
}

TEST_CASE("invalid operation fails validation") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm quadcopter frame");
    Intent tampered = parsed.intent;
    tampered.operation = "teleport";
    IntentValidator validator;
    const auto result = validator.validate(tampered);
    CHECK_FALSE(result.passed());
}

TEST_CASE("out-of-range CAD parameter fails validation") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 5000 mm quadcopter frame");
    IntentValidator validator;
    const auto result = validator.validate(parsed.intent);
    CHECK_FALSE(result.passed());
}

TEST_CASE("non-numeric CAD parameter fails validation") {
    RequirementParser parser;
    const auto parsed = parser.parse("Create a 50 mm quadcopter frame");
    Intent tampered = parsed.intent;
    tampered.parameters["overall_size_mm"] = "huge";
    IntentValidator validator;
    const auto result = validator.validate(tampered);
    CHECK_FALSE(result.passed());
}

TEST_CASE("math intent without expression fails validation") {
    Intent intent;
    intent.domain = "math";
    intent.operation = "evaluate_expression";
    intent.object = "expression";
    intent.status = ParseStatus::Valid;
    IntentValidator validator;
    const auto result = validator.validate(intent);
    CHECK_FALSE(result.passed());
}

TEST_CASE("unsupported CAD object fails capability check but passes shape checks") {
    RequirementParser parser;
    const auto parsed = parser.parse("Generate a 100 mm plate with 4 mm thickness");
    IntentValidator validator;
    // Shape-only validation passes: plate is a well-formed CAD intent.
    CHECK(validator.validate(parsed.intent).passed());
    // Capability validation against the live registry fails: CadEngine
    // only implements quadcopter_frame.
    auto registry = makeRegistry();
    const auto withCaps = validator.validate(parsed.intent, registry);
    CHECK_FALSE(withCaps.passed());
    bool hasCapability = false;
    for (const auto& msg : withCaps.messages) {
        if (msg.rule == "intent.capability") {
            hasCapability = true;
            CHECK_FALSE(msg.passed);
        }
    }
    CHECK(hasCapability);
}

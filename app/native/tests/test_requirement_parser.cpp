#include <doctest.h>

#include "trinity/intelligence/RequirementParser.hpp"

using trinity::intelligence::ParseStatus;
using trinity::intelligence::RequirementParser;
using trinity::intelligence::UnitNormalizer;

TEST_CASE("CAD quadcopter frame request parses with normalized params") {
    RequirementParser parser;
    const auto result = parser.parse("Create a 50 mm quadcopter frame with 2 mm thick arms.");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "cad");
    CHECK(result.intent.operation == "generate");
    CHECK(result.intent.object == "quadcopter_frame");
    REQUIRE(result.intent.parameters.contains("overall_size_mm"));
    CHECK(result.intent.parameters["overall_size_mm"].get<double>() == doctest::Approx(50.0));
    REQUIRE(result.intent.parameters.contains("arm_thickness_mm"));
    CHECK(result.intent.parameters["arm_thickness_mm"].get<double>() == doctest::Approx(2.0));
    CHECK(result.intent.units == "mm");
    CHECK(result.intent.missing.empty());
    CHECK(result.intent.confidence > 0.8);
    CHECK(result.intent.source == "deterministic");
    CHECK_FALSE(result.intent.intentId.empty());
    CHECK_FALSE(result.intent.timestamp.empty());
    CHECK(result.intent.rawRequest == "Create a 50 mm quadcopter frame with 2 mm thick arms.");
    CHECK(result.intent.rawMetadata.contains("original_quantities"));
}

TEST_CASE("CAD plate request parses as valid plate") {
    RequirementParser parser;
    const auto result = parser.parse("Generate a 100 mm plate with 4 mm thickness");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "cad");
    CHECK(result.intent.operation == "generate");
    CHECK(result.intent.object == "plate");
    CHECK(result.intent.parameters["overall_size_mm"].get<double>() == doctest::Approx(100.0));
    CHECK(result.intent.parameters["thickness_mm"].get<double>() == doctest::Approx(4.0));
}

TEST_CASE("math evaluation request parses") {
    RequirementParser parser;
    const auto result = parser.parse("Calculate 25 * 8");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "math");
    CHECK(result.intent.object == "expression");
    CHECK(result.intent.parameters["expression"].get<std::string>() == "25 * 8");
    CHECK((result.intent.operation == "evaluate_expression" || result.intent.operation == "evaluate"));
}

TEST_CASE("math solve request parses") {
    RequirementParser parser;
    const auto result = parser.parse("Solve x^2 + 2x + 1");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "math");
    CHECK(result.intent.operation == "solve");
    CHECK(result.intent.parameters["expression"].get<std::string>() == "x^2 + 2x + 1");
}

TEST_CASE("bare quadcopter frame without verb parses") {
    RequirementParser parser;
    const auto result = parser.parse("Create a 50 mm quadcopter frame");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.object == "quadcopter_frame");
    CHECK(result.intent.parameters["overall_size_mm"].get<double>() == doctest::Approx(50.0));
}

TEST_CASE("explicit engine request pins domain") {
    RequirementParser parser;
    const auto result = parser.parse("Using math engine calculate 2 + 2");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "math");
    CHECK(result.intent.parameters["expression"].get<std::string>() == "2 + 2");
}

TEST_CASE("unit normalization converts to canonical form") {
    const auto mm = UnitNormalizer::normalize(50.0, "mm");
    CHECK(mm.ok);
    CHECK(mm.canonicalUnit == "mm");
    CHECK(mm.normalizedValue == doctest::Approx(50.0));

    const auto cm = UnitNormalizer::normalize(5.0, "cm");
    CHECK(cm.ok);
    CHECK(cm.canonicalUnit == "mm");
    CHECK(cm.normalizedValue == doctest::Approx(50.0));

    const auto m = UnitNormalizer::normalize(0.05, "m");
    CHECK(m.ok);
    CHECK(m.normalizedValue == doctest::Approx(50.0));

    const auto inch = UnitNormalizer::normalize(1.0, "in");
    CHECK(inch.ok);
    CHECK(inch.canonicalUnit == "mm");
    CHECK(inch.normalizedValue == doctest::Approx(25.4));

    const auto rad = UnitNormalizer::normalize(3.141592653589793, "rad");
    CHECK(rad.ok);
    CHECK(rad.canonicalUnit == "deg");
    CHECK(rad.normalizedValue == doctest::Approx(180.0));

    const auto kg = UnitNormalizer::normalize(1.0, "kg");
    CHECK(kg.ok);
    CHECK(kg.canonicalUnit == "g");
    CHECK(kg.normalizedValue == doctest::Approx(1000.0));

    const auto bad = UnitNormalizer::normalize(1.0, "furlong");
    CHECK_FALSE(bad.ok);

    // End-to-end: cm input normalizes into Intent parameters.
    RequirementParser parser;
    const auto result = parser.parse("Create a 5 cm quadcopter frame");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.parameters["overall_size_mm"].get<double>() == doctest::Approx(50.0));
    CHECK(result.intent.rawMetadata["original_quantities"][0]["unit"] == "cm");
    CHECK(result.intent.rawMetadata["original_quantities"][0]["normalized_unit"] == "mm");
}

TEST_CASE("missing parameters yield INCOMPLETE without invented values") {
    RequirementParser parser;
    const auto result = parser.parse("Create a drone frame");
    CHECK(trinity::intelligence::toString(result.status) == "INCOMPLETE");
    CHECK(result.intent.domain == "cad");
    CHECK_FALSE(result.intent.missing.empty());
    CHECK(result.intent.parameters.find("overall_size_mm") == result.intent.parameters.end());
    CHECK(result.intent.confidence < 0.8);
}

TEST_CASE("ambiguous requests yield AMBIGUOUS") {
    RequirementParser parser;
    const auto result = parser.parse("Create a 50 mm frame");
    CHECK(trinity::intelligence::toString(result.status) == "AMBIGUOUS");
    REQUIRE(result.intent.missing.size() == 1);
    CHECK(result.intent.missing[0] == "object");
}

TEST_CASE("invalid requests yield INVALID") {
    RequirementParser parser;
    const auto empty = parser.parse("");
    CHECK(trinity::intelligence::toString(empty.status) == "INVALID");
    CHECK_FALSE(empty.errors.empty());

    const auto gibberish = parser.parse("Write a poem about the sea");
    CHECK(trinity::intelligence::toString(gibberish.status) == "INVALID");
    CHECK_FALSE(gibberish.errors.empty());
}

TEST_CASE("priority and outputs are detected") {
    RequirementParser parser;
    const auto urgent = parser.parse("Urgently create a 50 mm quadcopter frame");
    CHECK(urgent.intent.priority == "high");

    const auto stl = parser.parse("Generate a 50 mm quadcopter frame with STL output");
    bool hasStl = false;
    for (const auto& item : stl.intent.outputs) {
        if (item.is_string() && item.get<std::string>() == "stl") {
            hasStl = true;
        }
    }
    CHECK(hasStl);
}

TEST_CASE("intent serialization round-trips structured fields") {
    RequirementParser parser;
    const auto result = parser.parse("Create a 50 mm quadcopter frame with 2 mm thick arms.");
    const auto back =
        trinity::intelligence::Intent::fromJson(result.intent.toJson());
    CHECK(back.domain == "cad");
    CHECK(back.operation == "generate");
    CHECK(back.object == "quadcopter_frame");
    CHECK(back.parameters["overall_size_mm"].get<double>() == doctest::Approx(50.0));
    CHECK(back.parameters["arm_thickness_mm"].get<double>() == doctest::Approx(2.0));
    CHECK(back.rawRequest == result.intent.rawRequest);
    CHECK(trinity::intelligence::toString(back.status) == "VALID");
    CHECK(back.source == "deterministic");
}

TEST_CASE("math variables parse from with-bindings") {
    RequirementParser parser;
    const auto result = parser.parse("Calculate 2*x + 5 with x = 10");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "math");
    CHECK(result.intent.operation == "evaluate");
    CHECK(result.intent.parameters["expression"].get<std::string>() == "2*x + 5");
    REQUIRE(result.intent.parameters.contains("variables"));
    CHECK(result.intent.parameters["variables"]["x"].get<double>() == doctest::Approx(10.0));
}

TEST_CASE("math convert request parses") {
    RequirementParser parser;
    const auto result = parser.parse("Convert 10 cm to mm");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "math");
    CHECK(result.intent.operation == "convert");
    CHECK(result.intent.parameters["value"].get<double>() == doctest::Approx(10.0));
    CHECK(result.intent.parameters["from"].get<std::string>() == "cm");
    CHECK(result.intent.parameters["to"].get<std::string>() == "mm");
}

TEST_CASE("math formula request parses") {
    RequirementParser parser;
    const auto result = parser.parse("Formula ohm with V = 12, R = 6");
    CHECK(trinity::intelligence::toString(result.status) == "VALID");
    CHECK(result.intent.domain == "math");
    CHECK(result.intent.operation == "formula");
    CHECK(result.intent.parameters["name"].get<std::string>() == "ohm");
    CHECK(result.intent.parameters["inputs"]["V"].get<double>() == doctest::Approx(12.0));
    CHECK(result.intent.parameters["inputs"]["R"].get<double>() == doctest::Approx(6.0));
}

TEST_CASE("math coefficient solvers parse") {
    RequirementParser parser;
    const auto linear = parser.parse("Solve linear with a = 2, b = 4");
    CHECK(trinity::intelligence::toString(linear.status) == "VALID");
    CHECK(linear.intent.operation == "solve_linear");
    CHECK(linear.intent.parameters["a"].get<double>() == doctest::Approx(2.0));
    CHECK(linear.intent.parameters["b"].get<double>() == doctest::Approx(4.0));

    const auto quad = parser.parse("Solve quadratic with a = 1, b = -5, c = 6");
    CHECK(trinity::intelligence::toString(quad.status) == "VALID");
    CHECK(quad.intent.operation == "solve_quadratic");
    CHECK(quad.intent.parameters["c"].get<double>() == doctest::Approx(6.0));
}

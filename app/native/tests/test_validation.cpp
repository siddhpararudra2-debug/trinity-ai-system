#include <doctest.h>

#include "trinity/validation/ValidationResult.hpp"

TEST_CASE("validation states are distinct") {
    using trinity::validation::ValidationStatus;
    CHECK(trinity::validation::toString(ValidationStatus::Generated) == "GENERATED");
    CHECK(trinity::validation::toString(ValidationStatus::Validated) == "VALIDATED");
    CHECK(trinity::validation::toString(ValidationStatus::Verified) == "VERIFIED");
    CHECK(trinity::validation::toString(ValidationStatus::Invalid) == "INVALID");
    // FAILED stays a backward-compatible alias of INVALID.
    CHECK(trinity::validation::toString(trinity::validation::fromString("FAILED")) ==
          "INVALID");
    CHECK(trinity::validation::toString(trinity::validation::fromString("INVALID")) ==
          "INVALID");

    trinity::validation::ValidationResult ok;
    ok.status = ValidationStatus::Validated;
    CHECK(ok.passed());
    trinity::validation::ValidationResult verified;
    verified.status = ValidationStatus::Verified;
    CHECK(verified.passed());
    trinity::validation::ValidationResult gen;
    gen.status = ValidationStatus::Generated;
    CHECK_FALSE(gen.passed());
    trinity::validation::ValidationResult bad;
    bad.status = ValidationStatus::Invalid;
    CHECK_FALSE(bad.passed());
}

TEST_CASE("validation messages carry rule, severity and details") {
    trinity::validation::ValidationMessage msg;
    msg.rule = "math.value_finite";
    msg.severity = trinity::validation::Severity::Error;
    msg.passed = false;
    msg.message = "not finite";
    msg.details = {{"value", 0}};
    const auto back = trinity::validation::ValidationMessage::fromJson(msg.toJson());
    CHECK(back.rule == "math.value_finite");
    CHECK(trinity::validation::toString(back.severity) == "ERROR");
    CHECK_FALSE(back.passed);
    CHECK(trinity::validation::toString(trinity::validation::Severity::Info) == "INFO");
    CHECK(trinity::validation::toString(trinity::validation::Severity::Warning) ==
          "WARNING");
    CHECK(trinity::validation::toString(trinity::validation::Severity::Error) == "ERROR");
}

TEST_CASE("validation result round-trips messages") {
    trinity::validation::ValidationResult result;
    result.status = trinity::validation::ValidationStatus::Invalid;
    result.operation = "evaluate_expression";
    trinity::validation::ValidationMessage msg;
    msg.rule = "r1";
    msg.severity = trinity::validation::Severity::Warning;
    msg.passed = false;
    msg.message = "check failed";
    result.addMessage(msg);
    const auto back = trinity::validation::ValidationResult::fromJson(result.toJson());
    CHECK_FALSE(back.passed());
    REQUIRE(back.messages.size() == 1);
    CHECK(back.messages[0].rule == "r1");
}

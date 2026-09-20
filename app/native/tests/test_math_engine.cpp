#include <doctest.h>

#include "trinity/engines/MathEngine.hpp"

TEST_CASE("math evaluates operator precedence") {
    CHECK(trinity::engines::MathEngine::evaluateExpression("2 + 3 * 4") == 14.0);
    CHECK(trinity::engines::MathEngine::evaluateExpression("(2 + 3) * 4") == 20.0);
    CHECK(trinity::engines::MathEngine::evaluateExpression("10 / 4") ==
          doctest::Approx(2.5));
    CHECK(trinity::engines::MathEngine::evaluateExpression("-3 + 8") == 5.0);
}

TEST_CASE("math rejects invalid input with structured errors") {
    CHECK_THROWS_AS(trinity::engines::MathEngine::evaluateExpression(""),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(trinity::engines::MathEngine::evaluateExpression("2 + foo"),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(trinity::engines::MathEngine::evaluateExpression("2 + * 3"),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(trinity::engines::MathEngine::evaluateExpression("1 / 0"),
                    trinity::core::RequestValidationError);
}

TEST_CASE("math engine executes and validates") {
    trinity::engines::MathEngine engine;
    CHECK(engine.name() == "math");
    CHECK_FALSE(engine.capabilities().empty());

    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "evaluate_expression";
    req.parameters = {{"expression", "2 + 3 * 4"}};
    const auto result = engine.execute(req);
    CHECK(result.success);
    CHECK(result.result.value("value", 0.0) == 14.0);
    REQUIRE(result.validation.has_value());
    CHECK(result.validation->passed());

    trinity::engines::EngineRequest bad;
    bad.engine = "math";
    bad.operation = "integrate_symbolic";
    CHECK_THROWS_AS(engine.execute(bad), trinity::core::CapabilityUnavailableError);
}

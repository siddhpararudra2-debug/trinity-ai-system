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

TEST_CASE("math engine executes and validates") {    trinity::engines::MathEngine engine;
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

TEST_CASE("math evaluates variables, functions and constants") {
    CHECK(trinity::engines::MathEngine::evaluateWithVariables("x * 2 + 1", {{"x", 3.0}}) ==
          7.0);
    CHECK(trinity::engines::MathEngine::evaluateWithVariables("sin(pi / 2)", {}) ==
          doctest::Approx(1.0));
    CHECK(trinity::engines::MathEngine::evaluateWithVariables("sqrt(16) + abs(-3)", {}) ==
          7.0);
    CHECK(trinity::engines::MathEngine::evaluateWithVariables("2 ^ 10", {}) == 1024.0);
    CHECK(trinity::engines::MathEngine::evaluateWithVariables("e ^ 0", {}) ==
          doctest::Approx(1.0));
    CHECK_THROWS_AS(trinity::engines::MathEngine::evaluateWithVariables("y + 1", {}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(trinity::engines::MathEngine::evaluateWithVariables("bogus(1)", {}),
                    trinity::core::RequestValidationError);

    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "evaluate";
    req.parameters = {{"expression", "x^2 + 2*x + 1"}, {"variables", {{"x", 3.0}}}};
    const auto result = engine.execute(req);
    CHECK(result.success);
    CHECK(result.result.value("value", 0.0) == doctest::Approx(16.0));
    REQUIRE(result.validation.has_value());
    CHECK(result.validation->passed());
}

TEST_CASE("math solves single-variable equations") {
    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest linear;
    linear.engine = "math";
    linear.operation = "solve";
    linear.parameters = {{"expression", "2*x + 4 = 0"}};
    const auto linResult = engine.execute(linear);
    CHECK(linResult.success);
    REQUIRE(linResult.result.contains("solutions"));
    REQUIRE(linResult.result["solutions"].size() == 1);
    CHECK(linResult.result["solutions"][0].get<double>() == doctest::Approx(-2.0));
    CHECK(linResult.result.value("solved_for", "") == "x");
    REQUIRE(linResult.validation.has_value());
    CHECK(linResult.validation->passed());

    trinity::engines::EngineRequest quad;
    quad.engine = "math";
    quad.operation = "solve";
    quad.parameters = {{"expression", "x^2 - 4 = 0"}};
    const auto quadResult = engine.execute(quad);
    CHECK(quadResult.success);
    REQUIRE(quadResult.result["solutions"].size() == 2);
    CHECK(quadResult.result["solutions"][0].get<double>() == doctest::Approx(-2.0));
    CHECK(quadResult.result["solutions"][1].get<double>() == doctest::Approx(2.0));

    trinity::engines::EngineRequest withVars;
    withVars.engine = "math";
    withVars.operation = "solve";
    withVars.parameters = {{"expression", "x + y = 10"},
                           {"variables", {{"y", 3.0}}}};
    const auto varsResult = engine.execute(withVars);
    CHECK(varsResult.success);
    CHECK(varsResult.result["solutions"][0].get<double>() == doctest::Approx(7.0));
}

TEST_CASE("math solve rejects bad input with structured errors") {
    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest noEq;
    noEq.engine = "math";
    noEq.operation = "solve";
    noEq.parameters = {{"expression", "2 + 2"}};
    CHECK_FALSE(engine.execute(noEq).success);

    trinity::engines::EngineRequest multi;
    multi.engine = "math";
    multi.operation = "solve";
    multi.parameters = {{"expression", "x + y = 10"}};
    const auto multiResult = engine.execute(multi);
    CHECK_FALSE(multiResult.success);
    REQUIRE_FALSE(multiResult.errors.empty());
    CHECK(multiResult.errors.front().value("code", "") == "request_validation_error");

    trinity::engines::EngineRequest wrongTarget;
    wrongTarget.engine = "math";
    wrongTarget.operation = "solve";
    wrongTarget.parameters = {{"expression", "x + 1 = 0"}, {"solve_for", "z"}};
    CHECK_FALSE(engine.execute(wrongTarget).success);
}

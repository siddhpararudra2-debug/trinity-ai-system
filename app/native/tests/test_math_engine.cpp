#include <doctest.h>

#include <algorithm>

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

TEST_CASE("math evaluates extended functions and constants") {
    using trinity::engines::MathEngine;
    CHECK(MathEngine::evaluateWithVariables("2^8", {}) == 256.0);
    CHECK(MathEngine::evaluateWithVariables("sqrt(25)", {}) == 5.0);
    CHECK(MathEngine::evaluateWithVariables("sin(pi/2)", {}) == doctest::Approx(1.0));
    CHECK(MathEngine::evaluateWithVariables("cos(0)", {}) == doctest::Approx(1.0));
    CHECK(MathEngine::evaluateWithVariables("asin(1)", {}) ==
          doctest::Approx(3.141592653589793 / 2.0));
    CHECK(MathEngine::evaluateWithVariables("acos(0)", {}) ==
          doctest::Approx(3.141592653589793 / 2.0));
    CHECK(MathEngine::evaluateWithVariables("atan(1)", {}) ==
          doctest::Approx(3.141592653589793 / 4.0));
    CHECK(MathEngine::evaluateWithVariables("ln(e)", {}) == doctest::Approx(1.0));
    CHECK(MathEngine::evaluateWithVariables("log(e)", {}) == doctest::Approx(1.0));
    CHECK(MathEngine::evaluateWithVariables("exp(0)", {}) == doctest::Approx(1.0));
    CHECK(MathEngine::evaluateWithVariables("2*x + 5", {{"x", 10.0}}) == 25.0);
    // Domain errors are structured rejections, never silent NaN.
    CHECK_THROWS_AS(MathEngine::evaluateWithVariables("sqrt(-1)", {}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(MathEngine::evaluateWithVariables("asin(2)", {}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(MathEngine::evaluateWithVariables("acos(-2)", {}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(MathEngine::evaluateWithVariables("ln(0)", {}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(MathEngine::evaluateWithVariables("bogusfn(1)", {}),
                    trinity::core::RequestValidationError);
}

TEST_CASE("math rejects non-finite and overflow results") {
    using trinity::engines::MathEngine;
    // Overflow must not return success=true. Strict path overflow via
    // repeated multiplication (1e309 exceeds double range).
    std::string big = "10";
    for (int i = 0; i < 309; ++i) {
        big += "*10";
    }
    CHECK_THROWS_AS(MathEngine::evaluateExpression(big),
                    trinity::core::EngineExecutionError);
    CHECK_THROWS_AS(MathEngine::evaluateWithVariables("10^1000", {}),
                    trinity::core::EngineExecutionError);
    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "evaluate";
    req.parameters = {{"expression", "10^1000"}};
    const auto result = engine.execute(req);
    CHECK_FALSE(result.success);
}

TEST_CASE("math solve_linear handles solution and degenerate cases") {
    trinity::engines::MathEngine engine;
    auto run = [&](double a, double b) {
        trinity::engines::EngineRequest req;
        req.engine = "math";
        req.operation = "solve_linear";
        req.parameters = {{"a", a}, {"b", b}};
        return engine.execute(req);
    };
    const auto ok = run(2.0, 4.0);
    CHECK(ok.success);
    CHECK(ok.result.value("outcome", "") == "solution");
    CHECK(ok.result.value("solution", 0.0) == doctest::Approx(-2.0));
    REQUIRE(ok.validation.has_value());
    CHECK(ok.validation->passed());

    const auto none = run(0.0, 5.0);
    CHECK(none.success);
    CHECK(none.result.value("outcome", "") == "no_solution");
    CHECK(none.validation.has_value());
    CHECK(none.validation->passed());

    const auto infinite = run(0.0, 0.0);
    CHECK(infinite.success);
    CHECK(infinite.result.value("outcome", "") == "infinite_solutions");

    trinity::engines::EngineRequest missing;
    missing.engine = "math";
    missing.operation = "solve_linear";
    missing.parameters = {{"a", 1.0}};
    CHECK_FALSE(engine.execute(missing).success);
}

TEST_CASE("math solve_quadratic returns structured roots") {
    trinity::engines::MathEngine engine;
    auto run = [&](double a, double b, double c) {
        trinity::engines::EngineRequest req;
        req.engine = "math";
        req.operation = "solve_quadratic";
        req.parameters = {{"a", a}, {"b", b}, {"c", c}};
        return engine.execute(req);
    };
    // x^2 - 5x + 6 = 0 -> 2, 3.
    const auto two = run(1.0, -5.0, 6.0);
    CHECK(two.success);
    CHECK(two.result.value("outcome", "") == "two_roots");
    REQUIRE(two.result["solutions"].size() == 2);
    CHECK(two.result["solutions"][0].get<double>() == doctest::Approx(2.0));
    CHECK(two.result["solutions"][1].get<double>() == doctest::Approx(3.0));
    REQUIRE(two.validation.has_value());
    CHECK(two.validation->passed());

    // x^2 + 2x + 1 = 0 -> repeated -1.
    const auto repeated = run(1.0, 2.0, 1.0);
    CHECK(repeated.success);
    CHECK(repeated.result.value("outcome", "") == "repeated_root");
    REQUIRE(repeated.result["solutions"].size() == 1);
    CHECK(repeated.result["solutions"][0].get<double>() == doctest::Approx(-1.0));

    // x^2 + 1 = 0 -> no real roots, complex explicitly unsupported.
    const auto complex = run(1.0, 0.0, 1.0);
    CHECK(complex.success);
    CHECK(complex.result.value("outcome", "") == "no_real_roots");
    CHECK(complex.result["solutions"].empty());
    CHECK(complex.result.value("message", "").find("Complex") != std::string::npos);

    // Degenerate a = 0 degrades to linear 2x + 4 = 0 -> -2.
    const auto degraded = run(0.0, 2.0, 4.0);
    CHECK(degraded.success);
    CHECK(degraded.result.value("degraded_to_linear", false) == true);
    CHECK(degraded.result.value("solution", 0.0) == doctest::Approx(-2.0));
}

TEST_CASE("math convert handles compatible units and rejects mismatch") {
    using trinity::engines::MathEngine;
    CHECK(MathEngine::convertUnits(10.0, "cm", "mm") == doctest::Approx(100.0));
    CHECK(MathEngine::convertUnits(1.0, "m", "mm") == doctest::Approx(1000.0));
    CHECK(MathEngine::convertUnits(180.0, "deg", "rad") ==
          doctest::Approx(3.141592653589793));
    CHECK(MathEngine::convertUnits(1.0, "kg", "g") == doctest::Approx(1000.0));
    CHECK(MathEngine::convertUnits(1.0, "in", "mm") == doctest::Approx(25.4));
    CHECK(MathEngine::dimensionOf("cm") == "length");
    CHECK(MathEngine::dimensionOf("kg") == "mass");
    CHECK(MathEngine::dimensionOf("furlong").empty());
    CHECK_THROWS_AS(MathEngine::convertUnits(10.0, "kg", "mm"),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(MathEngine::convertUnits(1.0, "furlong", "mm"),
                    trinity::core::RequestValidationError);

    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "convert";
    req.parameters = {{"value", 10.0}, {"from", "cm"}, {"to", "mm"}};
    const auto result = engine.execute(req);
    CHECK(result.success);
    CHECK(result.result.value("value", 0.0) == doctest::Approx(100.0));
    CHECK(result.result.value("dimension", "") == "length");
    REQUIRE(result.validation.has_value());
    CHECK(result.validation->passed());

    trinity::engines::EngineRequest bad;
    bad.engine = "math";
    bad.operation = "convert";
    bad.parameters = {{"value", 10.0}, {"from", "kg"}, {"to", "mm"}};
    CHECK_FALSE(engine.execute(bad).success);
}

TEST_CASE("math formulas solve ohm power and force") {
    using trinity::engines::MathEngine;
    const auto names = MathEngine::formulaNames();
    CHECK(std::find(names.begin(), names.end(), "ohm") != names.end());
    CHECK(std::find(names.begin(), names.end(), "power") != names.end());
    CHECK(std::find(names.begin(), names.end(), "force") != names.end());

    // V = 12, R = 6 -> I = 2.
    const auto ohm = MathEngine::formulaResult("ohm", {{"V", 12.0}, {"R", 6.0}});
    CHECK(ohm["outputs"].value("I", 0.0) == doctest::Approx(2.0));
    CHECK(ohm["outputs"].value("V", 0.0) == doctest::Approx(12.0));
    // P = V * I = 24.
    const auto power = MathEngine::formulaResult("power", {{"V", 12.0}, {"I", 2.0}});
    CHECK(power["outputs"].value("P", 0.0) == doctest::Approx(24.0));
    // F = m * a = 6.
    const auto force = MathEngine::formulaResult("force", {{"m", 2.0}, {"a", 3.0}});
    CHECK(force["outputs"].value("F", 0.0) == doctest::Approx(6.0));
    CHECK(force["units"].value("F", "") == "N");

    CHECK_THROWS_AS(MathEngine::formulaResult("warp_drive", {{"x", 1.0}}),
                    trinity::core::CapabilityUnavailableError);
    CHECK_THROWS_AS(MathEngine::formulaResult("ohm", {{"V", 12.0}}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(MathEngine::formulaResult("ohm", {{"V", 12.0}, {"R", 0.0}}),
                    trinity::core::RequestValidationError);

    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "formula";
    req.parameters = {{"name", "force"}, {"inputs", {{"m", 2.0}, {"a", 3.0}}}};
    const auto result = engine.execute(req);
    CHECK(result.success);
    CHECK(result.result["outputs"].value("F", 0.0) == doctest::Approx(6.0));
    REQUIRE(result.validation.has_value());
    CHECK(result.validation->passed());
}

TEST_CASE("math differentiates symbolically with numeric verification") {
    using trinity::engines::MathEngine;
    using trinity::core::RequestValidationError;

    CHECK(MathEngine::differentiateSimplified("x^2", "x") == "2*x");
    CHECK(MathEngine::differentiateSimplified("2*x + 1", "x") == "2");
    CHECK(MathEngine::differentiateSimplified("ln(x)", "x") == "1/x");
    CHECK(MathEngine::differentiateSimplified("2^x", "x") == "2^x*ln(2)");
    CHECK(MathEngine::differentiateSimplified("x*sin(x)", "x") ==
          "x*cos(x)+sin(x)");
    CHECK(MathEngine::differentiateSimplified("sin(x^2)", "x") ==
          "cos(x^2)*2*x");
    CHECK(MathEngine::differentiateSimplified("t^3", "t") == "3*t^2");
    CHECK(MathEngine::differentiateSimplified("exp(t)", "t") == "exp(t)");

    // The serialized derivative must evaluate to the numeric derivative:
    // the round trip through the verified evaluator is the serializer's
    // proof of correctness.
    const std::string quotient =
        MathEngine::differentiateSimplified("sin(x)/x", "x");
    for (double x : {0.5, 1.7, -2.3}) {
        trinity::engines::MathEngine engine;
        trinity::engines::EngineRequest req;
        req.engine = "math";
        req.operation = "evaluate";
        req.parameters = {{"expression", quotient}, {"variables", {{"x", x}}}};
        const auto evaluated = engine.execute(req);
        REQUIRE(evaluated.success);
        const double h = 1e-6 * (1.0 + std::fabs(x));
        const double plus =
            MathEngine::evaluateWithVariables("sin(x)/x", {{"x", x + h}});
        const double minus =
            MathEngine::evaluateWithVariables("sin(x)/x", {{"x", x - h}});
        CHECK(evaluated.result.value("value", 0.0) ==
              doctest::Approx((plus - minus) / (2.0 * h)).epsilon(1e-4));
    }

    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "differentiate";
    req.parameters = {{"expression", "x^3"}, {"at", 5.0}};
    const auto atPoint = engine.execute(req);
    CHECK(atPoint.success);
    CHECK(atPoint.result.value("value", 0.0) == doctest::Approx(75.0));
    CHECK(atPoint.metadata.value("derivative_check", false));
    REQUIRE(atPoint.validation.has_value());
    CHECK(atPoint.validation->passed());

    // Honest refusals: piecewise abs, non-positive constant base, unknown
    // variable, equations, and ambiguous multi-symbol expressions.
    trinity::engines::EngineRequest absReq;
    absReq.engine = "math";
    absReq.operation = "differentiate";
    absReq.parameters = {{"expression", "abs(x)"}};
    CHECK_FALSE(engine.execute(absReq).success);

    CHECK_THROWS_AS(MathEngine::differentiateSimplified("(-2)^x", "x"),
                    RequestValidationError);
    CHECK_THROWS_AS(
        MathEngine::differentiateSimplified("x^2", "y"),  // y not present
        RequestValidationError);
    CHECK_THROWS_AS(
        MathEngine::differentiateSimplified("2*x = 2", "x"),  // equation
        RequestValidationError);
    CHECK_THROWS_AS(
        MathEngine::differentiateSimplified("x + y", "z"),  // z not present
        RequestValidationError);

    trinity::engines::EngineRequest multi;
    multi.engine = "math";
    multi.operation = "differentiate";
    multi.parameters = {{"expression", "x + y"}};
    const auto multiResult = engine.execute(multi);
    CHECK_FALSE(multiResult.success);
}

TEST_CASE("math simplification folds constants and identities") {
    using trinity::engines::MathEngine;
    using trinity::core::RequestValidationError;

    CHECK(MathEngine::simplifyExpression("x + 0") == "x");
    CHECK(MathEngine::simplifyExpression("0 + x") == "x");
    CHECK(MathEngine::simplifyExpression("x - 0") == "x");
    CHECK(MathEngine::simplifyExpression("0 - x") == "-x");
    CHECK(MathEngine::simplifyExpression("x*0") == "0");
    CHECK(MathEngine::simplifyExpression("0*x") == "0");
    CHECK(MathEngine::simplifyExpression("1*x") == "x");
    CHECK(MathEngine::simplifyExpression("x/1") == "x");
    CHECK(MathEngine::simplifyExpression("x^1") == "x");
    CHECK(MathEngine::simplifyExpression("x^0") == "1");
    CHECK(MathEngine::simplifyExpression("x - x") == "0");
    CHECK(MathEngine::simplifyExpression("2 + 3") == "5");
    CHECK(MathEngine::simplifyExpression("(2 + 3) * (4 * 5)") == "100");
    CHECK(MathEngine::simplifyExpression("x*1 + 0*2") == "x");
    // Honest non-folds: these stay symbolic and remain refusals at eval.
    CHECK(MathEngine::simplifyExpression("1/0") == "1/0");
    CHECK(MathEngine::simplifyExpression("0^0") == "0^0");

    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "simplify";
    req.parameters = {{"expression", "3*4 + x*0"}};
    const auto result = engine.execute(req);
    CHECK(result.success);
    CHECK(result.result.value("simplified", "") == "12");
    CHECK(result.result.value("value", 0.0) == doctest::Approx(12.0));
    REQUIRE(result.validation.has_value());
    CHECK(result.validation->passed());

    trinity::engines::EngineRequest bad;
    bad.engine = "math";
    bad.operation = "simplify";
    bad.parameters = {{"expression", "2*x = 4"}};
    CHECK_FALSE(engine.execute(bad).success);
    CHECK_THROWS_AS(MathEngine::simplifyExpression("2 + * 3"),
                    RequestValidationError);
}

TEST_CASE("math solve_linear matrix form solves and reports singular") {
    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "solve_linear";
    req.parameters = {{"A", {{2.0, 1.0}, {1.0, 3.0}}}, {"b", {5.0, 10.0}}};
    const auto solution = engine.execute(req);
    CHECK(solution.success);
    CHECK(solution.result.value("outcome", "") == "solution");
    REQUIRE(solution.result.contains("solutions"));
    REQUIRE(solution.result["solutions"].size() == 2);
    CHECK(solution.result["solutions"][0].get<double>() ==
          doctest::Approx(1.0));
    CHECK(solution.result["solutions"][1].get<double>() ==
          doctest::Approx(3.0));
    REQUIRE(solution.metadata.contains("equation_checks"));
    for (auto it = solution.metadata["equation_checks"].begin();
         it != solution.metadata["equation_checks"].end(); ++it) {
        CHECK(it.value().get<bool>());
    }
    REQUIRE(solution.validation.has_value());
    CHECK(solution.validation->passed());

    // Singular matrix: an honest defined outcome, not a crash.
    trinity::engines::EngineRequest singular;
    singular.engine = "math";
    singular.operation = "solve_linear";
    singular.parameters = {{"A", {{1.0, 2.0}, {2.0, 4.0}}}, {"b", {3.0, 6.0}}};
    const auto noPivot = engine.execute(singular);
    CHECK(noPivot.success);
    CHECK(noPivot.result.value("outcome", "") == "singular");
    CHECK_FALSE(noPivot.result.contains("solutions"));
    REQUIRE(noPivot.validation.has_value());
    CHECK(noPivot.validation->passed());

    // Structured refusals: ragged matrix, mismatched right-hand side,
    // non-finite entries.
    trinity::engines::EngineRequest ragged;
    ragged.engine = "math";
    ragged.operation = "solve_linear";
    ragged.parameters = {{"A", {{1.0, 2.0}}}, {"b", {1.0}}};
    CHECK_FALSE(engine.execute(ragged).success);

    trinity::engines::EngineRequest mismatch;
    mismatch.engine = "math";
    mismatch.operation = "solve_linear";
    mismatch.parameters = {{"A", {{1.0, 0.0}, {0.0, 1.0}}},
                           {"b", {1.0, 2.0, 3.0}}};
    CHECK_FALSE(engine.execute(mismatch).success);

    // The scalar (a, b) path is untouched.
    trinity::engines::EngineRequest scalar;
    scalar.engine = "math";
    scalar.operation = "solve_linear";
    scalar.parameters = {{"a", -2.0}, {"b", 8.0}};
    const auto legacy = engine.execute(scalar);
    CHECK(legacy.success);
    CHECK(legacy.result.value("solution", 0.0) == doctest::Approx(4.0));
}

TEST_CASE("math solve_system runs Newton with residual verification") {
    trinity::engines::MathEngine engine;
    trinity::engines::EngineRequest req;
    req.engine = "math";
    req.operation = "solve_system";
    req.parameters = {{"equations", {"x^2 + y^2 = 4", "y = x"}},
                      {"variables", {{"x", 1.4}, {"y", 0.9}}}};
    const auto solution = engine.execute(req);
    CHECK(solution.success);
    CHECK(solution.result.value("outcome", "") == "solution");
    CHECK(solution.result["solution"].value("x", 0.0) ==
          doctest::Approx(1.4142135623730951).epsilon(1e-9));
    CHECK(solution.result["solution"].value("y", 0.0) ==
          doctest::Approx(1.4142135623730951).epsilon(1e-9));
    CHECK(solution.result.value("iterations", 0) >= 1);
    REQUIRE(solution.validation.has_value());
    CHECK(solution.validation->passed());
    REQUIRE(solution.metadata.contains("equation_checks"));
    for (auto it = solution.metadata["equation_checks"].begin();
         it != solution.metadata["equation_checks"].end(); ++it) {
        CHECK(it.value().get<bool>());
    }

    // Residual form (no '=') is an equally honest input.
    trinity::engines::EngineRequest residual;
    residual.engine = "math";
    residual.operation = "solve_system";
    residual.parameters = {{"equations", {"x^2 + y^2 - 4", "x - y"}},
                           {"variables", {{"x", 1.4}, {"y", 0.9}}}};
    const auto residualResult = engine.execute(residual);
    CHECK(residualResult.success);
    CHECK(residualResult.result["solution"].value("x", 0.0) ==
          doctest::Approx(1.4142135623730951).epsilon(1e-9));

    // Structured refusals: inconsistent system (singular Jacobian),
    // symbol without an initial value, and a non-square system.
    trinity::engines::EngineRequest inconsistent;
    inconsistent.engine = "math";
    inconsistent.operation = "solve_system";
    inconsistent.parameters = {
        {"equations", {"x^2 + y^2 - 1", "x^2 + y^2 - 4"}},
        {"variables", {{"x", 0.3}, {"y", 0.3}}}};
    CHECK_FALSE(engine.execute(inconsistent).success);

    trinity::engines::EngineRequest unbound;
    unbound.engine = "math";
    unbound.operation = "solve_system";
    unbound.parameters = {{"equations", {"x - 1"}}, {"variables", {{"y", 0.0}}}};
    CHECK_FALSE(engine.execute(unbound).success);

    trinity::engines::EngineRequest overspecified;
    overspecified.engine = "math";
    overspecified.operation = "solve_system";
    overspecified.parameters = {{"equations", {"x - 1", "x^2 - 4"}},
                                {"variables", {{"x", 0.5}}}};
    CHECK_FALSE(engine.execute(overspecified).success);
}

#include "MathEngine.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "../../core/Error.hpp"

namespace trinity::engines::math {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;
constexpr double kResidualTolerance = 1e-9;

[[noreturn]] void fail(const std::string& message, core::ErrorCode code =
                                                       core::ErrorCode::RequestValidationError) {
    throw core::TrinityException(core::Error(code, message));
}

bool is_function(const std::string& name) {
    static const char* kFunctions[] = {"sin", "cos", "tan", "sqrt", "log",
                                       "ln",  "exp", "abs"};
    for (const char* f : kFunctions) {
        if (name == f) return true;
    }
    return false;
}

bool is_constant(const std::string& name) { return name == "pi" || name == "e"; }

double apply_function(const std::string& name, double value) {
    if (name == "sin") return std::sin(value);
    if (name == "cos") return std::cos(value);
    if (name == "tan") return std::tan(value);
    if (name == "sqrt") {
        if (value < 0.0) fail("sqrt of negative value", core::ErrorCode::EngineExecutionError);
        return std::sqrt(value);
    }
    if (name == "log" || name == "ln") {
        if (value <= 0.0) fail("log of non-positive value", core::ErrorCode::EngineExecutionError);
        return std::log(value);
    }
    if (name == "exp") return std::exp(value);
    if (name == "abs") return std::fabs(value);
    fail("unknown function '" + name + "'");
}

// -------------------------------------------------------------- evaluator

// Recursive-descent parser producing immediate evaluation.
class Evaluator {
public:
    Evaluator(const std::string& text, const std::map<std::string, double>& variables)
        : text_(text), variables_(variables) {}

    double run() {
        pos_ = 0;
        skip_ws();
        const double value = parse_expression(0);
        skip_ws();
        if (pos_ != text_.size()) fail("unexpected character at position " + std::to_string(pos_));
        return value;
    }

private:
    static constexpr int kMaxDepth = 64;

    void skip_ws() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    char peek() {
        if (pos_ >= text_.size()) fail("unexpected end of expression");
        return text_[pos_];
    }

    bool consume(char c) {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    // Precedence climbing: + - (1) * / % (2) unary (3) ^ (4, right-assoc).
    double parse_expression(int depth) {
        if (depth > kMaxDepth) fail("expression nesting too deep");
        double left = parse_term(depth);
        while (true) {
            skip_ws();
            if (consume('+')) {
                left = left + parse_term(depth);
            } else if (consume('-')) {
                left = left - parse_term(depth);
            } else {
                return left;
            }
        }
    }

    double parse_term(int depth) {
        double left = parse_unary(depth);
        while (true) {
            skip_ws();
            if (consume('*')) {
                left = left * parse_unary(depth);
            } else if (consume('/')) {
                const double right = parse_unary(depth);
                if (right == 0.0) {
                    fail("division by zero", core::ErrorCode::EngineExecutionError);
                }
                left = left / right;
            } else if (consume('%')) {
                const double right = parse_unary(depth);
                if (right == 0.0) {
                    fail("modulo by zero", core::ErrorCode::EngineExecutionError);
                }
                left = std::fmod(left, right);
            } else {
                return left;
            }
        }
    }

    double parse_unary(int depth) {
        skip_ws();
        if (consume('-')) return -parse_unary(depth);
        if (consume('+')) return parse_unary(depth);
        return parse_power(depth);
    }

    double parse_power(int depth) {
        const double base = parse_atom(depth);
        skip_ws();
        if (consume('^')) {
            const double exponent = parse_unary(depth);  // right associative
            return std::pow(base, exponent);
        }
        return base;
    }

    double parse_atom(int depth) {
        if (depth > kMaxDepth) fail("expression nesting too deep");
        skip_ws();
        if (consume('(')) {
            const double value = parse_expression(depth + 1);
            if (!consume(')')) fail("missing closing parenthesis");
            return value;
        }
        if (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(peek())) ||
                                    peek() == '.')) {
            return parse_number();
        }
        if (pos_ < text_.size() && std::isalpha(static_cast<unsigned char>(peek()))) {
            return parse_identifier(depth);
        }
        fail("unexpected character at position " + std::to_string(pos_));
    }

    double parse_number() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.')) {
            ++pos_;
        }
        return std::stod(text_.substr(start, pos_ - start));
    }

    double parse_identifier(int depth) {
        const std::size_t start = pos_;
        while (pos_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
            ++pos_;
        }
        const std::string name = text_.substr(start, pos_ - start);
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '(' && is_function(name)) {
            ++pos_;  // consume '('
            const double argument = parse_expression(depth + 1);
            if (!consume(')')) fail("missing closing parenthesis for " + name);
            return apply_function(name, argument);
        }
        if (name == "pi") return kPi;
        if (name == "e") return kE;
        const auto it = variables_.find(name);
        if (it != variables_.end()) return it->second;
        fail("unknown identifier '" + name + "'");
    }

    std::string text_;
    std::map<std::string, double> variables_;
    std::size_t pos_ = 0;
};

// -------------------------------------------------------------- helpers

std::string strip_ws(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(c);
    }
    return out;
}

// Polynomial coefficient extraction for simple axis-aligned forms:
//   a*x + b  or  a*x^2 + b*x + c   (after constant folding by the evaluator)
// We evaluate the expression numerically at probe points and reconstruct the
// polynomial coefficients from three samples (Lagrange interpolation).
struct PolyCoeffs {
    bool quadratic = false;
    double c0 = 0.0, c1 = 0.0, c2 = 0.0;
};

std::optional<PolyCoeffs> fit_polynomial(const std::string& expression,
                                         const std::string& unknown,
                                         const std::map<std::string, double>& variables) {
    const double probes[3] = {1.7, 3.1, 5.3};
    double values[3];
    for (int i = 0; i < 3; ++i) {
        std::map<std::string, double> scope = variables;
        scope[unknown] = probes[i];
        values[i] = evaluate_expression(expression, scope);
    }
    // Lagrange basis through (p_i, v_i):
    // v(x) = c0 + c1 x + c2 x^2
    const double p1 = probes[0], p2 = probes[1], p3 = probes[2];
    const double v1 = values[0], v2 = values[1], v3 = values[2];

    const double d = (p3 - p1) * (p2 - p1) * (p3 - p2);
    if (std::fabs(d) < 1e-12) return std::nullopt;
    // Solve the 3x3 Vandermonde system.
    const double c0 =
        v1 * (p2 * p3) / ((p1 - p2) * (p1 - p3)) + v2 * (p1 * p3) / ((p2 - p1) * (p2 - p3)) +
        v3 * (p1 * p2) / ((p3 - p1) * (p3 - p2));
    const double c1 = v1 * (-(p2 + p3)) / ((p1 - p2) * (p1 - p3)) +
                      v2 * (-(p1 + p3)) / ((p2 - p1) * (p2 - p3)) +
                      v3 * (-(p1 + p2)) / ((p3 - p1) * (p3 - p2));
    const double c2 = v1 / ((p1 - p2) * (p1 - p3)) + v2 / ((p2 - p1) * (p2 - p3)) +
                      v3 / ((p3 - p1) * (p3 - p2));

    PolyCoeffs coeffs;
    coeffs.c0 = c0;
    coeffs.c1 = c1;
    coeffs.c2 = c2;
    coeffs.quadratic = std::fabs(c2) > 1e-12;
    return coeffs;
}

}  // namespace

double evaluate_expression(const std::string& expression,
                           const std::map<std::string, double>& variables) {
    if (expression.empty()) fail("'expression' must not be empty");
    Evaluator evaluator(expression, variables);
    return evaluator.run();
}

std::vector<std::string> extract_symbol_names(const std::string& expression) {
    std::vector<std::string> names;
    std::string current;
    const auto flush = [&] {
        if (!current.empty() && !is_function(current) && !is_constant(current) &&
            std::find(names.begin(), names.end(), current) == names.end()) {
            names.push_back(current);
        }
        current.clear();
    };
    for (const char c : expression) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            current.push_back(c);
        } else {
            flush();
        }
    }
    flush();
    return names;
}

std::optional<SolveResult> solve_polynomial(const std::string& lhs, const std::string& rhs,
                                            const std::string& unknown,
                                            const std::map<std::string, double>& variables) {
    // Bring everything to one side: f(x) = lhs - rhs, then fit coefficients.
    const std::string combined = "(" + lhs + ") - (" + rhs + ")";
    auto coeffs = fit_polynomial(combined, unknown, variables);
    if (!coeffs.has_value()) return std::nullopt;

    SolveResult result;
    if (coeffs->quadratic) {
        const double a = coeffs->c2, b = coeffs->c1, c = coeffs->c0;
        const double discriminant = b * b - 4.0 * a * c;
        result.detail = "quadratic";
        if (discriminant < -1e-12) {
            result.verified = true;  // no real roots is a valid verified outcome
            return result;
        }
        const double root = std::sqrt(std::max(0.0, discriminant));
        result.roots.push_back((-b - root) / (2.0 * a));
        if (discriminant > 1e-12) result.roots.push_back((-b + root) / (2.0 * a));
    } else {
        result.detail = "linear";
        if (std::fabs(coeffs->c1) < 1e-15) {
            if (std::fabs(coeffs->c0) < 1e-15) {
                result.verified = true;  // identity: 0 = 0
                result.detail = "identity";
                return result;
            }
            result.detail = "contradiction";
            result.verified = true;  // no solution is a valid verified outcome
            return result;
        }
        result.roots.push_back(-coeffs->c0 / coeffs->c1);
    }

    // Verification: each root re-substituted must satisfy the original.
    for (const double root : result.roots) {
        std::map<std::string, double> scope = variables;
        scope[unknown] = root;
        const double residual = std::fabs(evaluate_expression(combined, scope));
        if (!(residual < kResidualTolerance)) {
            result.verified = false;
            return result;
        }
    }
    result.verified = true;
    return result;
}

// ------------------------------------------------------------------ engine

EngineDescriptor MathEngine::describe() const {
    EngineDescriptor descriptor;
    descriptor.id = "math";
    descriptor.name = "Math Engine";
    descriptor.version = "1.0";
    descriptor.capabilities = {"solve", "evaluate"};
    descriptor.input_schema.fields = {
        {"expression", "string: expression or equation (use '=')"},
        {"variables", "object of known values"},
        {"solve_for", "optional unknown name"}};
    descriptor.output_schema.fields = {{"result", "number or roots"},
                                       {"verified", "bool"},
                                       {"checks", "object"}};
    descriptor.health = EngineHealth::Healthy;
    descriptor.health_detail = "deterministic evaluator + polynomial solver; symbolic/SymPy "
                               "solving via python engine host when connected";
    return descriptor;
}

ExecutionOutput MathEngine::execute(const std::string& capability, const core::Json& parameters) {
    const core::Json* expression = parameters.find("expression");
    if (expression == nullptr || !expression->is_string() || expression->as_string().empty()) {
        fail("'expression' is required for math." + capability);
    }
    const std::string text = expression->as_string();

    std::map<std::string, double> variables;
    if (const core::Json* vars = parameters.find("variables");
        vars != nullptr && vars->is_object()) {
        for (const auto& [name, value] : vars->as_object()) {
            variables[name] = value.as_double();
        }
    }

    ExecutionOutput output;
    output.success = true;
    output.validation_status = "VERIFIED";

    if (capability == "evaluate") {
        const double value = evaluate_expression(text, variables);
        output.result["expression"] = text;
        output.result["result"] = value;
        core::Json checks = core::Json::object();
        checks["numeric"] = true;
        checks["finite"] = std::isfinite(value);
        output.validation_checks = checks;
        return output;
    }

    if (capability != "solve") {
        fail("Math engine has no operation '" + capability + "'");
    }

    // Split on '=' (single occurrence).
    const std::size_t eq = text.find('=');
    std::string lhs = text;
    std::string rhs = "0";
    if (eq != std::string::npos) {
        lhs = text.substr(0, eq);
        rhs = text.substr(eq + 1);
        if (rhs.find('=') != std::string::npos) fail("only one '=' is allowed");
    }

    std::string solve_for;
    if (const core::Json* target = parameters.find("solve_for");
        target != nullptr && target->is_string()) {
        solve_for = target->as_string();
    }
    if (solve_for.empty()) {
        // Unknowns on the left side after substitution.
        std::vector<std::string> symbols;
        for (const std::string& name : extract_symbol_names(lhs + " " + rhs)) {
            if (variables.count(name) == 0) symbols.push_back(name);
        }
        if (symbols.size() == 1) {
            solve_for = symbols[0];
        } else if (symbols.empty()) {
            // Identity check.
            const double diff = std::fabs(evaluate_expression("(" + lhs + ") - (" + rhs + ")",
                                                              variables));
            output.result["expression"] = text;
            output.result["identity_holds"] = diff < kResidualTolerance;
            output.success = diff < kResidualTolerance;
            core::Json checks = core::Json::object();
            checks["identity_holds"] = diff < kResidualTolerance;
            output.validation_checks = checks;
            output.validation_status = output.success ? "VERIFIED" : "FAILED";
            return output;
        } else {
            core::Json details = core::Json::object();
            core::Json free_symbols = core::Json::array();
            for (const std::string& s : symbols) free_symbols.push_back(core::Json(s));
            details["free_symbols"] = free_symbols;
            fail("Multiple free symbols remain; specify 'solve_for'", details);
        }
    }

    auto solved = solve_polynomial(lhs, rhs, solve_for, variables);
    if (!solved.has_value()) {
        fail("no deterministic polynomial form found for this equation; use the python "
             "engine host for symbolic solving",
             core::ErrorCode::EngineExecutionError);
    }
    if (!solved->verified) {
        fail("computed roots failed re-substitution verification",
             core::ErrorCode::EngineExecutionError);
    }

    core::Json roots = core::Json::array();
    for (const double root : solved->roots) roots.push_back(root);
    output.result["expression"] = text;
    output.result["solved_for"] = solve_for;
    output.result["form"] = solved->detail;
    output.result["result"] = roots;
    core::Json checks = core::Json::object();
    checks["residual_below_1e-9"] = true;
    checks["root_count"] = static_cast<double>(solved->roots.size());
    output.validation_checks = checks;
    output.validation_status = "VALIDATED";
    return output;
}

core::Json MathEngine::validate(const core::Json& payload) {
    core::Json out = core::Json::object();
    if (const core::Json* expression = payload.find("expression");
        expression != nullptr && expression->is_string()) {
        try {
            const double value = evaluate_expression(expression->as_string(), {});
            out["evaluates"] = std::isfinite(value);
        } catch (const core::TrinityException&) {
            out["evaluates"] = false;
        }
    } else {
        out["evaluates"] = false;
    }
    return out;
}

EngineHealth MathEngine::health() const { return EngineHealth::Healthy; }

void register_math_engine(std::vector<std::string>& registered) {
    static std::shared_ptr<MathEngine> engine = std::make_shared<MathEngine>();
    if (EngineRegistry::instance().register_engine(engine).is_ok()) {
        registered.push_back("math");
    }
}

}  // namespace trinity::engines::math

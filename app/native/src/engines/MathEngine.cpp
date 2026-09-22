#include "trinity/engines/MathEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "trinity/core/Logger.hpp"

namespace trinity::engines {

MathEngine::MathEngine() {
    name_ = "math";
    version_ = "0.3.0";
    capabilities_ = {"evaluate_expression", "evaluate",         "solve",
                     "solve_linear",        "solve_quadratic",  "convert",
                     "formula"};
}

namespace {

// Function names with single-double-argument semantics, plus constants.
// Mirrors the Python backend's reserved set (sin/cos/tan/exp/log/sqrt/pi)
// extended with inverse trig (asin/acos/atan) and ln.
bool isFunctionName(const std::string& name) {
    return name == "sin" || name == "cos" || name == "tan" || name == "asin" ||
            name == "acos" || name == "atan" || name == "exp" || name == "log" ||
            name == "ln" || name == "sqrt" || name == "abs";
}

bool isConstantName(const std::string& name) {
    return name == "pi" || name == "e";
}

double constantValue(const std::string& name) {
    if (name == "pi") {
        return 3.14159265358979323846;
    }
    return 2.71828182845904523536;  // e
}

double applyFunction(const std::string& name, double arg, const std::string& expression) {
    if (name == "sin") return std::sin(arg);
    if (name == "cos") return std::cos(arg);
    if (name == "tan") return std::tan(arg);
    if (name == "asin") {
        if (arg < -1.0 || arg > 1.0) {
            throw core::RequestValidationError("asin() requires an argument in [-1, 1]",
                                                {{"expression", expression}}, "engines");
        }
        return std::asin(arg);
    }
    if (name == "acos") {
        if (arg < -1.0 || arg > 1.0) {
            throw core::RequestValidationError("acos() requires an argument in [-1, 1]",
                                                {{"expression", expression}}, "engines");
        }
        return std::acos(arg);
    }
    if (name == "atan") return std::atan(arg);
    if (name == "exp") return std::exp(arg);
    if (name == "log" || name == "ln") {
        if (arg <= 0.0) {
            throw core::RequestValidationError(name + "() requires a positive argument",
                                                {{"expression", expression}}, "engines");
        }
        return std::log(arg);
    }
    if (name == "sqrt") {
        if (arg < 0.0) {
            throw core::RequestValidationError("sqrt() requires a non-negative argument",
                                               {{"expression", expression}}, "engines");
        }
        return std::sqrt(arg);
    }
    return std::fabs(arg);  // abs
}

// Recursive-descent parser with variables, functions, constants and power:
//   expr   := term (('+'|'-') term)*
//   term   := factor (('*'|'/') factor)*
//   factor := ('+'|'-') factor | power
//   power  := primary ('^' factor)?          (right-associative)
//   primary:= number | constant | variable | function '(' expr ')' | '(' expr ')'
class Parser {
public:
    Parser(const std::string& text, const std::map<std::string, double>& variables)
        : text_(text), variables_(variables) {}

    double parse() {
        skipSpaces();
        if (pos_ >= text_.size()) {
            throw std::invalid_argument("empty expression");
        }
        const double value = parseExpr();
        skipSpaces();
        if (pos_ != text_.size()) {
            throw std::invalid_argument("unexpected character at offset " +
                                        std::to_string(pos_));
        }
        return value;
    }

private:
    void skipSpaces() {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    double parseExpr() {
        double value = parseTerm();
        for (;;) {
            skipSpaces();
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                const char op = text_[pos_++];
                const double rhs = parseTerm();
                value = (op == '+') ? value + rhs : value - rhs;
            } else {
                return value;
            }
        }
    }

    double parseTerm() {
        double value = parseFactor();
        for (;;) {
            skipSpaces();
            if (pos_ < text_.size() && (text_[pos_] == '*' || text_[pos_] == '/')) {
                const char op = text_[pos_++];
                const double rhs = parseFactor();
                if (op == '*') {
                    value *= rhs;
                } else {
                    if (rhs == 0.0) {
                        throw std::invalid_argument("division by zero");
                    }
                    value /= rhs;
                }
            } else {
                return value;
            }
        }
    }

    double parseFactor() {
        skipSpaces();
        if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
            const char sign = text_[pos_++];
            const double value = parseFactor();
            return (sign == '-') ? -value : value;
        }
        return parsePower();
    }

    double parsePower() {
        double base = parsePrimary();
        skipSpaces();
        if (pos_ < text_.size() && text_[pos_] == '^') {
            ++pos_;
            const double exponent = parseFactor();  // right-associative
            if (base < 0.0 && std::floor(exponent) != exponent) {
                throw std::invalid_argument("negative base with fractional exponent");
            }
            return std::pow(base, exponent);
        }
        return base;
    }

    double parsePrimary() {
        skipSpaces();
        if (pos_ >= text_.size()) {
            throw std::invalid_argument("unexpected end of expression");
        }
        if (text_[pos_] == '(') {
            ++pos_;
            const double value = parseExpr();
            skipSpaces();
            if (pos_ >= text_.size() || text_[pos_] != ')') {
                throw std::invalid_argument("missing closing parenthesis");
            }
            ++pos_;
            return value;
        }
        if (std::isalpha(static_cast<unsigned char>(text_[pos_])) ||
            text_[pos_] == '_') {
            return parseIdentifier();
        }
        return parseNumber();
    }

    double parseIdentifier() {
        const size_t start = pos_;
        while (pos_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[pos_])) ||
                text_[pos_] == '_')) {
            ++pos_;
        }
        const std::string name = text_.substr(start, pos_ - start);
        if (isConstantName(name)) {
            return constantValue(name);
        }
        skipSpaces();
        if (pos_ < text_.size() && text_[pos_] == '(') {
            if (!isFunctionName(name)) {
                throw std::invalid_argument("unknown function '" + name + "'");
            }
            ++pos_;
            const double arg = parseExpr();
            skipSpaces();
            if (pos_ >= text_.size() || text_[pos_] != ')') {
                throw std::invalid_argument("missing closing parenthesis after '" + name +
                                            "'");
            }
            ++pos_;
            return applyFunction(name, arg, text_);
        }
        const auto it = variables_.find(name);
        if (it == variables_.end()) {
            throw std::invalid_argument("unknown variable '" + name + "'");
        }
        return it->second;
    }

    double parseNumber() {
        skipSpaces();
        const size_t start = pos_;
        bool digits = false;
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
            digits = true;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
                digits = true;
            }
        }
        if (!digits) {
            throw std::invalid_argument("expected number at offset " +
                                        std::to_string(start));
        }
        return std::stod(text_.substr(start, pos_ - start));
    }

    const std::string& text_;
    const std::map<std::string, double>& variables_;
    size_t pos_ = 0;
};

/// Identifiers that are not functions/constants — i.e. variable slots.
std::vector<std::string> extractSymbols(const std::string& expression) {
    std::vector<std::string> symbols;
    size_t i = 0;
    while (i < expression.size()) {
        const auto c = static_cast<unsigned char>(expression[i]);
        if (std::isalpha(c) || expression[i] == '_') {
            size_t start = i;
            while (i < expression.size() &&
                   (std::isalnum(static_cast<unsigned char>(expression[i])) ||
                    expression[i] == '_')) {
                ++i;
            }
            const std::string name = expression.substr(start, i - start);
            if (!isFunctionName(name) && !isConstantName(name) &&
                std::find(symbols.begin(), symbols.end(), name) == symbols.end()) {
                symbols.push_back(name);
            }
        } else {
            ++i;
        }
    }
    return symbols;
}

std::map<std::string, double> jsonToVariables(const core::Json& json,
                                              const std::string& expression) {
    std::map<std::string, double> variables;
    if (json.is_null()) {
        return variables;
    }
    if (!json.is_object()) {
        throw core::RequestValidationError("'variables' must be an object",
                                           {{"expression", expression}}, "engines");
    }
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!it.value().is_number()) {
            throw core::RequestValidationError(
                "Variable '" + it.key() + "' must be numeric",
                {{"expression", expression}, {"variable", it.key()}}, "engines");
        }
        variables[it.key()] = it.value().get<double>();
    }
    return variables;
}

}  // namespace

double MathEngine::evaluateExpression(const std::string& expression) {
    // Legacy strict path: plain arithmetic only (preserves the historic
    // "unsupported character" contract).
    for (char c : expression) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isdigit(uc) || std::isspace(uc) || c == '+' || c == '-' || c == '*' ||
            c == '/' || c == '(' || c == ')' || c == '.') {
            continue;
        }
        throw core::RequestValidationError(
            "Expression contains unsupported character",
            {{"expression", expression}, {"character", std::string(1, c)}}, "engines");
    }
    const double value = evaluateWithVariables(expression, {});
    if (!std::isfinite(value)) {
        // Overflow (or any non-finite outcome) is an execution failure,
        // never a successful result.
        throw core::EngineExecutionError("Evaluation produced a non-finite value",
                                         {{"expression", expression}}, "engines");
    }
    return value;
}

double MathEngine::evaluateWithVariables(const std::string& expression,
                                         const std::map<std::string, double>& variables) {
    // Extended path always: identifiers resolve via variables/functions/constants.
    try {
        const double value = Parser(expression, variables).parse();
        if (!std::isfinite(value)) {
            // Overflow (or any non-finite outcome) is an execution
            // failure, never a usable value.
            throw core::EngineExecutionError("Evaluation produced a non-finite value",
                                             {{"expression", expression}}, "engines");
        }
        return value;
    } catch (const core::TrinityError&) {
        throw;
    } catch (const std::exception& exc) {
        throw core::RequestValidationError(
            std::string("Invalid expression: ") + exc.what(),
            {{"expression", expression}}, "engines");
    }
}

std::vector<std::string> MathEngine::symbolsIn(const std::string& expression) {
    return extractSymbols(expression);
}

// --- Dimensional units ------------------------------------------------
// Canonical base per dimension: length -> mm, mass -> g, angle -> rad,
// force -> N, pressure -> Pa. `factor` converts one unit into canonical.
struct UnitDef {
    const char* dimension;
    double factor;
};

const std::map<std::string, UnitDef>& unitTable() {
    static const std::map<std::string, UnitDef> table = {
        {"mm", {"length", 1.0}},      {"cm", {"length", 10.0}},
        {"m", {"length", 1000.0}},    {"in", {"length", 25.4}},
        {"g", {"mass", 1.0}},         {"kg", {"mass", 1000.0}},
        {"deg", {"angle", 3.14159265358979323846 / 180.0}},
        {"rad", {"angle", 1.0}},      {"n", {"force", 1.0}},
        {"pa", {"pressure", 1.0}},    {"kpa", {"pressure", 1000.0}},
        {"mpa", {"pressure", 1000000.0}},
    };
    return table;
}

std::string lowerUnit(const std::string& unit) {
    std::string out;
    out.reserve(unit.size());
    for (char c : unit) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    // Trim ASCII whitespace.
    const auto first = out.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = out.find_last_not_of(" \t");
    return out.substr(first, last - first + 1);
}

std::string MathEngine::dimensionOf(const std::string& unit) {
    const auto& table = unitTable();
    const auto it = table.find(lowerUnit(unit));
    return it == table.end() ? std::string() : std::string(it->second.dimension);
}

double MathEngine::convertUnits(double value, const std::string& from,
                                const std::string& to) {
    const auto& table = unitTable();
    const std::string f = lowerUnit(from);
    const std::string t = lowerUnit(to);
    const auto fit = table.find(f);
    if (fit == table.end()) {
        throw core::RequestValidationError("Unknown unit '" + from + "'",
                                           {{"from", from}, {"to", to}}, "engines");
    }
    const auto tit = table.find(t);
    if (tit == table.end()) {
        throw core::RequestValidationError("Unknown unit '" + to + "'",
                                           {{"from", from}, {"to", to}}, "engines");
    }
    if (std::string(fit->second.dimension) != std::string(tit->second.dimension)) {
        throw core::RequestValidationError(
            "Dimensional mismatch: cannot convert " +
                std::string(fit->second.dimension) + " '" + from + "' to " +
                std::string(tit->second.dimension) + " '" + to + "'",
            {{"from", from}, {"to", to}}, "engines");
    }
    if (!std::isfinite(value)) {
        throw core::RequestValidationError("Conversion value must be finite",
                                           {{"value", value}}, "engines");
    }
    const double canonical = value * fit->second.factor;
    const double converted = canonical / tit->second.factor;
    if (!std::isfinite(converted)) {
        throw core::EngineExecutionError("Unit conversion overflowed",
                                         {{"from", from}, {"to", to}}, "engines");
    }
    return converted;
}

// --- Engineering formulas ---------------------------------------------
// Extensible registry: each formula names its variables, SI units and a
// pure calculation function. Exactly one variable must be unknown —
// callers pass the known inputs and receive every variable back.
struct FormulaDef {
    std::vector<std::string> variables;
    std::map<std::string, std::string> units;
    std::function<core::Json(const std::map<std::string, double>&)> solve;
};

namespace {

double needFinite(const std::map<std::string, double>& in, const std::string& key,
                  const std::string& formula) {
    const auto it = in.find(key);
    if (it == in.end() || !std::isfinite(it->second)) {
        throw core::RequestValidationError(
            "Formula '" + formula + "' needs a finite numeric '" + key + "'",
            {{"formula", formula}}, "engines");
    }
    return it->second;
}

const std::map<std::string, FormulaDef>& formulaTable() {
    static const std::map<std::string, FormulaDef> table = {
        {"ohm",
         {{"V", "I", "R"},
          {{"V", "V"}, {"I", "A"}, {"R", "ohm"}},
          [](const std::map<std::string, double>& in) {
              const bool v = in.count("V") != 0u;
              const bool i = in.count("I") != 0u;
              const bool r = in.count("R") != 0u;
              if (static_cast<int>(v) + static_cast<int>(i) + static_cast<int>(r) !=
                  2) {
                  throw core::RequestValidationError(
                      "Ohm's law needs exactly two of V, I, R",
                      {{"formula", "ohm"}}, "engines");
              }
              core::Json out = core::Json::object();
              if (!v) {
                  out["V"] = needFinite(in, "I", "ohm") * needFinite(in, "R", "ohm");
              } else if (!i) {
                  const double R = needFinite(in, "R", "ohm");
                  if (R == 0.0) {
                      throw core::RequestValidationError(
                          "Ohm's law: division by zero (R = 0)",
                          {{"formula", "ohm"}}, "engines");
                  }
                  out["I"] = needFinite(in, "V", "ohm") / R;
              } else {
                  const double I = needFinite(in, "I", "ohm");
                  if (I == 0.0) {
                      throw core::RequestValidationError(
                          "Ohm's law: division by zero (I = 0)",
                          {{"formula", "ohm"}}, "engines");
                  }
                  out["R"] = needFinite(in, "V", "ohm") / I;
              }
              return out;
          }}},
        {"power",
         {{"P", "V", "I"},
          {{"P", "W"}, {"V", "V"}, {"I", "A"}},
          [](const std::map<std::string, double>& in) {
              const bool p = in.count("P") != 0u;
              const bool v = in.count("V") != 0u;
              const bool i = in.count("I") != 0u;
              if (static_cast<int>(p) + static_cast<int>(v) + static_cast<int>(i) !=
                  2) {
                  throw core::RequestValidationError(
                      "Power formula needs exactly two of P, V, I",
                      {{"formula", "power"}}, "engines");
              }
              core::Json out = core::Json::object();
              if (!p) {
                  out["P"] = needFinite(in, "V", "power") * needFinite(in, "I", "power");
              } else if (!v) {
                  const double I = needFinite(in, "I", "power");
                  if (I == 0.0) {
                      throw core::RequestValidationError(
                          "Power formula: division by zero (I = 0)",
                          {{"formula", "power"}}, "engines");
                  }
                  out["V"] = needFinite(in, "P", "power") / I;
              } else {
                  const double V = needFinite(in, "V", "power");
                  if (V == 0.0) {
                      throw core::RequestValidationError(
                          "Power formula: division by zero (V = 0)",
                          {{"formula", "power"}}, "engines");
                  }
                  out["I"] = needFinite(in, "P", "power") / V;
              }
              return out;
          }}},
        {"force",
         {{"F", "m", "a"},
          {{"F", "N"}, {"m", "kg"}, {"a", "m/s^2"}},
          [](const std::map<std::string, double>& in) {
              const bool f = in.count("F") != 0u;
              const bool m = in.count("m") != 0u;
              const bool a = in.count("a") != 0u;
              if (static_cast<int>(f) + static_cast<int>(m) + static_cast<int>(a) !=
                  2) {
                  throw core::RequestValidationError(
                      "Force formula needs exactly two of F, m, a",
                      {{"formula", "force"}}, "engines");
              }
              core::Json out = core::Json::object();
              if (!f) {
                  out["F"] = needFinite(in, "m", "force") * needFinite(in, "a", "force");
              } else if (!m) {
                  const double A = needFinite(in, "a", "force");
                  if (A == 0.0) {
                      throw core::RequestValidationError(
                          "Force formula: division by zero (a = 0)",
                          {{"formula", "force"}}, "engines");
                  }
                  out["m"] = needFinite(in, "F", "force") / A;
              } else {
                  const double M = needFinite(in, "m", "force");
                  if (M == 0.0) {
                      throw core::RequestValidationError(
                          "Force formula: division by zero (m = 0)",
                          {{"formula", "force"}}, "engines");
                  }
                  out["a"] = needFinite(in, "F", "force") / M;
              }
              return out;
          }}},
    };
    return table;
}

}  // namespace

std::vector<std::string> MathEngine::formulaNames() {
    std::vector<std::string> names;
    for (const auto& [name, def] : formulaTable()) {
        names.push_back(name);
    }
    return names;
}

core::Json MathEngine::formulaResult(const std::string& name, const core::Json& inputs) {
    const auto& table = formulaTable();
    const auto it = table.find(name);
    if (it == table.end()) {
        throw core::CapabilityUnavailableError(
            "Unknown engineering formula '" + name + "'",
            {{"formula", name}, {"supported", formulaNames()}}, "engines");
    }
    if (!inputs.is_object()) {
        throw core::RequestValidationError("Formula inputs must be an object",
                                           {{"formula", name}}, "engines");
    }
    std::map<std::string, double> in;
    core::Json echoed = core::Json::object();
    for (auto jt = inputs.begin(); jt != inputs.end(); ++jt) {
        if (!jt.value().is_number()) {
            throw core::RequestValidationError(
                "Formula input '" + jt.key() + "' must be numeric",
                {{"formula", name}}, "engines");
        }
        in[jt.key()] = jt.value().get<double>();
        echoed[jt.key()] = jt.value().get<double>();
    }
    core::Json computed = it->second.solve(in);
    core::Json outputs = echoed;
    for (auto jt = computed.begin(); jt != computed.end(); ++jt) {
        if (!jt.value().is_number() || !std::isfinite(jt.value().get<double>())) {
            throw core::EngineExecutionError(
                "Formula '" + name + "' produced a non-finite value",
                {{"formula", name}}, "engines");
        }
        outputs[jt.key()] = jt.value();
    }
    core::Json units = core::Json::object();
    for (const auto& [var, unit] : it->second.units) {
        units[var] = unit;
    }
    return core::Json{{"formula", name},
                      {"inputs", echoed},
                      {"outputs", outputs},
                      {"units", units}};
}

namespace {

/// f(target) for an equation lhs == rhs with all other symbols bound.
double equationResidual(const std::string& lhs, const std::string& rhs,
                        const std::map<std::string, double>& bound, const std::string& target,
                        double value) {
    auto vars = bound;
    vars[target] = value;
    return Parser(lhs, vars).parse() - Parser(rhs, vars).parse();
}

/// Deterministic single-variable solver: Newton from fixed seeds plus
/// bisection over sign-change brackets, deduplicated and verified.
/// Returns verified roots only (caller checks residuals independently).
std::vector<double> solveSingleVariable(const std::string& lhs, const std::string& rhs,
                                        const std::map<std::string, double>& bound,
                                        const std::string& target,
                                        const std::string& expression) {
    auto f = [&](double x) {
        return equationResidual(lhs, rhs, bound, target, x);
    };
    std::vector<double> roots;
    auto note = [&](double x) {
        if (!std::isfinite(x)) {
            return;
        }
        for (double known : roots) {
            if (std::fabs(known - x) < 1e-9) {
                return;
            }
        }
        double residual = 0.0;
        try {
            residual = f(x);
        } catch (...) {
            return;
        }
        if (std::fabs(residual) < 1e-9) {
            roots.push_back(x);
        }
    };
    // Newton iterations from deterministic seeds.
    for (double seed : {-100.0, -10.0, -1.0, 0.0, 1.0, 10.0, 100.0}) {
        try {
            double x = seed;
            for (int i = 0; i < 100; ++i) {
                const double fx = f(x);
                if (std::fabs(fx) < 1e-12) {
                    break;
                }
                const double h = 1e-6 * (1.0 + std::fabs(x));
                const double dfx = (f(x + h) - f(x - h)) / (2.0 * h);
                if (!std::isfinite(dfx) || std::fabs(dfx) < 1e-12) {
                    break;
                }
                x -= fx / dfx;
                if (!std::isfinite(x)) {
                    break;
                }
            }
            note(x);
        } catch (...) {
            // Seed failed; other seeds may still succeed.
        }
    }
    // Bisection over sign-change brackets on a fixed grid.
    const double lo = -1000.0;
    const double hi = 1000.0;
    const int steps = 2000;
    double prevX = lo;
    double prevF = 0.0;
    bool prevOk = false;
    try {
        prevF = f(prevX);
        prevOk = std::isfinite(prevF);
    } catch (...) {
        prevOk = false;
    }
    for (int i = 1; i <= steps; ++i) {
        const double x = lo + (hi - lo) * i / steps;
        double fx = 0.0;
        bool ok = false;
        try {
            fx = f(x);
            ok = std::isfinite(fx);
        } catch (...) {
            ok = false;
        }
        if (ok && prevOk && fx == 0.0) {
            note(x);
        } else if (ok && prevOk && ((prevF < 0.0) != (fx < 0.0))) {
            double a = prevX;
            double b = x;
            double fa = prevF;
            for (int j = 0; j < 100; ++j) {
                const double mid = 0.5 * (a + b);
                double fm = 0.0;
                try {
                    fm = f(mid);
                } catch (...) {
                    break;
                }
                if (!std::isfinite(fm)) {
                    break;
                }
                if (fm == 0.0 || (b - a) < 1e-12) {
                    a = b = mid;
                    fa = fm;
                    break;
                }
                if ((fa < 0.0) != (fm < 0.0)) {
                    b = mid;
                } else {
                    a = mid;
                    fa = fm;
                }
            }
            note(0.5 * (a + b));
        }
        prevX = x;
        prevF = fx;
        prevOk = ok;
    }
    if (roots.empty()) {
        throw core::EngineExecutionError("No solution found for '" + expression + "'",
                                         {{"expression", expression}}, "engines");
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

}  // namespace

EngineResult MathEngine::execute(const EngineRequest& request) {
    requireCapability(request);
    try {
        if (request.operation == "evaluate_expression") {
            requireParams(request, {"expression"});
            const std::string expr = request.parameters.value("expression", "");
            const auto vars =
                jsonToVariables(request.parameters.value("variables", core::Json(nullptr)),
                                expr);
            const double value =
                vars.empty() ? evaluateExpression(expr) : evaluateWithVariables(expr, vars);
            EngineResult out =
                successResult(request, {{"expression", expr}, {"value", value}});
            out.validation = validate(out);
            core::Logger::instance().info(
                "engines", "math evaluate_expression",
                core::Json{{"expression", expr}, {"value", value}});
            return out;
        }
        if (request.operation == "evaluate") {
            requireParams(request, {"expression"});
            const std::string expr = request.parameters.value("expression", "");
            const auto vars =
                jsonToVariables(request.parameters.value("variables", core::Json(nullptr)),
                                expr);
            const double raw = evaluateWithVariables(expr, vars);
            if (!std::isfinite(raw)) {
                throw core::EngineExecutionError("Evaluation produced a non-finite value",
                                                 {{"expression", expr}}, "engines");
            }
            core::Json varsJson = core::Json::object();
            for (const auto& [k, v] : vars) {
                varsJson[k] = v;
            }
            EngineResult out = successResult(
                request, {{"expression", expr}, {"variables", varsJson}, {"value", raw}});
            out.validation = validate(out);
            core::Logger::instance().info(
                "engines", "math evaluate",
                core::Json{{"expression", expr}, {"value", raw}});
            return out;
        }
        if (request.operation == "solve") {
            return executeSolve(request);
        }
        if (request.operation == "solve_linear") {
            return executeSolveLinear(request);
        }
        if (request.operation == "solve_quadratic") {
            return executeSolveQuadratic(request);
        }
        if (request.operation == "convert") {
            return executeConvert(request);
        }
        if (request.operation == "formula") {
            return executeFormula(request);
        }
        throw core::CapabilityUnavailableError(
            "Math engine has no operation '" + request.operation + "'",
            {{"engine", "math"}, {"operation", request.operation}}, "engines");
    } catch (const core::TrinityError& exc) {
        EngineResult out = failureResult(
            request, exc.what(), exc.toJson().value("details", core::Json::object()));
        // Preserve the original error code instead of generic execution error.
        out.errors.clear();
        out.addError(exc.info());
        out.validation = validate(out);
        return out;
    }
}

EngineResult MathEngine::executeSolve(const EngineRequest& request) {
    requireParams(request, {"expression"});
    const std::string expr = request.parameters.value("expression", "");
    const auto bound = jsonToVariables(
        request.parameters.value("variables", core::Json(nullptr)), expr);
    const std::string solveFor = request.parameters.value("solve_for", "");

    const size_t eq = expr.find('=');
    if (eq == std::string::npos) {
        throw core::RequestValidationError("solve expression must contain '='",
                                           {{"expression", expr}}, "engines");
    }
    const std::string lhs = expr.substr(0, eq);
    const std::string rhs = expr.substr(eq + 1);
    if (lhs.find_first_not_of(" \t") == std::string::npos ||
        rhs.find_first_not_of(" \t") == std::string::npos) {
        throw core::RequestValidationError("solve expression needs both sides of '='",
                                           {{"expression", expr}}, "engines");
    }

    std::vector<std::string> free;
    for (const auto& sym : extractSymbols(expr)) {
        if (bound.find(sym) == bound.end()) {
            free.push_back(sym);
        }
    }
    std::string target;
    if (!solveFor.empty()) {
        const std::vector<std::string> present = extractSymbols(expr);
        bool known = std::find(free.begin(), free.end(), solveFor) != free.end() ||
                     bound.find(solveFor) != bound.end();
        if (!known) {
            if (std::find(present.begin(), present.end(), solveFor) == present.end()) {
                throw core::RequestValidationError(
                    "solve_for '" + solveFor + "' does not appear in the expression",
                    {{"expression", expr}}, "engines");
            }
        }
        target = solveFor;
    } else if (free.size() == 1) {
        target = free[0];
    } else if (free.empty()) {
        // Fully substituted — check the identity holds.
        double residual = 0.0;
        try {
            residual = Parser(lhs, bound).parse() - Parser(rhs, bound).parse();
        } catch (const core::TrinityError&) {
            throw;
        } catch (const std::exception& exc) {
            throw core::RequestValidationError(
                std::string("Invalid expression: ") + exc.what(), {{"expression", expr}},
                "engines");
        }
        const bool holds = std::fabs(residual) < 1e-9;
        EngineResult out = successResult(
            request, {{"expression", expr}, {"identity_holds", holds}});
        out.success = holds;
        if (!holds) {
            out.errors.clear();
            out.addError(core::makeError(core::ErrorCode::EngineExecutionError,
                                         "Identity does not hold", "engines",
                                         {{"expression", expr}, {"residual", residual}}));
        }
        out.validation = validate(out);
        return out;
    } else {
        core::Json syms = core::Json::array();
        for (const auto& s : free) {
            syms.push_back(s);
        }
        throw core::RequestValidationError(
            "Multiple free symbols remain; specify 'solve_for'",
            {{"expression", expr}, {"free_symbols", syms}}, "engines");
    }

    const std::vector<double> roots = solveSingleVariable(lhs, rhs, bound, target, expr);
    core::Json solutions = core::Json::array();
    for (double root : roots) {
        solutions.push_back(root);
    }
    core::Json varsJson = core::Json::object();
    for (const auto& [k, v] : bound) {
        varsJson[k] = v;
    }
    EngineResult out =
        successResult(request, {{"expression", expr},
                                {"variables", varsJson},
                                {"solved_for", target},
                                {"solutions", solutions},
                                {"units", request.parameters.value("units", core::Json::object())}});
    // Independent residual verification per root (mirrors Python checks).
    core::Json checks = core::Json::object();
    bool allOk = true;
    for (size_t i = 0; i < roots.size(); ++i) {
        double residual = equationResidual(lhs, rhs, bound, target, roots[i]);
        const bool ok = std::fabs(residual) < 1e-9;
        checks["root_" + std::to_string(i) + "_satisfies_equation"] = ok;
        allOk = allOk && ok;
    }
    out.metadata["equation_checks"] = checks;
    if (!allOk) {
        out.success = false;
        out.addError(core::makeError(core::ErrorCode::EngineExecutionError,
                                     "Solution failed residual verification", "engines",
                                     {{"expression", expr}}));
    }
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "math solve",
        core::Json{{"expression", expr}, {"solved_for", target},
                   {"solutions", solutions.size()}});
    return out;
}

namespace {

/// Read a required finite numeric parameter. Throws RequestValidationError
/// when missing, non-numeric, NaN or infinite.
double finiteParam(const core::Json& params, const std::string& key,
                   const std::string& operation) {
    if (!params.contains(key) || !params[key].is_number()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires a numeric '" + key + "'",
            {{"operation", operation}}, "engines");
    }
    const double value = params[key].get<double>();
    if (!std::isfinite(value)) {
        throw core::RequestValidationError(
            "Parameter '" + key + "' must be finite", {{"operation", operation}},
            "engines");
    }
    return value;
}

}  // namespace

EngineResult MathEngine::executeSolveLinear(const EngineRequest& request) {
    requireParams(request, {"a", "b"});
    const double a = finiteParam(request.parameters, "a", "solve_linear");
    const double b = finiteParam(request.parameters, "b", "solve_linear");
    core::Json data = {{"a", a}, {"b", b}};
    if (a == 0.0) {
        if (b == 0.0) {
            // 0*x + 0 = 0 holds for every x.
            data["outcome"] = "infinite_solutions";
            data["message"] = "0*x + 0 = 0 holds for all x (infinitely many solutions)";
        } else {
            data["outcome"] = "no_solution";
            data["message"] = "0*x + " + std::to_string(b) +
                              " = 0 has no solution (contradiction)";
        }
        EngineResult out = successResult(request, data);
        out.validation = validate(out);
        return out;
    }
    const double x = -b / a;
    if (!std::isfinite(x)) {
        throw core::EngineExecutionError("Linear solve overflowed",
                                         {{"a", a}, {"b", b}}, "engines");
    }
    data["outcome"] = "solution";
    data["solution"] = x;
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "math solve_linear", core::Json{{"a", a}, {"b", b}, {"x", x}});
    return out;
}

EngineResult MathEngine::executeSolveQuadratic(const EngineRequest& request) {
    requireParams(request, {"a", "b", "c"});
    const double a = finiteParam(request.parameters, "a", "solve_quadratic");
    const double b = finiteParam(request.parameters, "b", "solve_quadratic");
    const double c = finiteParam(request.parameters, "c", "solve_quadratic");
    if (a == 0.0) {
        // Degenerate: fall back to the linear equation b*x + c = 0.
        EngineRequest linear = request;
        linear.operation = "solve_linear";
        linear.parameters = core::Json{{"a", b}, {"b", c}};
        EngineResult out = executeSolveLinear(linear);
        out.operation = "solve_quadratic";
        out.result["degraded_to_linear"] = true;
        out.validation = validate(out);
        return out;
    }
    const double disc = b * b - 4.0 * a * c;
    if (!std::isfinite(disc)) {
        throw core::EngineExecutionError("Quadratic discriminant overflowed",
                                         {{"a", a}, {"b", b}, {"c", c}}, "engines");
    }
    core::Json data = {{"a", a}, {"b", b}, {"c", c}, {"discriminant", disc}};
    if (disc < 0.0) {
        data["outcome"] = "no_real_roots";
        data["solutions"] = core::Json::array();
        data["message"] =
            "Discriminant is negative: no real roots. Complex output is unsupported.";
        EngineResult out = successResult(request, data);
        out.validation = validate(out);
        return out;
    }
    core::Json solutions = core::Json::array();
    if (disc == 0.0) {
        const double x = -b / (2.0 * a);
        if (!std::isfinite(x)) {
            throw core::EngineExecutionError("Quadratic solve overflowed",
                                             {{"a", a}, {"b", b}, {"c", c}}, "engines");
        }
        data["outcome"] = "repeated_root";
        solutions.push_back(x);
    } else {
        const double root = std::sqrt(disc);
        double x1 = (-b - root) / (2.0 * a);
        double x2 = (-b + root) / (2.0 * a);
        if (!std::isfinite(x1) || !std::isfinite(x2)) {
            throw core::EngineExecutionError("Quadratic solve overflowed",
                                             {{"a", a}, {"b", b}, {"c", c}}, "engines");
        }
        if (x1 > x2) {
            std::swap(x1, x2);
        }
        data["outcome"] = "two_roots";
        solutions.push_back(x1);
        solutions.push_back(x2);
    }
    data["solutions"] = solutions;
    EngineResult out = successResult(request, data);
    // Residual verification per root: |a*x^2 + b*x + c| must vanish.
    core::Json checks = core::Json::object();
    bool allOk = true;
    for (size_t i = 0; i < solutions.size(); ++i) {
        const double x = solutions[i].get<double>();
        const bool ok = std::fabs((a * x + b) * x + c) < 1e-9;
        checks["root_" + std::to_string(i) + "_satisfies_equation"] = ok;
        allOk = allOk && ok;
    }
    out.metadata["equation_checks"] = checks;
    if (!allOk) {
        out.success = false;
        out.addError(core::makeError(core::ErrorCode::EngineExecutionError,
                                     "Solution failed residual verification", "engines",
                                     {{"a", a}, {"b", b}, {"c", c}}));
    }
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "math solve_quadratic",
        core::Json{{"a", a}, {"b", b}, {"c", c}, {"outcome", data["outcome"]}});
    return out;
}

EngineResult MathEngine::executeConvert(const EngineRequest& request) {
    requireParams(request, {"value", "from", "to"});
    if (!request.parameters["value"].is_number()) {
        throw core::RequestValidationError("Convert 'value' must be numeric",
                                           {{"operation", "convert"}}, "engines");
    }
    if (!request.parameters["from"].is_string() || !request.parameters["to"].is_string()) {
        throw core::RequestValidationError("Convert 'from'/'to' must be unit strings",
                                           {{"operation", "convert"}}, "engines");
    }
    const double value = request.parameters["value"].get<double>();
    const std::string from = request.parameters["from"].get<std::string>();
    const std::string to = request.parameters["to"].get<std::string>();
    const double converted = convertUnits(value, from, to);
    EngineResult out =
        successResult(request, {{"input_value", value},
                                {"from", from},
                                {"to", to},
                                {"value", converted},
                                {"dimension", dimensionOf(to)}});
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "math convert",
        core::Json{{"value", value}, {"from", from}, {"to", to}, {"result", converted}});
    return out;
}

EngineResult MathEngine::executeFormula(const EngineRequest& request) {
    requireParams(request, {"name", "inputs"});
    if (!request.parameters["name"].is_string()) {
        throw core::RequestValidationError("Formula 'name' must be a string",
                                           {{"operation", "formula"}}, "engines");
    }
    const std::string name = request.parameters["name"].get<std::string>();
    const core::Json computed = formulaResult(name, request.parameters["inputs"]);
    EngineResult out = successResult(request, computed);
    out.validation = validate(out);
    core::Logger::instance().info("engines", "math formula",
                                  core::Json{{"formula", name}});
    return out;
}

validation::ValidationResult MathEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "math"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Math operation failed";
        validation::ValidationMessage msg;
        msg.rule = "math.success";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Engine reported failure";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    if (result.operation == "solve") {
        return validateSolve(result, validation);
    }
    if (result.operation == "solve_linear" || result.operation == "solve_quadratic") {
        const std::string outcome = result.result.value("outcome", "");
        if (outcome.empty()) {
            validation.status = validation::ValidationStatus::Invalid;
            validation.message = "Solver result has no outcome";
            return validation;
        }
        if (outcome == "solution" || outcome == "two_roots" ||
            outcome == "repeated_root") {
            if (!result.result.contains("solutions") && !result.result.contains("solution")) {
                validation.status = validation::ValidationStatus::Invalid;
                validation.message = "Solver outcome claims roots but lists none";
                return validation;
            }
            bool finite = true;
            if (result.result.contains("solution")) {
                finite = result.result["solution"].is_number() &&
                         std::isfinite(result.result["solution"].get<double>());
            }
            if (result.result.contains("solutions")) {
                for (const auto& sol : result.result["solutions"]) {
                    if (!sol.is_number() || !std::isfinite(sol.get<double>())) {
                        finite = false;
                        break;
                    }
                }
            }
            if (!finite) {
                validation.status = validation::ValidationStatus::Invalid;
                validation.message = "Solver produced non-finite solutions";
                return validation;
            }
        }
        // no_solution / infinite_solutions / no_real_roots are truthful
        // closed-form answers, not failures.
        validation.status = validation::ValidationStatus::Validated;
        validation.message = "Closed-form outcome: " + outcome;
        validation::ValidationMessage msg;
        msg.rule = "math.solver_outcome";
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = validation.message;
        validation.addMessage(std::move(msg));
        return validation;
    }
    if (result.operation == "formula") {
        if (!result.result.contains("outputs") || !result.result["outputs"].is_object() ||
            result.result["outputs"].empty()) {
            validation.status = validation::ValidationStatus::Invalid;
            validation.message = "Formula result has no outputs";
            return validation;
        }
        for (auto it = result.result["outputs"].begin();
             it != result.result["outputs"].end(); ++it) {
            if (!it.value().is_number() || !std::isfinite(it.value().get<double>())) {
                validation.status = validation::ValidationStatus::Invalid;
                validation.message = "Formula produced a non-finite output";
                return validation;
            }
        }
        validation.status = validation::ValidationStatus::Validated;
        validation.message = "Formula evaluated with finite outputs";
        validation::ValidationMessage msg;
        msg.rule = "math.formula_outputs_finite";
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = validation.message;
        validation.addMessage(std::move(msg));
        return validation;
    }
    if (!result.result.contains("value") || !result.result["value"].is_number()) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Math result missing numeric value";
        validation::ValidationMessage msg;
        msg.rule = "math.value_present";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Result has no numeric 'value'";
        validation.addMessage(std::move(msg));
        return validation;
    }
    const double value = result.result["value"].get<double>();
    if (!std::isfinite(value)) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Math result is not finite";
        return validation;
    }
    validation.status = validation::ValidationStatus::Validated;
    validation.message = "Expression evaluated and value is finite";
    validation::ValidationMessage msg;
    msg.rule = "math.value_finite";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = "Numeric value present and finite";
    validation.addMessage(std::move(msg));
    return validation;
}

validation::ValidationResult MathEngine::validateSolve(
    const EngineResult& result, validation::ValidationResult validation) const {
    if (result.result.contains("identity_holds")) {
        const bool holds = result.result.value("identity_holds", false);
        validation.status = holds ? validation::ValidationStatus::Verified
                                  : validation::ValidationStatus::Invalid;
        validation.message =
            holds ? "Identity holds for all substituted values" : "Identity does not hold";
        validation::ValidationMessage msg;
        msg.rule = "math.identity_holds";
        msg.severity = holds ? validation::Severity::Info : validation::Severity::Error;
        msg.passed = holds;
        msg.message = validation.message;
        validation.addMessage(std::move(msg));
        if (!holds && !result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    if (!result.result.contains("solutions") || !result.result["solutions"].is_array() ||
        result.result["solutions"].empty()) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Solve result has no solutions";
        validation::ValidationMessage msg;
        msg.rule = "math.solutions_present";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Result has no non-empty 'solutions' array";
        validation.addMessage(std::move(msg));
        return validation;
    }
    bool finite = true;
    for (const auto& sol : result.result["solutions"]) {
        if (!sol.is_number() || !std::isfinite(sol.get<double>())) {
            finite = false;
            break;
        }
    }
    if (!finite) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Solve produced non-finite solutions";
        return validation;
    }
    validation.status = validation::ValidationStatus::Validated;
    validation.message = "Solutions present, finite, and residual-verified";
    validation::ValidationMessage msg;
    msg.rule = "math.roots_satisfy_equation";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = validation.message;
    validation.checks["solution_count"] =
        static_cast<int>(result.result["solutions"].size());
    validation.addMessage(std::move(msg));
    return validation;
}

}  // namespace trinity::engines

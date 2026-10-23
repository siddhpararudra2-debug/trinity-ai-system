#include "trinity/engines/MathEngine.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "trinity/core/Logger.hpp"

namespace trinity::engines {

MathEngine::MathEngine() {
    name_ = "math";
    version_ = "0.4.0";
    capabilities_ = {"evaluate_expression", "evaluate",         "solve",
                     "solve_linear",        "solve_quadratic", "convert",
                     "formula",             "differentiate",   "simplify",
                     "solve_system"};
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

// ---------------------------------------------------------------------------
// Expression AST: the symbolic layer. The direct Parser above stays the
// verified numeric hot path; this AST exists for differentiating,
// simplifying and the multi-variable Newton system solver. It mirrors the
// same grammar and error strings, reuses the shared function/constant
// tables, and is treated as immutable everywhere (subtrees may be shared
// between nodes after reconstruction).
// ---------------------------------------------------------------------------

struct ExprNode;
using ExprPtr = std::shared_ptr<ExprNode>;

struct ExprNode {
    enum class Kind { Number, Symbol, Binary, UnaryMinus, Call };
    Kind kind = Kind::Number;
    double number = 0.0;  // Kind::Number
    std::string name;     // Kind::Symbol / Kind::Call
    char op = 0;          // Kind::Binary: + - * / ^
    ExprPtr left;         // Binary: lhs; UnaryMinus / Call: only operand
    ExprPtr right;        // Binary: rhs
};

ExprPtr numberNode(double value) {
    auto node = std::make_shared<ExprNode>();
    node->kind = ExprNode::Kind::Number;
    node->number = value;
    return node;
}

ExprPtr symbolNode(std::string name) {
    auto node = std::make_shared<ExprNode>();
    node->kind = ExprNode::Kind::Symbol;
    node->name = std::move(name);
    return node;
}

ExprPtr binaryNode(char op, ExprPtr left, ExprPtr right) {
    auto node = std::make_shared<ExprNode>();
    node->kind = ExprNode::Kind::Binary;
    node->op = op;
    node->left = std::move(left);
    node->right = std::move(right);
    return node;
}

ExprPtr minusNode(ExprPtr operand) {
    auto node = std::make_shared<ExprNode>();
    node->kind = ExprNode::Kind::UnaryMinus;
    node->left = std::move(operand);
    return node;
}

ExprPtr callNode(std::string name, ExprPtr arg) {
    auto node = std::make_shared<ExprNode>();
    node->kind = ExprNode::Kind::Call;
    node->name = std::move(name);
    node->left = std::move(arg);
    return node;
}

// Grammar mirror of Parser: expr/term/factor/power/primary. The difference
// is that identifiers resolve lazily: constants fold here, functions must
// be known here, but free symbols are ALLOWED (they are the point of the
// symbolic layer) and resolve at evaluation time.
class AstParser {
public:
    explicit AstParser(std::string text) : text_(std::move(text)) {}

    ExprPtr parse() {
        skipSpaces();
        if (pos_ >= text_.size()) {
            throw std::invalid_argument("empty expression");
        }
        ExprPtr value = parseExpr();
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

    ExprPtr parseExpr() {
        ExprPtr value = parseTerm();
        for (;;) {
            skipSpaces();
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                const char op = text_[pos_++];
                value = binaryNode(op, std::move(value), parseTerm());
            } else {
                return value;
            }
        }
    }

    ExprPtr parseTerm() {
        ExprPtr value = parseFactor();
        for (;;) {
            skipSpaces();
            if (pos_ < text_.size() && (text_[pos_] == '*' || text_[pos_] == '/')) {
                const char op = text_[pos_++];
                value = binaryNode(op, std::move(value), parseFactor());
            } else {
                return value;
            }
        }
    }

    ExprPtr parseFactor() {
        skipSpaces();
        if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
            const char sign = text_[pos_++];
            ExprPtr value = parseFactor();
            return (sign == '-') ? minusNode(std::move(value)) : value;
        }
        return parsePower();
    }

    ExprPtr parsePower() {
        ExprPtr base = parsePrimary();
        skipSpaces();
        if (pos_ < text_.size() && text_[pos_] == '^') {
            ++pos_;
            return binaryNode('^', std::move(base), parseFactor());
        }
        return base;
    }

    ExprPtr parsePrimary() {
        skipSpaces();
        if (pos_ >= text_.size()) {
            throw std::invalid_argument("unexpected end of expression");
        }
        if (text_[pos_] == '(') {
            ++pos_;
            ExprPtr value = parseExpr();
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

    ExprPtr parseIdentifier() {
        const size_t start = pos_;
        while (pos_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[pos_])) ||
                text_[pos_] == '_')) {
            ++pos_;
        }
        const std::string name = text_.substr(start, pos_ - start);
        if (isConstantName(name)) {
            return numberNode(constantValue(name));
        }
        skipSpaces();
        if (pos_ < text_.size() && text_[pos_] == '(') {
            if (!isFunctionName(name)) {
                throw std::invalid_argument("unknown function '" + name + "'");
            }
            ++pos_;
            ExprPtr arg = parseExpr();
            skipSpaces();
            if (pos_ >= text_.size() || text_[pos_] != ')') {
                throw std::invalid_argument("missing closing parenthesis after '" +
                                            name + "'");
            }
            ++pos_;
            return callNode(name, std::move(arg));
        }
        return symbolNode(name);
    }

    ExprPtr parseNumber() {
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
        return numberNode(std::stod(text_.substr(start, pos_ - start)));
    }

    const std::string text_;
    size_t pos_ = 0;
};

/// Numeric evaluation of an AST under variable bindings. Mirrors the direct
/// Parser's semantics and error strings; constants shadow variables exactly
/// like the legacy parse path. Domain errors from applyFunction surface as
/// RequestValidationError (matching legacy behavior).
double evalAst(const ExprPtr& node, const std::map<std::string, double>& variables) {
    using Kind = ExprNode::Kind;
    switch (node->kind) {
        case Kind::Number:
            return node->number;
        case Kind::Symbol: {
            if (isConstantName(node->name)) {
                return constantValue(node->name);
            }
            const auto it = variables.find(node->name);
            if (it == variables.end()) {
                throw std::invalid_argument("unknown variable '" + node->name + "'");
            }
            return it->second;
        }
        case Kind::UnaryMinus:
            return -evalAst(node->left, variables);
        case Kind::Call:
            return applyFunction(node->name, evalAst(node->left, variables), node->name + "(...)");
        case Kind::Binary: {
            const double a = evalAst(node->left, variables);
            const double b = evalAst(node->right, variables);
            switch (node->op) {
                case '+':
                    return a + b;
                case '-':
                    return a - b;
                case '*':
                    return a * b;
                case '/':
                    if (b == 0.0) {
                        throw std::invalid_argument("division by zero");
                    }
                    return a / b;
                case '^':
                    if (a < 0.0 && std::floor(b) != b) {
                        throw std::invalid_argument(
                            "negative base with fractional exponent");
                    }
                    return std::pow(a, b);
                default:
                    break;
            }
            break;
        }
    }
    throw std::invalid_argument("malformed expression tree");
}

/// Does the subtree contain the symbol (used for power-rule case split)?
bool referencesVariable(const ExprPtr& node, const std::string& variable) {
    using Kind = ExprNode::Kind;
    switch (node->kind) {
        case Kind::Number:
            return false;
        case Kind::Symbol:
            return node->name == variable;
        case Kind::UnaryMinus:
        case Kind::Call:
            return referencesVariable(node->left, variable);
        case Kind::Binary:
            return referencesVariable(node->left, variable) ||
                   referencesVariable(node->right, variable);
    }
    return false;
}

/// Constant-fold a numeric value into a deterministic, shortest-round-trip
/// string. Integers print without decimals for readable derivatives.
std::string formatNumber(double value) {
    if (value == 0.0) {
        return "0";  // collapses -0 as well
    }
    if (std::isfinite(value) && std::fabs(value) < 1e15) {
        const double rounded = std::nearbyint(value);
        if (std::fabs(value - rounded) < 1e-12) {
            return std::to_string(static_cast<long long>(rounded));
        }
    }
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc()) {
        // Unreachable for finite doubles; fall back to fixed precision.
        return std::to_string(value);
    }
    return std::string(buffer, result.ptr);
}

int binaryPrecedence(char op) {
    if (op == '^') {
        return 3;
    }
    if (op == '*' || op == '/') {
        return 2;
    }
    return 1;
}

/// Precedence-aware serialization of one child inside a binary/unary
/// operator. Parenthesizes only when required, so serialization is
/// deterministic and reparse-identical in value.
std::string serializeChild(const ExprPtr& child, int parentPrec, char parentOp,
                           bool rightSide);

std::string serializeAst(const ExprPtr& node) {
    using Kind = ExprNode::Kind;
    switch (node->kind) {
        case Kind::Number:
            return formatNumber(node->number);
        case Kind::Symbol:
            return node->name;
        case Kind::Call:
            return node->name + "(" + serializeAst(node->left) + ")";
        case Kind::UnaryMinus:
            return "-" + serializeChild(node->left, 2, '-', false);
        case Kind::Binary: {
            const int prec = binaryPrecedence(node->op);
            return serializeChild(node->left, prec, node->op, false) + node->op +
                   serializeChild(node->right, prec, node->op, true);
        }
    }
    return "?";
}

std::string serializeChild(const ExprPtr& child, int parentPrec, char parentOp,
                           bool rightSide) {
    const std::string text = serializeAst(child);
    if (child->kind != ExprNode::Kind::Binary &&
        child->kind != ExprNode::Kind::UnaryMinus) {
        return text;
    }
    const int prec = child->kind == ExprNode::Kind::Binary
                         ? binaryPrecedence(child->op)
                         : 2;  // UnaryMinus binds like multiplication
    bool parens = prec < parentPrec ||
                  (parentOp == '^' && prec == parentPrec &&
                   child->kind == ExprNode::Kind::Binary);
    if (!parens && rightSide) {
        if (parentOp == '/') {
            parens = prec <= parentPrec;
        } else if (parentOp == '-') {
            parens = prec <= 1;
        } else if (parentOp == '^') {
            parens = prec <= parentPrec;
        }
    }
    return parens ? "(" + text + ")" : text;
}

bool isNumberValue(const ExprPtr& node, double value) {
    return node->kind == ExprNode::Kind::Number && node->number == value;
}

/// Symbolic derivative w.r.t. `variable`. Honest refusals through abs()
/// (piecewise) and through constant^variable with a non-positive base
/// (undefined over the reals) are part of the contract, not oversights.
ExprPtr diffAst(const ExprPtr& node, const std::string& variable) {
    using Kind = ExprNode::Kind;
    switch (node->kind) {
        case Kind::Number:
            return numberNode(0.0);
        case Kind::Symbol:
            return numberNode(node->name == variable ? 1.0 : 0.0);
        case Kind::UnaryMinus:
            return minusNode(diffAst(node->left, variable));
        case Kind::Call: {
            const std::string& fn = node->name;
            const ExprPtr& u = node->left;
            const ExprPtr du = diffAst(u, variable);
            if (fn == "abs") {
                throw core::RequestValidationError(
                    "abs() is not symbolically differentiable (piecewise function)",
                    {{"function", fn}}, "engines");
            }
            if (fn == "sin") {
                return binaryNode('*', callNode("cos", u), du);
            }
            if (fn == "cos") {
                return minusNode(binaryNode('*', callNode("sin", u), du));
            }
            if (fn == "tan") {
                return binaryNode('/', du,
                                  binaryNode('^', callNode("cos", u), numberNode(2.0)));
            }
            if (fn == "exp") {
                return binaryNode('*', callNode("exp", u), du);
            }
            if (fn == "log" || fn == "ln") {
                return binaryNode('/', du, u);
            }
            if (fn == "sqrt") {
                return binaryNode('/', du,
                                  binaryNode('*', numberNode(2.0), callNode("sqrt", u)));
            }
            if (fn == "asin") {
                return binaryNode('/', du,
                                  callNode("sqrt",
                                           binaryNode('-', numberNode(1.0),
                                                      binaryNode('^', u, numberNode(2.0)))));
            }
            if (fn == "acos") {
                return minusNode(
                    binaryNode('/', du,
                               callNode("sqrt",
                                        binaryNode('-', numberNode(1.0),
                                                   binaryNode('^', u, numberNode(2.0))))));
            }
            if (fn == "atan") {
                return binaryNode('/', du,
                                  binaryNode('+', numberNode(1.0),
                                             binaryNode('^', u, numberNode(2.0))));
            }
            // AstParser only produces known functions; unreachable.
            throw std::invalid_argument("unknown function '" + fn + "'");
        }
        case Kind::Binary: {
            const char op = node->op;
            const ExprPtr& a = node->left;
            const ExprPtr& b = node->right;
            const ExprPtr da = diffAst(a, variable);
            const ExprPtr db = diffAst(b, variable);
            if (op == '+' || op == '-') {
                return binaryNode(op, da, db);
            }
            if (op == '*') {
                return binaryNode('+', binaryNode('*', a, db),
                                  binaryNode('*', da, b));
            }
            if (op == '/') {
                return binaryNode('/',
                                  binaryNode('-', binaryNode('*', da, b),
                                             binaryNode('*', a, db)),
                                  binaryNode('^', b, numberNode(2.0)));
            }
            // Power: case split by which side references the variable.
            const bool vHasVar = referencesVariable(b, variable);
            const bool uHasVar = referencesVariable(a, variable);
            if (!vHasVar) {
                // d(u^n) = n * u^(n-1) * du   (n is a constant subtree)
                const double n = evalAst(b, {});
                return binaryNode('*',
                                  numberNode(n),
                                  binaryNode('*', binaryNode('^', a, numberNode(n - 1.0)),
                                             da));
            }
            if (!uHasVar) {
                // d(c^v) = c^v * ln(c) * dv; c <= 0 has no real derivative.
                const double c = evalAst(a, {});
                if (c <= 0.0) {
                    throw core::RequestValidationError(
                        "constant^variable with non-positive base has no real "
                        "derivative",
                        {{"base", formatNumber(c)}}, "engines");
                }
                return binaryNode('*',
                                  node,
                                  binaryNode('*', callNode("ln", a), db));
            }
            // d(u^v) = u^v * (dv * ln(u) + v * du / u)
            return binaryNode('*',
                              node,
                              binaryNode('+',
                                         binaryNode('*', db, callNode("ln", a)),
                                         binaryNode('/', binaryNode('*', b, da), a)));
        }
    }
    throw std::invalid_argument("malformed expression tree");
}

/// One simplification pass (post-order). Constant folding refuses to fold
/// divisions by zero, negative-base fractional powers and 0^0, and only
/// folds results that stay finite: those remain symbolic and honest.
ExprPtr simplifyStep(const ExprPtr& node) {
    using Kind = ExprNode::Kind;
    switch (node->kind) {
        case Kind::Number:
        case Kind::Symbol:
            return node;
        case Kind::UnaryMinus: {
            const ExprPtr child = simplifyStep(node->left);
            if (child->kind == Kind::Number) {
                return numberNode(-child->number);
            }
            if (child->kind == Kind::UnaryMinus) {
                return child->left;
            }
            return minusNode(child);
        }
        case Kind::Call:
            return callNode(node->name, simplifyStep(node->left));
        case Kind::Binary: {
            const char op = node->op;
            const ExprPtr a = simplifyStep(node->left);
            const ExprPtr b = simplifyStep(node->right);
            if (a->kind == Kind::Number && b->kind == Kind::Number) {
                const double x = a->number;
                const double y = b->number;
                double folded = 0.0;
                bool foldable = true;
                switch (op) {
                    case '+':
                        folded = x + y;
                        break;
                    case '-':
                        folded = x - y;
                        break;
                    case '*':
                        folded = x * y;
                        break;
                    case '/':
                        foldable = y != 0.0;
                        if (foldable) {
                            folded = x / y;
                        }
                        break;
                    case '^':
                        foldable = !(x < 0.0 && std::floor(y) != y) &&
                                   !(x == 0.0 && y == 0.0);
                        if (foldable) {
                            folded = std::pow(x, y);
                        }
                        break;
                    default:
                        foldable = false;
                        break;
                }
                if (foldable && std::isfinite(folded)) {
                    return numberNode(folded);
                }
            }
            switch (op) {
                case '+':
                    if (isNumberValue(b, 0.0)) {
                        return a;
                    }
                    if (isNumberValue(a, 0.0)) {
                        return b;
                    }
                    break;
                case '-':
                    if (isNumberValue(b, 0.0)) {
                        return a;
                    }
                    if (isNumberValue(a, 0.0)) {
                        return minusNode(b);
                    }
                    if (serializeAst(a) == serializeAst(b)) {
                        return numberNode(0.0);
                    }
                    break;
                case '*':
                    if (isNumberValue(a, 0.0) || isNumberValue(b, 0.0)) {
                        return numberNode(0.0);
                    }
                    if (isNumberValue(a, 1.0)) {
                        return b;
                    }
                    if (isNumberValue(b, 1.0)) {
                        return a;
                    }
                    break;
                case '/':
                    if (isNumberValue(b, 1.0)) {
                        return a;
                    }
                    break;
                case '^':
                    if (isNumberValue(b, 1.0)) {
                        return a;
                    }
                    if (isNumberValue(b, 0.0) &&
                        !(a->kind == Kind::Number && a->number == 0.0)) {
                        return numberNode(1.0);
                    }
                    break;
                default:
                    break;
            }
            return binaryNode(op, a, b);
        }
    }
    return node;
}

/// Iterate simplifyStep until the serialization stabilizes (bounded passes).
ExprPtr simplifyAst(const ExprPtr& node) {
    ExprPtr current = node;
    for (int pass = 0; pass < 8; ++pass) {
        ExprPtr next = simplifyStep(current);
        if (next == current || serializeAst(next) == serializeAst(current)) {
            return next;
        }
        current = std::move(next);
    }
    return current;
}

// --- Gaussian elimination with partial pivoting -------------------------
// Shared by the matrix form of solve_linear and the Newton system
// solver. Scale-aware pivot threshold; overflow is refused, not returned.
std::vector<double> gaussSolve(std::vector<std::vector<double>> a,
                               std::vector<double> rhs) {
    const size_t n = rhs.size();
    double scale = 1.0;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (std::isfinite(a[i][j])) {
                scale = std::max(scale, std::fabs(a[i][j]));
            }
        }
    }
    for (size_t col = 0; col < n; ++col) {
        size_t pivotRow = col;
        double best = std::fabs(a[col][col]);
        for (size_t row = col + 1; row < n; ++row) {
            const double candidate = std::fabs(a[row][col]);
            if (candidate > best) {
                best = candidate;
                pivotRow = row;
            }
        }
        if (!(best > 1e-12 * scale)) {
            throw core::EngineExecutionError(
                "Matrix is singular (a pivot vanished)",
                {{"size", static_cast<double>(n)}, {"singular", true}}, "engines");
        }
        if (pivotRow != col) {
            std::swap(a[pivotRow], a[col]);
            std::swap(rhs[pivotRow], rhs[col]);
        }
        for (size_t row = col + 1; row < n; ++row) {
            const double factor = a[row][col] / a[col][col];
            if (factor == 0.0) {
                continue;
            }
            a[row][col] = 0.0;
            for (size_t k = col + 1; k < n; ++k) {
                a[row][k] -= factor * a[col][k];
            }
            rhs[row] -= factor * rhs[col];
        }
    }
    std::vector<double> x(n, 0.0);
    for (size_t i = n; i-- > 0;) {
        double sum = rhs[i];
        for (size_t j = i + 1; j < n; ++j) {
            sum -= a[i][j] * x[j];
        }
        x[i] = sum / a[i][i];
        if (!std::isfinite(x[i])) {
            throw core::EngineExecutionError(
                "Linear solve produced a non-finite value",
                {{"index", static_cast<double>(i)}}, "engines");
        }
    }
    return x;
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
        if (request.operation == "differentiate") {
            return executeDifferentiate(request);
        }
        if (request.operation == "simplify") {
            return executeSimplify(request);
        }
        if (request.operation == "solve_system") {
            return executeSolveSystem(request);
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
    if (request.parameters.contains("A")) {
        return executeSolveLinearMatrix(request);
    }
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

namespace {

//! Parse text into an AST, mapping syntax errors onto the structured
//! RequestValidationError (same phrasing as `solve`).
ExprPtr parseAstOrThrow(const std::string& expression) {
    try {
        return AstParser(expression).parse();
    } catch (const std::invalid_argument& exc) {
        throw core::RequestValidationError(std::string("Invalid expression: ") + exc.what(),
                                           {{"expression", expression}}, "engines");
    }
}

//! The symbolic operations are plain-expressions by contract; equations
//! route through `solve` / `solve_system` instead.
void rejectEquation(const std::string& expression, const std::string& operation) {
    if (expression.find('=') != std::string::npos) {
        throw core::RequestValidationError(
            operation + " expects a plain expression, not an equation with '='",
            {{"expression", expression}}, "engines");
    }
}

//! True when an expression parameter is a present, non-empty string.
void requireExpressionParam(const core::Json& parameters, const std::string& operation) {
    if (!parameters["expression"].is_string() ||
        parameters["expression"].get<std::string>().empty()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires a non-empty string 'expression'",
            {{"operation", operation}}, "engines");
    }
}

}  // namespace

EngineResult MathEngine::executeSolveLinearMatrix(const EngineRequest& request) {
    // Matrix form: A (n x n) and b (length n) as JSON arrays. It is an
    // extension of solve_linear, sharing its name, result keys and
    // validation; the scalar (a, b) coefficient form above is untouched.
    const core::Json& aJson = request.parameters["A"];
    const core::Json& bJson = request.parameters["b"];
    if (!aJson.is_array() || !bJson.is_array()) {
        throw core::RequestValidationError(
            "solve_linear 'A' and 'b' must be arrays (matrix form)",
            {{"operation", "solve_linear"}}, "engines");
    }
    const size_t n = aJson.size();
    if (n < 1 || n > 16 || bJson.size() != n) {
        throw core::RequestValidationError(
            "solve_linear expects a square 'A' (1x1..16x16) and 'b' of matching "
            "length",
            {{"rows", n}, {"b_length", bJson.size()}}, "engines");
    }
    std::vector<std::vector<double>> matrix(n, std::vector<double>(n, 0.0));
    std::vector<double> rhs(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        if (!aJson[i].is_array() || aJson[i].size() != n) {
            throw core::RequestValidationError(
                "solve_linear 'A' must be a square matrix of finite numbers",
                {{"row", i}}, "engines");
        }
        for (size_t j = 0; j < n; ++j) {
            if (!aJson[i][j].is_number() ||
                !std::isfinite(aJson[i][j].get<double>())) {
                throw core::RequestValidationError(
                    "solve_linear 'A' must contain only finite numbers",
                    {{"row", i}, {"column", j}}, "engines");
            }
            matrix[i][j] = aJson[i][j].get<double>();
        }
        if (!bJson[i].is_number() || !std::isfinite(bJson[i].get<double>())) {
            throw core::RequestValidationError(
                "solve_linear 'b' must contain only finite numbers",
                {{"index", i}}, "engines");
        }
        rhs[i] = bJson[i].get<double>();
    }

    core::Json data = {{"A", aJson}, {"b", bJson}, {"mode", "matrix"}};
    try {
        const std::vector<double> x = gaussSolve(matrix, rhs);
        core::Json solutions = core::Json::array();
        for (double value : x) {
            solutions.push_back(value);
        }
        data["outcome"] = "solution";
        data["solutions"] = solutions;
        EngineResult out = successResult(request, data);
        // Scale-aware residual verification per row (mirrors solve).
        core::Json checks = core::Json::object();
        bool allOk = true;
        for (size_t i = 0; i < n; ++i) {
            double residual = -rhs[i];
            for (size_t j = 0; j < n; ++j) {
                residual += matrix[i][j] * x[j];
            }
            const bool ok =
                std::fabs(residual) < 1e-9 * (1.0 + std::fabs(rhs[i]));
            checks["row_" + std::to_string(i) + "_satisfies_system"] = ok;
            allOk = allOk && ok;
        }
        out.metadata["equation_checks"] = checks;
        if (!allOk) {
            out.success = false;
            out.addError(core::makeError(core::ErrorCode::EngineExecutionError,
                                         "Solution failed residual verification",
                                         "engines", {{"size", n}}));
        }
        out.validation = validate(out);
        core::Logger::instance().info(
            "engines", "math solve_linear matrix",
            core::Json{{"size", n}, {"outcome", "solution"}});
        return out;
    } catch (const core::EngineExecutionError& exc) {
        // Singular is a defined, honest outcome (like no_solution), not a
        // crash; genuine overflow rethrows with its own message.
        const bool singular =
            exc.toJson().value("details", core::Json::object())
                .value("singular", false);
        if (!singular) {
            throw;
        }
        data["outcome"] = "singular";
        data["message"] = "Matrix is singular; the system has no unique solution";
        EngineResult out = successResult(request, data);
        out.validation = validate(out);
        core::Logger::instance().info(
            "engines", "math solve_linear matrix",
            core::Json{{"size", n}, {"outcome", "singular"}});
        return out;
    }
}

EngineResult MathEngine::executeDifferentiate(const EngineRequest& request) {
    requireParams(request, {"expression"});
    requireExpressionParam(request.parameters, "differentiate");
    const std::string expr = request.parameters["expression"].get<std::string>();
    rejectEquation(expr, "differentiate");

    std::string variable = "x";
    if (request.parameters.contains("variable")) {
        if (!request.parameters["variable"].is_string()) {
            throw core::RequestValidationError(
                "Parameter 'variable' must be a string",
                {{"operation", "differentiate"}}, "engines");
        }
        variable = request.parameters["variable"].get<std::string>();
        if (variable.empty()) {
            throw core::RequestValidationError(
                "Parameter 'variable' must not be empty",
                {{"operation", "differentiate"}}, "engines");
        }
        if (isFunctionName(variable) || isConstantName(variable)) {
            throw core::RequestValidationError(
                "'" + variable + "' is a function or constant name, not a variable",
                {{"operation", "differentiate"}}, "engines");
        }
    }
    const std::vector<std::string> symbols = extractSymbols(expr);
    if (request.parameters.contains("variable")) {
        if (std::find(symbols.begin(), symbols.end(), variable) ==
            symbols.end()) {
            throw core::RequestValidationError(
                "Variable '" + variable + "' does not appear in the expression",
                {{"expression", expr}, {"variable", variable}}, "engines");
        }
    } else {
        if (symbols.size() == 1) {
            variable = symbols.front();
        } else if (symbols.size() > 1) {
            core::Json names = core::Json::array();
            for (const auto& symbol : symbols) {
                names.push_back(symbol);
            }
            throw core::RequestValidationError(
                "Multiple free symbols; pass a 'variable' to differentiate "
                "against",
                {{"expression", expr}, {"symbols", names}}, "engines");
        }
    }

    bool hasAt = false;
    double at = 0.0;
    if (request.parameters.contains("at")) {
        if (!request.parameters["at"].is_number() ||
            !std::isfinite(request.parameters["at"].get<double>())) {
            throw core::RequestValidationError(
                "Parameter 'at' must be a finite number",
                {{"operation", "differentiate"}}, "engines");
        }
        hasAt = true;
        at = request.parameters["at"].get<double>();
    }

    const ExprPtr raw = parseAstOrThrow(expr);
    const ExprPtr rawDerivative = diffAst(raw, variable);
    const ExprPtr simplifiedDerivative = simplifyAst(rawDerivative);
    const std::string derivativeText = serializeAst(rawDerivative);
    const std::string simplifiedText = serializeAst(simplifiedDerivative);

    core::Json data = {{"expression", expr},
                       {"variable", variable},
                       {"derivative", derivativeText},
                       {"simplified", simplifiedText}};

    // Optional numeric value at a point; honest refusal when the derivative
    // is not finite there.
    if (hasAt) {
        try {
            const double value =
                evaluateWithVariables(simplifiedText, {{variable, at}});
            if (!std::isfinite(value)) {
                throw std::invalid_argument("non-finite");
            }
            data["value"] = value;
        } catch (const core::TrinityError&) {
            throw;
        } catch (const std::exception&) {
            throw core::RequestValidationError(
                "Derivative has no finite value at the given point",
                {{"expression", expr}, {"variable", variable}, {"at", at}},
                "engines");
        }
    }

    EngineResult out = successResult(request, data);

    // Independent numeric verification: symbolic derivative vs central
    // difference at a deterministic probe point (only possible when the
    // variable is the expression's single symbol). Skipped — and labeled
    // skipped — when the expression is not finite at the probe.
    const double probePoint = hasAt ? at : 1.7;
    bool onlyVariable = true;
    for (const auto& symbol : symbols) {
        if (symbol != variable) {
            onlyVariable = false;
            break;
        }
    }
    if (onlyVariable) {
        try {
            std::map<std::string, double> probe{{variable, probePoint}};
            const double exact =
                evaluateWithVariables(simplifiedText, probe);
            const double h = 1e-6 * (1.0 + probePoint);
            probe[variable] = probePoint + h;
            const double fp = evaluateWithVariables(expr, probe);
            probe[variable] = probePoint - h;
            const double fm = evaluateWithVariables(expr, probe);
            const double numeric = (fp - fm) / (2.0 * h);
            const bool ok =
                std::fabs(exact - numeric) < 1e-6 * (1.0 + std::fabs(exact));
            out.metadata["derivative_check"] = ok;
            if (!ok) {
                out.success = false;
                out.addError(core::makeError(
                    core::ErrorCode::EngineExecutionError,
                    "Derivative failed numeric verification", "engines",
                    {{"expression", expr}, {"variable", variable}}));
            }
        } catch (...) {
            out.metadata["derivative_check"] = "skipped";
        }
    }

    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "math differentiate",
        core::Json{{"expression", expr}, {"variable", variable},
                   {"derivative", simplifiedText}});
    return out;
}

EngineResult MathEngine::executeSimplify(const EngineRequest& request) {
    requireParams(request, {"expression"});
    requireExpressionParam(request.parameters, "simplify");
    const std::string expr = request.parameters["expression"].get<std::string>();
    rejectEquation(expr, "simplify");

    const ExprPtr ast = parseAstOrThrow(expr);
    const std::string simplified = serializeAst(simplifyAst(ast));
    core::Json data = {{"expression", expr}, {"simplified", simplified}};
    if (extractSymbols(simplified).empty()) {
        try {
            const double value = evaluateWithVariables(simplified, {});
            if (std::isfinite(value)) {
                data["value"] = value;
            }
        } catch (...) {
            // The simplified form survives; honest refusals (1/0 at
            // evaluation) remain refusals — no faked value is attached.
        }
    }
    EngineResult out = successResult(request, data);
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "math simplify",
        core::Json{{"expression", expr}, {"simplified", simplified}});
    return out;
}

EngineResult MathEngine::executeSolveSystem(const EngineRequest& request) {
    requireParams(request, {"equations", "variables"});
    const core::Json& equationsJson = request.parameters["equations"];
    if (!equationsJson.is_array() || equationsJson.empty()) {
        throw core::RequestValidationError(
            "Operation 'solve_system' requires an array of equations",
            {{"operation", "solve_system"}}, "engines");
    }
    if (equationsJson.size() > 8) {
        throw core::RequestValidationError(
            "solve_system supports at most 8 equations",
            {{"count", equationsJson.size()}}, "engines");
    }
    std::vector<std::string> residuals;
    residuals.reserve(equationsJson.size());
    for (size_t i = 0; i < equationsJson.size(); ++i) {
        if (!equationsJson[i].is_string() ||
            equationsJson[i].get<std::string>().empty()) {
            throw core::RequestValidationError(
                "solve_system equations must be non-empty strings",
                {{"index", i}}, "engines");
        }
        const std::string equation = equationsJson[i].get<std::string>();
        const size_t eq = equation.find('=');
        if (eq == std::string::npos) {
            residuals.push_back(equation);
        } else {
            const std::string lhs = equation.substr(0, eq);
            const std::string rhs = equation.substr(eq + 1);
            if (lhs.find_first_not_of(" \t") == std::string::npos ||
                rhs.find_first_not_of(" \t") == std::string::npos) {
                throw core::RequestValidationError(
                    "solve_system equation needs both sides of '='",
                    {{"index", i}}, "engines");
            }
            residuals.push_back("(" + lhs + ") - (" + rhs + ")");
        }
    }
    const size_t n = residuals.size();

    // Pre-parse every residual once: syntax errors are request errors and
    // the parsed ASTs are the exact objects evaluated during iteration.
    std::vector<ExprPtr> residualAsts;
    residualAsts.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        try {
            residualAsts.push_back(AstParser(residuals[i]).parse());
        } catch (const std::invalid_argument& exc) {
            throw core::RequestValidationError(
                std::string("Invalid equation: ") + exc.what(),
                {{"index", i}, {"equation", equationsJson[i].get<std::string>()}},
                "engines");
        }
    }

    const auto bound = jsonToVariables(request.parameters["variables"],
                                       equationsJson[0].get<std::string>());
    // Unknowns in first-appearance order (deterministic output contract).
    std::vector<std::string> symbols;
    for (const auto& residual : residuals) {
        for (const auto& symbol : extractSymbols(residual)) {
            if (std::find(symbols.begin(), symbols.end(), symbol) ==
                symbols.end()) {
                symbols.push_back(symbol);
            }
        }
    }
    for (const auto& symbol : symbols) {
        if (bound.find(symbol) == bound.end()) {
            throw core::RequestValidationError(
                "Equation uses symbol '" + symbol +
                    "' that has no initial value in 'variables'",
                {{"symbol", symbol}}, "engines");
        }
    }
    if (symbols.size() != n) {
        throw core::RequestValidationError(
            "solve_system needs exactly one equation per unknown",
            {{"equations", n},
             {"unknowns", symbols.size()}}, "engines");
    }

    std::vector<double> point(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        point[i] = bound.at(symbols[i]);
    }
    auto variablesAt = [&](const std::vector<double>& p) {
        std::map<std::string, double> vars;
        for (size_t i = 0; i < n; ++i) {
            vars[symbols[i]] = p[i];
        }
        return vars;
    };
    auto residualAt = [&](const std::vector<double>& p, std::vector<double>& out) {
        const auto vars = variablesAt(p);
        for (size_t i = 0; i < n; ++i) {
            out[i] = evalAst(residualAsts[i], vars);
        }
    };

    std::vector<double> r(n, 0.0);
    try {
        residualAt(point, r);
    } catch (const std::exception& exc) {
        throw core::EngineExecutionError(
            "Residual evaluation failed at the initial guess",
            {{"detail", exc.what()}}, "engines");
    }

    // Newton with a numeric centered-difference Jacobian; budget 200 steps.
    int iterations = 0;
    bool converged = false;
    for (int iter = 0; iter <= 200; ++iter) {
        double maxR = 0.0;
        for (double value : r) {
            maxR = std::max(maxR, std::fabs(value));
        }
        if (maxR < 1e-10) {
            converged = true;
            break;
        }
        if (iter == 200) {
            break;  // budget exhausted; honest non-convergence below
        }
        std::vector<std::vector<double>> jac(n, std::vector<double>(n, 0.0));
        for (size_t j = 0; j < n; ++j) {
            const double h = 1e-6 * (1.0 + std::fabs(point[j]));
            std::vector<double> pp = point;
            std::vector<double> pm = point;
            pp[j] += h;
            pm[j] -= h;
            std::vector<double> rp(n, 0.0);
            std::vector<double> rm(n, 0.0);
            try {
                residualAt(pp, rp);
                residualAt(pm, rm);
            } catch (const std::exception& exc) {
                throw core::EngineExecutionError(
                    "Residual evaluation failed while building the Newton "
                    "Jacobian",
                    {{"detail", exc.what()}, {"iteration", iter}}, "engines");
            }
            for (size_t i = 0; i < n; ++i) {
                jac[i][j] = (rp[i] - rm[i]) / (2.0 * h);
            }
        }
        std::vector<double> negR(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            negR[i] = -r[i];
        }
        std::vector<double> dx;
        try {
            dx = gaussSolve(jac, negR);
        } catch (const core::EngineExecutionError& exc) {
            throw core::EngineExecutionError(
                "Newton Jacobian is singular near the current point; try a "
                "different initial guess",
                {{"iteration", iter}, {"detail", exc.what()}}, "engines");
        }
        for (double value : dx) {
            if (!std::isfinite(value)) {
                throw core::EngineExecutionError(
                    "Newton produced a non-finite step",
                    {{"iteration", iter}}, "engines");
            }
        }
        for (size_t i = 0; i < n; ++i) {
            point[i] += dx[i];  // dx solved J dx = -r: p_new = p + dx
        }
        try {
            residualAt(point, r);
        } catch (const std::exception& exc) {
            throw core::EngineExecutionError(
                "Residual evaluation failed after a Newton step",
                {{"detail", exc.what()}, {"iteration", iter}}, "engines");
        }
        iterations = iter + 1;
    }
    if (!converged) {
        double finalMaxR = 0.0;
        for (double value : r) {
            finalMaxR = std::max(finalMaxR, std::fabs(value));
        }
        throw core::EngineExecutionError(
            "Newton solver did not converge in 200 iterations",
            {{"max_residual", finalMaxR}, {"equations", n}}, "engines");
    }

    // Independent residual verification per equation (mirrors solve).
    core::Json checks = core::Json::object();
    bool allOk = true;
    for (size_t i = 0; i < n; ++i) {
        const bool ok = std::fabs(r[i]) < 1e-9;
        checks["eq_" + std::to_string(i) + "_satisfies_equation"] = ok;
        allOk = allOk && ok;
    }

    core::Json solution = core::Json::object();
    core::Json variablesJson = core::Json::object();
    core::Json residualsJson = core::Json::object();
    core::Json equationsEcho = core::Json::array();
    for (size_t i = 0; i < n; ++i) {
        solution[symbols[i]] = point[i];
        variablesJson[symbols[i]] = bound.at(symbols[i]);
        residualsJson["eq_" + std::to_string(i)] = r[i];
        equationsEcho.push_back(equationsJson[i]);
    }
    core::Json data = {{"equations", equationsEcho},
                       {"variables", variablesJson},
                       {"outcome", "solution"},
                       {"solution", solution},
                       {"iterations", iterations},
                       {"residuals", residualsJson}};
    EngineResult out = successResult(request, data);
    out.metadata["equation_checks"] = checks;
    if (!allOk) {
        out.success = false;
        out.addError(core::makeError(core::ErrorCode::EngineExecutionError,
                                     "Solution failed residual verification",
                                     "engines", {{"equations", equationsEcho}}));
    }
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "math solve_system",
        core::Json{{"iterations", iterations}, {"outcome", "solution"}});
    return out;
}

std::string MathEngine::differentiateSimplified(const std::string& expression,
                                                const std::string& variable) {
    rejectEquation(expression, "differentiate");
    const std::string target = variable.empty() ? "x" : variable;
    const auto symbols = extractSymbols(expression);
    if (std::find(symbols.begin(), symbols.end(), target) == symbols.end()) {
        throw core::RequestValidationError(
            "Variable '" + target + "' does not appear in the expression",
            {{"expression", expression}, {"variable", target}}, "engines");
    }
    return serializeAst(
        simplifyAst(diffAst(parseAstOrThrow(expression), target)));
}

std::string MathEngine::simplifyExpression(const std::string& expression) {
    rejectEquation(expression, "simplify");
    return serializeAst(simplifyAst(parseAstOrThrow(expression)));
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
    if (result.operation == "solve_system") {
        if (!result.result.contains("solution") ||
            !result.result["solution"].is_object() ||
            result.result["solution"].empty()) {
            validation.status = validation::ValidationStatus::Invalid;
            validation.message = "Newton result has no solution object";
            return validation;
        }
        for (auto it = result.result["solution"].begin();
             it != result.result["solution"].end(); ++it) {
            if (!it.value().is_number() ||
                !std::isfinite(it.value().get<double>())) {
                validation.status = validation::ValidationStatus::Invalid;
                validation.message = "Newton solution contains a non-finite value";
                return validation;
            }
        }
        if (!result.result.contains("residuals") ||
            !result.result["residuals"].is_object() ||
            result.result["residuals"].empty()) {
            validation.status = validation::ValidationStatus::Invalid;
            validation.message = "Newton result lacks residual verification";
            return validation;
        }
        validation.status = validation::ValidationStatus::Validated;
        validation.message = "System solved with finite solution and residuals";
        validation::ValidationMessage msg;
        msg.rule = "math.solver_outcome";
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = validation.message;
        validation.addMessage(std::move(msg));
        return validation;
    }
    if (result.operation == "differentiate" || result.operation == "simplify") {
        if (!result.result.contains("simplified") ||
            !result.result["simplified"].is_string() ||
            result.result["simplified"].get<std::string>().empty()) {
            validation.status = validation::ValidationStatus::Invalid;
            validation.message = "Symbolic result is empty";
            return validation;
        }
        if (result.operation == "differentiate" &&
            (!result.result.contains("derivative") ||
             !result.result["derivative"].is_string() ||
             result.result["derivative"].get<std::string>().empty())) {
            validation.status = validation::ValidationStatus::Invalid;
            validation.message = "Derivative result is empty";
            return validation;
        }
        validation.status = validation::ValidationStatus::Validated;
        validation.message = "Symbolic result present and non-empty";
        validation::ValidationMessage msg;
        msg.rule = "math.symbolic_result";
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

#include "trinity/engines/MathEngine.hpp"

#include <cctype>
#include <cmath>
#include <stdexcept>

#include "trinity/core/Logger.hpp"

namespace trinity::engines {

MathEngine::MathEngine() {
    name_ = "math";
    version_ = "0.1.0";
    capabilities_ = {"evaluate_expression"};
}

namespace {

// Recursive-descent parser: expr := term (('+'|'-') term)*
// term := factor (('*'|'/') factor)* ; factor := number | '(' expr ')' | unary.
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

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
        if (text_[pos_] == '+' || text_[pos_] == '-') {
            const char sign = text_[pos_++];
            const double value = parseFactor();
            return (sign == '-') ? -value : value;
        }
        return parseNumber();
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
    size_t pos_ = 0;
};

}  // namespace

double MathEngine::evaluateExpression(const std::string& expression) {
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
    try {
        return Parser(expression).parse();
    } catch (const core::TrinityError&) {
        throw;
    } catch (const std::exception& exc) {
        throw core::RequestValidationError(
            std::string("Invalid expression: ") + exc.what(),
            {{"expression", expression}}, "engines");
    }
}

EngineResult MathEngine::execute(const EngineRequest& request) {
    requireCapability(request);
    requireParams(request, {"expression"});
    const std::string expr = request.parameters.value("expression", "");
    try {
        const double value = evaluateExpression(expr);
        EngineResult out = successResult(request, {{"expression", expr}, {"value", value}});
        out.validation = validate(out);
        core::Logger::instance().info(
            "engines", "math evaluate_expression",
            core::Json{{"expression", expr}, {"value", value}});
        return out;
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

}  // namespace trinity::engines

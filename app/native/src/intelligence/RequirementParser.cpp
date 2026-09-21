#include "trinity/intelligence/RequirementParser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::intelligence {
namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\n\r?.!;");
    return value.substr(begin, end - begin + 1);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string detectPriority(const std::string& lowered) {
    if (contains(lowered, "urgent") || contains(lowered, "asap") ||
        contains(lowered, "high priority") || contains(lowered, "as soon as possible")) {
        return "high";
    }
    if (contains(lowered, "low priority") || contains(lowered, "whenever") ||
        contains(lowered, "no rush")) {
        return "low";
    }
    return "normal";
}

core::Json detectOutputs(const std::string& lowered) {
    core::Json outputs = core::Json::array();
    auto add = [&](const std::string& name) {
        for (const auto& item : outputs) {
            if (item.is_string() && item.get<std::string>() == name) {
                return;
            }
        }
        outputs.push_back(name);
    };
    if (contains(lowered, "stl")) add("stl");
    if (contains(lowered, "step") || contains(lowered, ".stp")) add("step");
    if (contains(lowered, "json")) add("json");
    if (contains(lowered, "plot")) add("plot");
    if (contains(lowered, "report")) add("report");
    return outputs;
}

Intent makeBaseIntent(const std::string& rawText) {
    Intent intent;
    intent.intentId = core::newUuid();
    intent.timestamp = core::utcNowIso();
    intent.source = "deterministic";
    intent.rawRequest = rawText;
    intent.rawMetadata = core::Json::object();
    intent.rawMetadata["original_request"] = rawText;
    intent.constraints = core::Json::object();
    intent.parameters = core::Json::object();
    intent.outputs = core::Json::array();
    intent.priority = "normal";
    intent.confidence = 0.0;
    intent.status = ParseStatus::Invalid;
    return intent;
}

}  // namespace

NormalizedQuantity UnitNormalizer::normalize(double value, const std::string& unit) {
    NormalizedQuantity out;
    const std::string u = toLower(trim(unit));
    if (u == "mm") {
        out = {true, value, "mm", "length"};
    } else if (u == "cm") {
        out = {true, value * 10.0, "mm", "length"};
    } else if (u == "m") {
        out = {true, value * 1000.0, "mm", "length"};
    } else if (u == "in" || u == "inch" || u == "inches" || u == "\"") {
        out = {true, value * 25.4, "mm", "length"};
    } else if (u == "deg" || u == "degree" || u == "degrees") {
        out = {true, value, "deg", "angle"};
    } else if (u == "rad" || u == "radian" || u == "radians") {
        out = {true, value * 180.0 / 3.14159265358979323846, "deg", "angle"};
    } else if (u == "g" || u == "gram" || u == "grams") {
        out = {true, value, "g", "mass"};
    } else if (u == "kg" || u == "kilogram" || u == "kilograms") {
        out = {true, value * 1000.0, "g", "mass"};
    } else if (u == "n" || u == "newton" || u == "newtons") {
        out = {true, value, "N", "force"};
    } else if (u == "pa" || u == "pascal" || u == "pascals" || u == "kpa" || u == "mpa") {
        if (u == "kpa") {
            out = {true, value * 1000.0, "Pa", "pressure"};
        } else if (u == "mpa") {
            out = {true, value * 1000000.0, "Pa", "pressure"};
        } else {
            out = {true, value, "Pa", "pressure"};
        }
    } else {
        out.ok = false;
    }
    return out;
}

bool UnitNormalizer::isSupportedUnit(const std::string& unit) {
    return normalize(1.0, unit).ok;
}

std::string UnitNormalizer::canonicalUnitFor(const std::string& unit) {
    const NormalizedQuantity q = normalize(1.0, unit);
    return q.ok ? q.canonicalUnit : std::string();
}

core::Json ParseResult::toJson() const {
    core::Json errorsJson = core::Json::array();
    for (const auto& item : errors) {
        errorsJson.push_back(item);
    }
    return core::Json{{"intent", intent.toJson()},
                      {"status", toString(status)},
                      {"errors", errorsJson}};
}

ParseResult RequirementParser::parse(const std::string& text) const {
    const std::string trimmed = trim(text);
    const std::string lowered = toLower(trimmed);

    if (trimmed.empty()) {
        Intent intent = makeBaseIntent(text);
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Invalid;
        result.intent.status = ParseStatus::Invalid;
        result.errors.push_back("Empty request: no requirement to parse");
        core::Logger::instance().warning("intelligence", "requirement parse invalid",
                                         core::Json{{"reason", "empty_request"}});
        return result;
    }

    // Dispatcher order: explicit engine pin first, then CAD, then math.
    // Each handler returns nullopt-equivalent via status sentinel handled
    // Dispatcher order: explicit engine pin first, then CAD, then math.
    // Each handler returns nullopt-equivalent via status sentinel handled
    // internally; here we chain by checking a matched flag. Handlers
    // receive the original text (rawRequest preserved verbatim); lowered
    // is derived from the trimmed form for keyword detection only.
    ParseResult explicitResult = tryExplicitEngine(text, lowered);
    // tryExplicitEngine returns Invalid with a sentinel error when no
    // explicit pin is present; fall through in that case.
    if (explicitResult.errors.size() == 1 &&
        explicitResult.errors.front() == "__no_explicit_engine__") {
        // fall through to normal dispatch
    } else {
        return explicitResult;
    }

    ParseResult cad = tryCadRequest(text, lowered, "");
    if (cad.status != ParseStatus::Invalid || !cad.errors.empty()) {
        // CAD handler claims the request when it saw CAD keywords, even
        // for INCOMPLETE/AMBIGUOUS. Only fall through when it explicitly
        // reports no CAD content.
        const bool noCad =
            cad.errors.size() == 1 && cad.errors.front() == "__no_cad_content__";
        if (!noCad) {
            return cad;
        }
    }

    ParseResult math = tryMathRequest(text, lowered, "");
    if (math.status != ParseStatus::Invalid || !math.errors.empty()) {
        const bool noMath =
            math.errors.size() == 1 && math.errors.front() == "__no_math_content__";
        if (!noMath) {
            return math;
        }
    }

    Intent intent = makeBaseIntent(text);
    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Invalid;
    result.intent.status = ParseStatus::Invalid;
    result.errors.push_back(
        "No deterministic parser matched this requirement; supported: CAD generation "
        "(quadcopter frame, plate), math evaluation (calculate, solve), explicit engine "
        "requests (using math/cad engine)");
    core::Logger::instance().warning("intelligence", "requirement parse invalid",
                                     core::Json{{"request", trimmed}});
    return result;
}

ParseResult RequirementParser::tryExplicitEngine(const std::string& text,
                                                 const std::string& lowered) const {
    // Pattern: "using|with|via [the] <engine> engine ..."
    static const std::regex kExplicit(
        R"((using|with|via)\s+(?:the\s+)?(math|cad|pcb|firmware|vision|research|simulation|robotics)\s+engine\b)",
        std::regex_constants::icase);
    std::smatch match;
    if (!std::regex_search(text, match, kExplicit)) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_explicit_engine__");
        return sentinel;
    }
    const std::string engine = toLower(match[2].str());
    // Strip the engine pin prefix and re-dispatch the remainder with a
    // forced domain so "using math engine calculate 2+2" works.
    const std::string remainder = trim(text.substr(static_cast<size_t>(match.position()) +
                                                   static_cast<size_t>(match.length())));
    const std::string remainderLower = toLower(remainder);
    if (engine == "math") {
        ParseResult inner = tryMathRequest(remainder.empty() ? text : remainder,
                                           remainder.empty() ? lowered : remainderLower,
                                           "math");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_math_content__") {
            Intent intent = makeBaseIntent(text);
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.domain = "math";
            result.intent.missing = {"expression"};
            result.errors.push_back("Explicit math engine request is missing an expression");
            return result;
        }
        return inner;
    }
    if (engine == "cad") {
        ParseResult inner = tryCadRequest(remainder.empty() ? text : remainder,
                                          remainder.empty() ? lowered : remainderLower, "cad");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_cad_content__") {
            Intent intent = makeBaseIntent(text);
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.domain = "cad";
            result.intent.missing = {"object", "overall_size_mm"};
            result.errors.push_back("Explicit cad engine request is missing an object");
            return result;
        }
        return inner;
    }
    // Explicit pin to an engine family the deterministic parser knows by
    // name but has no handler for: parse as far as domain, then report
    // incomplete so the router can reject truthfully.
    Intent intent = makeBaseIntent(text);
    intent.domain = engine;
    intent.missing = {"operation", "object"};
    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Incomplete;
    result.intent.status = ParseStatus::Incomplete;
    result.errors.push_back("Explicit '" + engine +
                            "' engine request recognized; no deterministic handler yet");
    return result;
}

ParseResult RequirementParser::tryCadRequest(const std::string& text,
                                              const std::string& lowered,
                                              const std::string& forcedDomain) const {
    const bool hasVerb = contains(lowered, "create") || contains(lowered, "generate") ||
                         contains(lowered, "make") || contains(lowered, "build") ||
                         contains(lowered, "design");
    const bool mentionsQuad = contains(lowered, "quadcopter") || contains(lowered, "quadrotor") ||
                              contains(lowered, "quadcopter_frame") ||
                              (contains(lowered, "quad") && contains(lowered, "frame")) ||
                              (contains(lowered, "drone") && contains(lowered, "frame"));
    const bool mentionsDroneBare = contains(lowered, "drone") && !contains(lowered, "frame");
    const bool mentionsPlate = contains(lowered, "plate") || contains(lowered, "panel");
    const bool mentionsFrameBare =
        contains(lowered, "frame") && !mentionsQuad && !mentionsPlate && !mentionsDroneBare;
    const bool mentionsCad = mentionsQuad || mentionsPlate || mentionsFrameBare ||
                             mentionsDroneBare || contains(lowered, "cad") ||
                             contains(lowered, "stl");

    if (!forcedDomain.empty() && forcedDomain != "cad") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_cad_content__");
        return sentinel;
    }
    if (forcedDomain.empty() && !hasVerb && !mentionsCad) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_cad_content__");
        return sentinel;
    }
    // Forced CAD domain with no CAD nouns at all: caller handles it.
    if (!forcedDomain.empty() && !mentionsCad && !hasVerb) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_cad_content__");
        return sentinel;
    }
    // Verb-only input with no CAD object (e.g. "generate something"):
    // not a CAD request; let other handlers try.
    if (!mentionsCad) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_cad_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "cad";
    intent.operation = "generate";
    intent.units = "mm";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    // Object resolution.
    if (mentionsQuad) {
        intent.object = "quadcopter_frame";
    } else if (mentionsPlate) {
        intent.object = "plate";
    } else if (mentionsDroneBare) {
        // "Create a drone" without "frame": treat as quadcopter intent
        // missing both object precision and size; keep object so the
        // validator can report a precise gap.
        intent.object = "quadcopter_frame";
        intent.assumptions.push_back("interpreted bare 'drone' as quadcopter_frame");
    } else {
        // e.g. "Create a 50mm frame": size may be present but the type is
        // genuinely ambiguous between quadcopter_frame and plate.
        intent.object = "";
        ParseResult result;
        result.intent = intent;
        // Extract quantities anyway for traceability.
        static const std::regex kQty(
            R"((\d+(?:\.\d+)?)\s*(mm|cm|m|in|inch|inches|deg|rad|radian|radians|degree|degrees|kg|kilogram|kilograms|g|gram|grams|N|newton|newtons|Pa|pascal|pascals|kPa|MPa)\b)",
            std::regex_constants::icase);
        auto begin = std::sregex_iterator(text.begin(), text.end(), kQty);
        auto end = std::sregex_iterator();
        core::Json originals = core::Json::array();
        for (auto it = begin; it != end; ++it) {
            core::Json entry = core::Json::object();
            entry["value"] = (*it)[1].str();
            entry["unit"] = (*it)[2].str();
            originals.push_back(entry);
        }
        if (!originals.empty()) {
            intent.rawMetadata["original_quantities"] = originals;
        }
        result.status = ParseStatus::Ambiguous;
        result.intent.status = ParseStatus::Ambiguous;
        result.intent.missing = {"object"};
        result.intent.confidence = 0.4;
        result.errors.push_back(
            "Ambiguous CAD request: frame type missing (quadcopter_frame or plate?)");
        core::Logger::instance().warning("intelligence", "requirement parse ambiguous",
                                         core::Json{{"request", text}});
        return result;
    }

    // Numeric quantity extraction with role assignment.
    static const std::regex kQty(
        R"((\d+(?:\.\d+)?)\s*(mm|cm|m|in|inch|inches|deg|rad|radian|radians|degree|degrees|kg|kilogram|kilograms|g|gram|grams|N|newton|newtons|Pa|pascal|pascals|kPa|MPa)\b)",
        std::regex_constants::icase);
    auto begin = std::sregex_iterator(text.begin(), text.end(), kQty);
    auto end = std::sregex_iterator();
    core::Json originals = core::Json::array();
    bool hasOverall = false;
    bool hasSecondary = false;
    for (auto it = begin; it != end; ++it) {
        const std::smatch& m = *it;
        const double rawValue = std::stod(m[1].str());
        const std::string rawUnit = m[2].str();
        const NormalizedQuantity q = UnitNormalizer::normalize(rawValue, rawUnit);
        core::Json entry = core::Json::object();
        entry["value"] = m[1].str();
        entry["unit"] = rawUnit;
        if (!q.ok) {
            entry["error"] = "unsupported unit";
            originals.push_back(entry);
            continue;
        }
        entry["normalized_value"] = q.normalizedValue;
        entry["normalized_unit"] = q.canonicalUnit;
        entry["category"] = q.category;

        // Role assignment from local context: `before` (up to 30 chars
        // before the match) and `after` (up to 20 chars after it).
        // Immediate post-nominals win over distant keywords so
        // "100 mm plate with 4 mm thickness" assigns 100->overall and
        // 4->thickness instead of cross-contaminating.
        const size_t pos = static_cast<size_t>(m.position());
        const size_t mlen = static_cast<size_t>(m.length());
        const size_t beforeStart = pos > 30 ? pos - 30 : 0;
        const std::string before = toLower(text.substr(beforeStart, pos - beforeStart));
        const size_t afterLen = (std::min)(static_cast<size_t>(20), text.size() - (pos + mlen));
        const std::string after = toLower(text.substr(pos + mlen, afterLen));

        auto within = [](const std::string& ctx, const std::string& needle,
                         size_t maxOff) {
            const size_t at = ctx.find(needle);
            return at != std::string::npos && at <= maxOff;
        };

        std::string key;
        if (q.category == "length") {
            const bool afterArm = within(after, "arm", 15);
            const bool beforeArm = contains(before, "arm");
            const bool afterThick = within(after, "thick", 12);
            const bool afterPlateFrame = within(after, "plate", 12) ||
                                         within(after, "frame", 15) ||
                                         within(after, "quad", 15) ||
                                         within(after, "drone", 15);
            const bool beforeSizeLike =
                contains(before, "size") || contains(before, "span") ||
                contains(before, "plate") || contains(before, "frame") ||
                contains(before, "drone") || contains(before, "quad");
            if (afterArm || beforeArm) {
                key = "arm_thickness_mm";
            } else if (contains(before, "diameter") || contains(before, "mount") ||
                       contains(before, "motor") || within(after, "diameter", 15)) {
                key = "diameter_mm";
            } else if (contains(before, "spacing") || within(after, "spacing", 15)) {
                key = "spacing_mm";
            } else if (afterThick) {
                if (intent.object == "plate" && !hasSecondary) {
                    key = "thickness_mm";
                } else if (intent.object == "quadcopter_frame" && hasOverall) {
                    key = "arm_thickness_mm";
                } else if (!hasOverall) {
                    key = "overall_size_mm";
                } else {
                    key = "arm_thickness_mm";
                }
            } else if (contains(before, "width") && hasOverall) {
                key = "arm_thickness_mm";
            } else if (afterPlateFrame || beforeSizeLike) {
                if (!hasOverall) {
                    key = "overall_size_mm";
                } else if (!hasSecondary) {
                    key = intent.object == "plate" ? "thickness_mm" : "arm_thickness_mm";
                } else {
                    key = "extra_length_mm";
                }
            } else if (contains(before, "thick")) {
                if (intent.object == "plate" && !hasSecondary) {
                    key = "thickness_mm";
                } else if (intent.object == "quadcopter_frame" && hasOverall) {
                    key = "arm_thickness_mm";
                } else if (!hasOverall) {
                    key = "overall_size_mm";
                } else {
                    key = "arm_thickness_mm";
                }
            } else {
                if (!hasOverall) {
                    key = "overall_size_mm";
                } else if (!hasSecondary) {
                    key = intent.object == "plate" ? "thickness_mm" : "arm_thickness_mm";
                } else {
                    key = "extra_length_mm";
                }
            }
            if (key == "overall_size_mm") hasOverall = true;
            if (key == "arm_thickness_mm" || key == "thickness_mm") hasSecondary = true;
            intent.parameters[key] = q.normalizedValue;
            entry["key"] = key;
        } else if (q.category == "angle") {
            key = "angle_deg";
            intent.parameters[key] = q.normalizedValue;
            entry["key"] = key;
        } else if (q.category == "mass") {
            key = "mass_g";
            intent.parameters[key] = q.normalizedValue;
            entry["key"] = key;
        } else if (q.category == "force") {
            key = "force_N";
            intent.parameters[key] = q.normalizedValue;
            entry["key"] = key;
        } else {
            key = "pressure_Pa";
            intent.parameters[key] = q.normalizedValue;
            entry["key"] = key;
        }
        originals.push_back(entry);
    }
    if (!originals.empty()) {
        intent.rawMetadata["original_quantities"] = originals;
    }

    // "describe" operation override: "describe|spec|info ... frame".
    if (contains(lowered, "describe") || contains(lowered, "specification") ||
        contains(lowered, "what is the spec")) {
        intent.operation = "describe";
    }

    // Missing-information analysis (never invent values).
    ParseResult result;
    result.intent = intent;
    if (intent.object == "quadcopter_frame" && !intent.parameters.contains("overall_size_mm")) {
        result.status = ParseStatus::Incomplete;
        result.intent.status = ParseStatus::Incomplete;
        result.intent.missing = {"overall_size_mm"};
        result.intent.confidence = 0.6;
        result.errors.push_back("Incomplete CAD request: overall size is missing");
        core::Logger::instance().info("intelligence", "requirement parse incomplete",
                                      core::Json{{"request", text}, {"missing", "overall_size_mm"}});
        return result;
    }
    if (intent.object == "plate" && !intent.parameters.contains("overall_size_mm")) {
        result.status = ParseStatus::Incomplete;
        result.intent.status = ParseStatus::Incomplete;
        result.intent.missing = {"overall_size_mm"};
        result.intent.confidence = 0.6;
        result.errors.push_back("Incomplete CAD request: plate size is missing");
        core::Logger::instance().info("intelligence", "requirement parse incomplete",
                                      core::Json{{"request", text}, {"missing", "overall_size_mm"}});
        return result;
    }

    result.status = ParseStatus::Valid;
    result.intent.status = ParseStatus::Valid;
    result.intent.confidence = 0.9;
    if (result.intent.outputs.empty()) {
        result.intent.assumptions.push_back("outputs default to stl+json at execution time");
    }
    core::Logger::instance().info(
        "intelligence", "requirement parsed",
        core::Json{{"domain", "cad"}, {"object", intent.object}, {"status", "VALID"}});
    return result;
}

ParseResult RequirementParser::tryMathRequest(const std::string& text,
                                               const std::string& lowered,
                                               const std::string& forcedDomain) const {
    if (!forcedDomain.empty() && forcedDomain != "math") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_math_content__");
        return sentinel;
    }

    const bool hasSolve = contains(lowered, "solve");
    const bool hasEval = contains(lowered, "calculate") || contains(lowered, "compute") ||
                         contains(lowered, "evaluate") || contains(lowered, "what is") ||
                         contains(lowered, "what's");
    // Bare arithmetic like "25 * 8" (no keywords) is also a math request.
    static const std::regex kBareArith(R"(^[\d\s\+\-\*\/\^\(\)\.\%]+$)");
    const bool isBareArith = std::regex_match(trim(text), kBareArith);

    if (!hasSolve && !hasEval && !isBareArith) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_math_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "math";
    intent.object = "expression";
    intent.units = "";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    std::string expression;
    if (hasSolve) {
        intent.operation = "solve";
        static const std::regex kSolve(R"(solve\s+(.+))", std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(text, m, kSolve)) {
            expression = trim(m[1].str());
        }
        if (expression.empty()) {
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"expression"};
            result.intent.confidence = 0.5;
            result.errors.push_back("Incomplete math request: solve is missing an expression");
            return result;
        }
        intent.parameters["expression"] = expression;
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    // Evaluate path.
    if (isBareArith && !hasEval) {
        expression = trim(text);
        // Distinguish plain arithmetic from extended evaluation: any
        // letter forces the extended "evaluate" operation; pure digits
        // stay on the strict "evaluate_expression" path.
        intent.operation = "evaluate_expression";
        intent.parameters["expression"] = expression;
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    static const std::regex kEval(
        R"((calculate|compute|evaluate|what is|what's)\s+(.+))", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(text, m, kEval)) {
        expression = trim(m[2].str());
    }
    if (expression.empty()) {
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Incomplete;
        result.intent.status = ParseStatus::Incomplete;
        result.intent.operation = "evaluate_expression";
        result.intent.missing = {"expression"};
        result.intent.confidence = 0.5;
        result.errors.push_back("Incomplete math request: missing expression to evaluate");
        return result;
    }
    // Any alphabetic content (functions, constants, variables) selects
    // the extended evaluator; otherwise the strict arithmetic path.
    const bool hasAlpha =
        std::any_of(expression.begin(), expression.end(),
                    [](unsigned char c) { return std::isalpha(c) != 0; });
    intent.operation = hasAlpha ? "evaluate" : "evaluate_expression";
    intent.parameters["expression"] = expression;
    // Record any quantities with units found inside the expression for
    // traceability without rewriting the expression itself.
    static const std::regex kQty(
        R"((\d+(?:\.\d+)?)\s*(mm|cm|m|in|inch|inches|deg|rad|radian|radians|degree|degrees|kg|kilogram|kilograms|g|gram|grams|N|newton|newtons|Pa|pascal|pascals|kPa|MPa)\b)",
        std::regex_constants::icase);
    auto begin = std::sregex_iterator(expression.begin(), expression.end(), kQty);
    auto end = std::sregex_iterator();
    core::Json originals = core::Json::array();
    for (auto it = begin; it != end; ++it) {
        core::Json entry = core::Json::object();
        entry["value"] = (*it)[1].str();
        entry["unit"] = (*it)[2].str();
        const NormalizedQuantity q =
            UnitNormalizer::normalize(std::stod((*it)[1].str()), (*it)[2].str());
        if (q.ok) {
            entry["normalized_value"] = q.normalizedValue;
            entry["normalized_unit"] = q.canonicalUnit;
            entry["category"] = q.category;
        }
        originals.push_back(entry);
    }
    if (!originals.empty()) {
        intent.rawMetadata["original_quantities"] = originals;
    }

    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Valid;
    result.intent.status = ParseStatus::Valid;
    result.intent.confidence = 0.9;
    core::Logger::instance().info(
        "intelligence", "requirement parsed",
        core::Json{{"domain", "math"}, {"operation", intent.operation}, {"status", "VALID"}});
    return result;
}

}  // namespace trinity::intelligence

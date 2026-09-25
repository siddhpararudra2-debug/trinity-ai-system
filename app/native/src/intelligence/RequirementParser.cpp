#include "trinity/intelligence/RequirementParser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
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

/// Parse `k = v` bindings ("x = 10, y = 3") into numeric values.
/// Returns an empty map when nothing parses; callers treat that as
/// "no bindings supplied" and never invent values.
std::map<std::string, double> parseBindings(const std::string& text) {
    std::map<std::string, double> out;
    static const std::regex kBinding(
        R"(([A-Za-z_]\w*)\s*=\s*(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?))");
    auto begin = std::sregex_iterator(text.begin(), text.end(), kBinding);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        try {
            out[(*it)[1].str()] = std::stod((*it)[2].str());
        } catch (...) {
            // Skip unparseable numbers; the engine validates completeness.
        }
    }
    return out;
}

/// Split "expr with a = 1, b = 2" into {expression, bindings}.
/// The " with " separator is only honored when the trailing part parses
/// as at least one binding; otherwise the text is unchanged.
std::pair<std::string, std::map<std::string, double>> splitWithBindings(
    const std::string& text) {
    static const std::regex kWith(R"(\s+with\s+)", std::regex_constants::icase);
    std::smatch m;
    std::string head = text;
    std::string tail;
    // Use the last " with " so expressions stay intact.
    auto begin = std::sregex_iterator(text.begin(), text.end(), kWith);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        m = *it;
    }
    if (m.ready() && !m.empty()) {
        head = text.substr(0, static_cast<size_t>(m.position(0)));
        tail = text.substr(static_cast<size_t>(m.position(0) + m.length(0)));
    } else {
        return {trim(text), {}};
    }
    const auto bindings = parseBindings(tail);
    if (bindings.empty()) {
        return {trim(text), {}};
    }
    return {trim(head), bindings};
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
    } else if (u == "s" || u == "sec" || u == "secs" || u == "second" || u == "seconds") {
        out = {true, value, "s", "time"};
    } else if (u == "ms" || u == "millisecond" || u == "milliseconds") {
        out = {true, value / 1000.0, "s", "time"};
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

    ParseResult sim = trySimulationRequest(text, lowered, "");
    if (sim.status != ParseStatus::Invalid || !sim.errors.empty()) {
        const bool noSim =
            sim.errors.size() == 1 && sim.errors.front() == "__no_simulation_content__";
        if (!noSim) {
            return sim;
        }
    }

    // Firmware before PCB: PCB's keyword list includes "esp32", which
    // must not steal "Generate firmware for ESP32 ..." requests.
    ParseResult firmware = tryFirmwareRequest(text, lowered, "");
    if (firmware.status != ParseStatus::Invalid || !firmware.errors.empty()) {
        const bool noFirmware =
            firmware.errors.size() == 1 &&
            firmware.errors.front() == "__no_firmware_content__";
        if (!noFirmware) {
            return firmware;
        }
    }

    ParseResult pcb = tryPcbRequest(text, lowered, "");
    if (pcb.status != ParseStatus::Invalid || !pcb.errors.empty()) {
        const bool noPcb =
            pcb.errors.size() == 1 && pcb.errors.front() == "__no_pcb_content__";
        if (!noPcb) {
            return pcb;
        }
    }

    ParseResult vision = tryVisionRequest(text, lowered, "");
    if (vision.status != ParseStatus::Invalid || !vision.errors.empty()) {
        const bool noVision =
            vision.errors.size() == 1 && vision.errors.front() == "__no_vision_content__";
        if (!noVision) {
            return vision;
        }
    }

    ParseResult research = tryResearchRequest(text, lowered, "");
    if (research.status != ParseStatus::Invalid || !research.errors.empty()) {
        const bool noResearch =
            research.errors.size() == 1 &&
            research.errors.front() == "__no_research_content__";
        if (!noResearch) {
            return research;
        }
    }

    ParseResult robotics = tryRoboticsRequest(text, lowered, "");
    if (robotics.status != ParseStatus::Invalid || !robotics.errors.empty()) {
        const bool noRobotics =
            robotics.errors.size() == 1 &&
            robotics.errors.front() == "__no_robotics_content__";
        if (!noRobotics) {
            return robotics;
        }
    }

    Intent intent = makeBaseIntent(text);
    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Invalid;
    result.intent.status = ParseStatus::Invalid;
    result.errors.push_back(
        "No deterministic parser matched this requirement; supported: CAD generation "
        "(quadcopter frame, plate), PCB creation (board dimensions, parts), math "
        "evaluation (calculate, solve), simulation (simulate linear motion, projectile, "
        "constant acceleration, dynamics), firmware configuration (MCU, GPIO, UART, I2C, "
        "PWM), vision processing (resize, grayscale, edge detect), research (index "
        "document, search documents, summarize results), robotics (forward "
        "kinematics, joint trajectory, urdf export), explicit engine "
        "requests (using math/cad/pcb/simulation/firmware/vision/research/robotics "
        "engine)");
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
    if (engine == "pcb") {
        ParseResult inner = tryPcbRequest(remainder.empty() ? text : remainder,
                                          remainder.empty() ? lowered : remainderLower,
                                          "pcb");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_pcb_content__") {
            Intent intent = makeBaseIntent(text);
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.domain = "pcb";
            result.intent.object = "pcb";
            result.intent.missing = {"operation", "width_mm", "height_mm"};
            result.errors.push_back("Explicit pcb engine request is missing a board");
            return result;
        }
        return inner;
    }
    if (engine == "firmware") {
        ParseResult inner = tryFirmwareRequest(remainder.empty() ? text : remainder,
                                               remainder.empty() ? lowered
                                                                 : remainderLower,
                                               "firmware");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_firmware_content__") {
            Intent intent = makeBaseIntent(text);
            intent.domain = "firmware";
            intent.object = "firmware_project";
            intent.operation = "create_project";
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"mcu", "name"};
            result.errors.push_back(
                "Explicit firmware engine request is missing an MCU or project name");
            return result;
        }
        return inner;
    }
    if (engine == "vision") {
        ParseResult inner = tryVisionRequest(remainder.empty() ? text : remainder,
                                             remainder.empty() ? lowered
                                                               : remainderLower,
                                             "vision");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_vision_content__") {
            Intent intent = makeBaseIntent(text);
            intent.domain = "vision";
            intent.object = "image";
            intent.operation = "load_image";
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"path"};
            result.errors.push_back(
                "Explicit vision engine request is missing an image path");
            return result;
        }
        return inner;
    }
    if (engine == "research") {
        ParseResult inner = tryResearchRequest(remainder.empty() ? text : remainder,
                                               remainder.empty() ? lowered
                                                                 : remainderLower,
                                               "research");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_research_content__") {
            Intent intent = makeBaseIntent(text);
            intent.domain = "research";
            intent.object = "query";
            intent.operation = "search";
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"query", "title", "text"};
            result.errors.push_back(
                "Explicit research engine request is missing a query, title, or text");
            return result;
        }
        return inner;
    }
    if (engine == "robotics") {
        ParseResult inner = tryRoboticsRequest(remainder.empty() ? text : remainder,
                                               remainder.empty() ? lowered
                                                                 : remainderLower,
                                               "robotics");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_robotics_content__") {
            Intent intent = makeBaseIntent(text);
            intent.domain = "robotics";
            intent.object = "robot";
            intent.operation = "forward_kinematics";
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Valid;
            result.intent.status = ParseStatus::Valid;
            result.intent.confidence = 0.8;
            result.errors.clear();
            return result;
        }
        return inner;
    }
    if (engine == "simulation") {
        ParseResult inner = trySimulationRequest(remainder.empty() ? text : remainder,
                                                 remainder.empty() ? lowered
                                                                   : remainderLower,
                                                 "simulation");
        if (inner.status == ParseStatus::Invalid && !inner.errors.empty() &&
            inner.errors.front() == "__no_simulation_content__") {
            Intent intent = makeBaseIntent(text);
            intent.domain = "simulation";
            intent.object = "simulation";
            intent.operation = "describe";
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"model", "duration_s"};
            result.errors.push_back(
                "Explicit simulation engine request is missing a model or duration");
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
    const bool hasConvert = contains(lowered, "convert");
    const bool hasFormula = contains(lowered, "formula");
    // Bare arithmetic like "25 * 8" (no keywords) is also a math request.
    static const std::regex kBareArith(R"(^[\d\s\+\-\*\/\^\(\)\.\%]+$)");
    const bool isBareArith = std::regex_match(trim(text), kBareArith);

    if (!hasSolve && !hasEval && !isBareArith && !hasConvert && !hasFormula) {
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

    // Structured unit conversion: "Convert 10 cm to mm".
    if (hasConvert) {
        static const std::regex kConvert(
            R"(convert\s+(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s*([A-Za-z%"]+)\s+to\s+([A-Za-z%"]+))",
            std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(text, m, kConvert)) {
            intent.operation = "convert";
            intent.parameters["value"] = std::stod(m[1].str());
            intent.parameters["from"] = m[2].str();
            intent.parameters["to"] = m[3].str();
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Valid;
            result.intent.status = ParseStatus::Valid;
            result.intent.confidence = 0.9;
            return result;
        }
    }

    // Engineering formula: "Formula ohm with V = 12, R = 6".
    if (hasFormula) {
        static const std::regex kFormula(R"(formula\s+([A-Za-z_]\w*)\s+with\s+(.+))",
                                         std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(text, m, kFormula)) {
            const auto bindings = parseBindings(m[2].str());
            if (!bindings.empty()) {
                intent.operation = "formula";
                intent.parameters["name"] = trim(m[1].str());
                core::Json inputs = core::Json::object();
                for (const auto& [k, v] : bindings) {
                    inputs[k] = v;
                }
                intent.parameters["inputs"] = inputs;
                ParseResult result;
                result.intent = intent;
                result.status = ParseStatus::Valid;
                result.intent.status = ParseStatus::Valid;
                result.intent.confidence = 0.9;
                return result;
            }
        }
    }

    // Closed-form coefficient solvers: "Solve linear with a = 2, b = 4",
    // "Solve quadratic with a = 1, b = -5, c = 6".
    if (hasSolve) {
        static const std::regex kLinear(R"(solve\s+linear\s+with\s+(.+))",
                                        std::regex_constants::icase);
        static const std::regex kQuadratic(R"(solve\s+quadratic\s+with\s+(.+))",
                                           std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(text, m, kLinear)) {
            const auto bindings = parseBindings(m[1].str());
            if (bindings.count("a") != 0u && bindings.count("b") != 0u) {
                intent.operation = "solve_linear";
                intent.parameters["a"] = bindings.at("a");
                intent.parameters["b"] = bindings.at("b");
                ParseResult result;
                result.intent = intent;
                result.status = ParseStatus::Valid;
                result.intent.status = ParseStatus::Valid;
                result.intent.confidence = 0.9;
                return result;
            }
        }
        if (std::regex_search(text, m, kQuadratic)) {
            const auto bindings = parseBindings(m[1].str());
            if (bindings.count("a") != 0u && bindings.count("b") != 0u &&
                bindings.count("c") != 0u) {
                intent.operation = "solve_quadratic";
                intent.parameters["a"] = bindings.at("a");
                intent.parameters["b"] = bindings.at("b");
                intent.parameters["c"] = bindings.at("c");
                ParseResult result;
                result.intent = intent;
                result.status = ParseStatus::Valid;
                result.intent.status = ParseStatus::Valid;
                result.intent.confidence = 0.9;
                return result;
            }
        }
    }

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
        // "Solve x + y = 10 with y = 3": trailing bindings become the
        // variables map; every free symbol must still be bound or solved.
        const auto split = splitWithBindings(expression);
        intent.parameters["expression"] = split.first;
        if (!split.second.empty()) {
            core::Json vars = core::Json::object();
            for (const auto& [k, v] : split.second) {
                vars[k] = v;
            }
            intent.parameters["variables"] = vars;
        }
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
    // A trailing "with x = 10, ..." supplies variable bindings.
    const auto split = splitWithBindings(expression);
    expression = split.first;
    if (!split.second.empty()) {
        core::Json vars = core::Json::object();
        for (const auto& [k, v] : split.second) {
            vars[k] = v;
        }
        intent.parameters["variables"] = vars;
    }
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

ParseResult RequirementParser::trySimulationRequest(const std::string& text,
                                                    const std::string& lowered,
                                                    const std::string& forcedDomain) const {
    if (!forcedDomain.empty() && forcedDomain != "simulation") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_simulation_content__");
        return sentinel;
    }

    const bool mentionsSim =
        contains(lowered, "simulate") || contains(lowered, "simulation") ||
        contains(lowered, "projectile");
    if (forcedDomain.empty() && !mentionsSim) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_simulation_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "simulation";
    intent.object = "simulation";
    intent.units = "s";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    const bool isProjectile = contains(lowered, "projectile");

    // Quantity probes (number+unit anywhere in the text) so requests
    // like "a 1 kg object under 10 N of force" or "5 m/s velocity" are
    // recognized even when the quantity word is absent or follows the
    // number. Group layout is always (value, unit).
    static const std::regex kMassQty(
        R"((\d+(?:\.\d+)?)\s*(kilograms?|kg|grams?|g)\b)",
        std::regex_constants::icase);
    static const std::regex kForceQty(
        R"((\d+(?:\.\d+)?)\s*(newtons?|N)\b)",
        std::regex_constants::icase);
    static const std::regex kVelocityQty(
        R"((\d+(?:\.\d+)?)\s*(m/s(?!\^?\d)|mps)\b)",
        std::regex_constants::icase);
    static const std::regex kAccelQty(
        R"((-?\d+(?:\.\d+)?)\s*(m/s\^?2|mps2)\b)",
        std::regex_constants::icase);
    static const std::regex kTimeStepLeading(
        R"((?:time\s*step|dt)\s*=?\s*(\d+(?:\.\d+)?)\s*(s|sec|secs|seconds?|ms|milliseconds?)\b)",
        std::regex_constants::icase);
    static const std::regex kTimeStepTrailing(
        R"((\d+(?:\.\d+)?)\s*(s|sec|secs|seconds?|ms|milliseconds?)\s*time\s*step\b)",
        std::regex_constants::icase);

    const bool hasMassQuantity = std::regex_search(text, kMassQty);
    const bool hasForceQuantity = std::regex_search(text, kForceQty);

    const bool isDynamics =
        contains(lowered, "dynamics") ||
        (contains(lowered, "force") && contains(lowered, "mass")) ||
        (hasMassQuantity && hasForceQuantity) ||
        contains(lowered, "f = m") || contains(lowered, "f=ma") ||
        contains(lowered, "newton");
    const bool isLinearMotion = contains(lowered, "linear motion");
    const bool isConstantAccel =
        contains(lowered, "constant acceleration") ||
        (contains(lowered, "acceleration") && !isProjectile && !isDynamics &&
         !isLinearMotion);
    const bool isLinear = isLinearMotion ||
                          (contains(lowered, "motion") && !isProjectile && !isDynamics &&
                           !isConstantAccel);

    if (isProjectile) {
        intent.operation = "simulate_projectile";
        intent.object = "projectile";
    } else if (isDynamics) {
        intent.operation = "simulate_dynamics";
        intent.object = "dynamics";
    } else if (isLinear) {
        intent.operation = "simulate_linear_motion";
        intent.object = "linear_motion";
    } else if (isConstantAccel) {
        intent.operation = "simulate_constant_acceleration";
        intent.object = "constant_acceleration";
    } else if (mentionsSim) {
        intent.operation = "simulate_linear_motion";
        intent.object = "linear_motion";
    }

    static const std::regex kDuration(
        R"((?:(?:for|duration|over)\s*=?\s*)(\d+(?:\.\d+)?)\s*(s|sec|secs|seconds?|ms|milliseconds?)\b)",
        std::regex_constants::icase);
    static const std::regex kVelocity(
        R"((?:initial\s+velocity|velocity|speed)\s*=?\s*(\d+(?:\.\d+)?)\s*(m/s(?:\^?2)?|mps)\b)",
        std::regex_constants::icase);
    static const std::regex kAccel(
        R"((?:acceleration|accel\.?)\s*=?\s*(-?\d+(?:\.\d+)?)\s*(m/s(?:\^?2)?|mps2?)\b)",
        std::regex_constants::icase);
    static const std::regex kAngle(
        R"((?:at\s+)?(\d+(?:\.\d+)?)\s*(deg|degrees?|rad|radians?)\b)",
        std::regex_constants::icase);
    static const std::regex kHeight(
        R"((?:initial\s+height|height)\s*=?\s*(\d+(?:\.\d+)?)\s*(mm|cm|m|in|inch|inches)\b)",
        std::regex_constants::icase);

    core::Json originals = core::Json::array();
    auto recordOriginal = [&](const std::smatch& m, const std::string& key,
                              const NormalizedQuantity& q) {
        core::Json entry = core::Json::object();
        entry["value"] = m[1].str();
        entry["unit"] = m[2].str();
        entry["key"] = key;
        if (q.ok) {
            entry["normalized_value"] = q.normalizedValue;
            entry["normalized_unit"] = q.canonicalUnit;
            entry["category"] = q.category;
        } else {
            entry["error"] = "unsupported unit";
        }
        originals.push_back(entry);
    };

    std::smatch m;
    if (std::regex_search(text, m, kDuration)) {
        const double raw = std::stod(m[1].str());
        const NormalizedQuantity q = UnitNormalizer::normalize(raw, m[2].str());
        if (q.ok) {
            intent.parameters["duration_s"] = q.normalizedValue;
            recordOriginal(m, "duration_s", q);
        } else {
            core::Json entry = core::Json::object();
            entry["value"] = m[1].str();
            entry["unit"] = m[2].str();
            entry["error"] = "unsupported unit";
            originals.push_back(entry);
        }
    }

    // Time step ("dt 0.01 s", "0.01 second time step"): explicit user
    // inputs are extracted, never invented. Milliseconds convert to s.
    const bool sawTimeStep =
        std::regex_search(text, m, kTimeStepLeading) ||
        std::regex_search(text, m, kTimeStepTrailing);
    if (sawTimeStep) {
        const double raw = std::stod(m[1].str());
        const std::string unit = toLower(m[2].str());
        double seconds = raw;
        if (unit == "ms" || unit == "millisecond" || unit == "milliseconds") {
            seconds = raw / 1000.0;
        }
        if (std::isfinite(seconds) && seconds > 0.0) {
            intent.parameters["dt_s"] = seconds;
            core::Json entry = core::Json::object();
            entry["value"] = m[1].str();
            entry["unit"] = m[2].str();
            entry["key"] = "dt_s";
            entry["normalized_value"] = seconds;
            entry["normalized_unit"] = "s";
            entry["category"] = "time";
            originals.push_back(entry);
        }
    }

    bool sawVelocity = std::regex_search(text, m, kVelocity);
    if (!sawVelocity) {
        sawVelocity = std::regex_search(text, m, kVelocityQty);
    }
    if (sawVelocity) {
        const double raw = std::stod(m[1].str());
        const std::string unit = toLower(m[2].str());
        double velocity = raw;
        if (unit == "mm/s") velocity = raw / 1000.0;
        if (std::isfinite(velocity)) {
            intent.parameters["initial_velocity_m_s"] = velocity;
            core::Json entry = core::Json::object();
            entry["value"] = m[1].str();
            entry["unit"] = m[2].str();
            entry["key"] = "initial_velocity_m_s";
            entry["normalized_value"] = velocity;
            entry["normalized_unit"] = "m/s";
            entry["category"] = "velocity";
            originals.push_back(entry);
        }
    }

    bool sawAccel = !isProjectile && std::regex_search(text, m, kAccel);
    if (!sawAccel && !isProjectile) {
        sawAccel = std::regex_search(text, m, kAccelQty);
    }
    if (sawAccel) {
        const double raw = std::stod(m[1].str());
        const std::string unit = toLower(m[2].str());
        double accel = raw;
        if (unit == "mm/s2" || unit == "mm/s^2") accel = raw / 1000.0;
        if (std::isfinite(accel)) {
            intent.parameters["acceleration_m_s2"] = accel;
            core::Json entry = core::Json::object();
            entry["value"] = m[1].str();
            entry["unit"] = m[2].str();
            entry["key"] = "acceleration_m_s2";
            entry["normalized_value"] = accel;
            entry["normalized_unit"] = "m/s^2";
            entry["category"] = "acceleration";
            originals.push_back(entry);
        }
    }

    if (isProjectile && std::regex_search(text, m, kAngle)) {
        const double raw = std::stod(m[1].str());
        const NormalizedQuantity q = UnitNormalizer::normalize(raw, m[2].str());
        if (q.ok) {
            intent.parameters["launch_angle_deg"] = q.normalizedValue;
            recordOriginal(m, "launch_angle_deg", q);
        }
    }

    if (isProjectile && std::regex_search(text, m, kHeight)) {
        const double raw = std::stod(m[1].str());
        const NormalizedQuantity q = UnitNormalizer::normalize(raw, m[2].str());
        if (q.ok && q.category == "length") {
            intent.parameters["initial_height_mm"] = q.normalizedValue;
            recordOriginal(m, "initial_height_mm", q);
        }
    }

    if (isDynamics && std::regex_search(text, m, kMassQty)) {
        const double raw = std::stod(m[1].str());
        const NormalizedQuantity q = UnitNormalizer::normalize(raw, m[2].str());
        if (q.ok && q.category == "mass") {
            intent.parameters["mass_g"] = q.normalizedValue;
            recordOriginal(m, "mass_g", q);
        }
    }

    if (isDynamics && std::regex_search(text, m, kForceQty)) {
        const double raw = std::stod(m[1].str());
        const NormalizedQuantity q = UnitNormalizer::normalize(raw, m[2].str());
        if (q.ok && q.category == "force") {
            intent.parameters["force_N"] = q.normalizedValue;
            recordOriginal(m, "force_N", q);
        }
    }

    if (!originals.empty()) {
        intent.rawMetadata["original_quantities"] = originals;
    }

    ParseResult result;
    result.intent = intent;

    std::vector<std::string> missing;
    if (!intent.parameters.contains("duration_s")) {
        missing.push_back("duration_s");
    }
    if (intent.operation == "simulate_projectile" &&
        !intent.parameters.contains("initial_velocity_m_s")) {
        missing.push_back("initial_velocity_m_s");
    }
    if (intent.operation == "simulate_projectile" &&
        !intent.parameters.contains("launch_angle_deg")) {
        missing.push_back("launch_angle_deg");
    }
    if (intent.operation == "simulate_constant_acceleration" &&
        !intent.parameters.contains("acceleration_m_s2")) {
        missing.push_back("acceleration_m_s2");
    }
    if (intent.operation == "simulate_dynamics") {
        if (!intent.parameters.contains("force_N")) {
            missing.push_back("force_N");
        }
        if (!intent.parameters.contains("mass_g")) {
            missing.push_back("mass_g");
        }
    }

    if (!missing.empty()) {
        result.status = ParseStatus::Incomplete;
        result.intent.status = ParseStatus::Incomplete;
        result.intent.missing = missing;
        result.intent.confidence = 0.5;
        result.errors.push_back("Incomplete simulation request: missing " +
                                missing.front());
        return result;
    }

    result.status = ParseStatus::Valid;
    result.intent.status = ParseStatus::Valid;
    result.intent.confidence = 0.9;
    core::Logger::instance().info(
        "intelligence", "requirement parsed",
        core::Json{{"domain", "simulation"},
                   {"operation", intent.operation},
                   {"status", "VALID"}});
    return result;
}

ParseResult RequirementParser::tryPcbRequest(const std::string& text,
                                             const std::string& lowered,
                                             const std::string& forcedDomain) const {
    if (!forcedDomain.empty() && forcedDomain != "pcb") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_pcb_content__");
        return sentinel;
    }
    const bool mentionsPcb =
        contains(lowered, "pcb") || contains(lowered, "kicad") ||
        contains(lowered, "circuit board") || contains(lowered, "board") ||
        contains(lowered, "footprint") || contains(lowered, "esp32") ||
        contains(lowered, "imu") || contains(lowered, "mpu") ||
        contains(lowered, "regulator") || contains(lowered, "ams1117") ||
        contains(lowered, "ldo");
    if (forcedDomain.empty() && !mentionsPcb) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_pcb_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "pcb";
    intent.object = "pcb";
    intent.units = "mm";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    auto collectParts = [&]() {
        core::Json parts = core::Json::array();
        auto add = [&](const std::string& part) {
            for (const auto& item : parts) {
                if (item.is_string() && item.get<std::string>() == part) {
                    return;
                }
            }
            parts.push_back(part);
        };
        if (contains(lowered, "esp32")) add("esp32");
        if (contains(lowered, "imu") || contains(lowered, "mpu") ||
            contains(lowered, "accelerometer") || contains(lowered, "gyroscope"))
            add("imu");
        if (contains(lowered, "regulator") || contains(lowered, "ams1117") ||
            contains(lowered, "ldo"))
            add("regulator");
        if (contains(lowered, "resistor")) add("resistor");
        if (contains(lowered, "capacitor")) add("capacitor");
        if (contains(lowered, "led")) add("led");
        return parts;
    };

    // Placement requests need an existing design: report it missing
    // instead of inventing board state.
    {
        static const std::regex kPlaceCenter(
            R"(place\s+([A-Za-z]+\d+)\s+at\s+(?:the\s+)?center)",
            std::regex_constants::icase);
        static const std::regex kPlaceXy(
            R"(place\s+([A-Za-z]+\d+)\s+at\s+(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?))",
            std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(text, m, kPlaceCenter) ||
            std::regex_search(text, m, kPlaceXy)) {
            intent.operation = "place_component";
            intent.parameters["ref"] = m[1].str();
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"design"};
            result.intent.confidence = 0.5;
            result.errors.push_back(
                "Placement needs an existing board design; create a board first");
            return result;
        }
    }

    // Board dimensions: "50 mm x 40 mm".
    static const std::regex kDims(
        R"((\d+(?:\.\d+)?)\s*mm\s*x\s*(\d+(?:\.\d+)?)\s*mm)",
        std::regex_constants::icase);
    std::smatch dims;
    const bool hasDims = std::regex_search(text, dims, kDims);
    static const std::regex kThick(R"((\d+(?:\.\d+)?)\s*mm\s+thick)",
                                   std::regex_constants::icase);
    std::smatch thick;

    const bool hasVerb = contains(lowered, "create") || contains(lowered, "generate") ||
                         contains(lowered, "make") || contains(lowered, "build") ||
                         contains(lowered, "design") || !forcedDomain.empty();
    if (hasDims || hasVerb) {
        intent.operation = "create_board";
        if (hasDims) {
            intent.parameters["width_mm"] = std::stod(dims[1].str());
            intent.parameters["height_mm"] = std::stod(dims[2].str());
        }
        if (std::regex_search(text, thick, kThick)) {
            intent.parameters["thickness_mm"] = std::stod(thick[1].str());
        }
        const core::Json parts = collectParts();
        if (!parts.empty()) {
            core::Json components = core::Json::array();
            for (const auto& part : parts) {
                components.push_back(core::Json{{"part", part}});
            }
            intent.parameters["components"] = components;
        }
        if (!hasDims) {
            // "Create a PCB" without dimensions: honest gap, parts kept.
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"width_mm", "height_mm"};
            result.intent.confidence = 0.5;
            result.errors.push_back(
                "Incomplete PCB request: board dimensions are missing");
            return result;
        }
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        core::Logger::instance().info(
            "intelligence", "requirement parsed",
            core::Json{{"domain", "pcb"},
                       {"operation", intent.operation},
                       {"status", "VALID"}});
        return result;
    }

    // PCB keyword without an actionable verb: incomplete, not invalid.
    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Incomplete;
    result.intent.status = ParseStatus::Incomplete;
    result.intent.missing = {"operation", "width_mm", "height_mm"};
    result.intent.confidence = 0.4;
    result.errors.push_back("Incomplete PCB request: no board operation recognized");
    return result;
}

ParseResult RequirementParser::tryFirmwareRequest(const std::string& text,
                                                  const std::string& lowered,
                                                  const std::string& forcedDomain) const {
    if (!forcedDomain.empty() && forcedDomain != "firmware") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_firmware_content__");
        return sentinel;
    }

    // Claim only explicit firmware signals. Never invent MCU/pin data.
    const bool mentionsFirmware = contains(lowered, "firmware");
    static const std::regex kConfigureGpio(
        R"(configure\s+(?:gpio|pin)\s+\d+|gpio\s*\d+\s+as\s+(?:an?\s+)?(?:output|input))",
        std::regex_constants::icase);
    static const std::regex kPwmControl(
        R"((?:create|configure|pwm)\s+pwm|(?:pwm\s+control)|pwm\s+on\s+gpio)",
        std::regex_constants::icase);
    static const std::regex kI2cConfigure(
        R"((?:configure\s+i2c|i2c\s+on\s+sda)|(?:sda\s+\d+.*scl\s+\d+)|(?:scl\s+\d+.*sda\s+\d+))",
        std::regex_constants::icase);
    static const std::regex kUartConfigure(
        R"((?:uart\s+communication|serial\s+communication|configure\s+uart|(?:tx|rx)\s+gpio\s*\d+))",
        std::regex_constants::icase);
    const bool configureGpio = std::regex_search(text, kConfigureGpio);
    const bool pwmControl = std::regex_search(text, kPwmControl);
    const bool i2cConfigure = std::regex_search(text, kI2cConfigure);
    const bool uartConfigure = std::regex_search(text, kUartConfigure) &&
                               (mentionsFirmware || contains(lowered, "uart") ||
                                contains(lowered, "serial"));
    const bool selectMcu = contains(lowered, "select") && contains(lowered, "mcu");

    const bool claims =
        forcedDomain == "firmware" || mentionsFirmware || configureGpio ||
        pwmControl || i2cConfigure || uartConfigure || selectMcu;
    if (!claims) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_firmware_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "firmware";
    intent.object = "firmware_project";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    // MCU: only an explicitly named supported model.
    static const std::regex kMcu(
        R"(\b(ESP32|STM32F401RE|RP2040)\b)", std::regex_constants::icase);
    std::smatch mcuMatch;
    const bool hasMcu = std::regex_search(text, mcuMatch, kMcu);
    if (hasMcu) {
        std::string model = mcuMatch[1].str();
        for (auto& c : model) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        // Canonical spellings.
        if (model == "STM32F401RE") {
            model = "STM32F401RE";
        } else if (model == "RP2040") {
            model = "RP2040";
        } else if (model == "ESP32") {
            model = "ESP32";
        }
        intent.parameters["mcu"] = model;
    }

    // GPIO pin reference: "GPIO 2", "GPIO18", "PA5", "GP4".
    static const std::regex kPin(
        R"(\b(?:gpio|gp|pa|pb)\s*(\d+)\b)", std::regex_constants::icase);
    // Named pin roles: "SDA 21", "SCL 22", "TX GPIO1", "RX GPIO3".
    static const std::regex kRolePin(
        R"(\b(sda|scl|tx|rx)\s+(?:gpio\s*)?(\d+)\b)", std::regex_constants::icase);

    std::vector<std::pair<std::string, std::string>> rolePins;  // role -> pin name
    auto begin = std::sregex_iterator(text.begin(), text.end(), kRolePin);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        std::string role = (*it)[1].str();
        for (auto& c : role) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::string num = (*it)[2].str();
        const std::string prefix =
            (role == "sda" || role == "scl") ? "GPIO" : "GPIO";
        rolePins.emplace_back(role, prefix + num);
        intent.parameters[role] = prefix + num;
    }

    // Generic pin when no role pins consumed the GPIO match.
    std::smatch pinMatch;
    bool hasGenericPin = false;
    if (std::regex_search(text, pinMatch, kPin)) {
        // Prefer the first GPIO that is not already claimed by a role.
        auto beginP = std::sregex_iterator(text.begin(), text.end(), kPin);
        for (auto it = beginP; it != end; ++it) {
            const std::string num = (*it)[1].str();
            std::string upperText = (*it)[0].str();
            for (auto& c : upperText) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            std::string pinName;
            if (upperText.rfind("PA", 0) == 0 || upperText.rfind("PB", 0) == 0) {
                pinName = upperText.substr(0, 2) + num;
            } else if (upperText.rfind("GP", 0) == 0 &&
                       upperText.rfind("GPIO", 0) != 0) {
                pinName = "GP" + num;
            } else {
                pinName = "GPIO" + num;
            }
            // Skip pins already used as role pins (e.g. "SDA 21").
            bool usedAsRole = false;
            for (const auto& [role, rp] : rolePins) {
                (void)role;
                if (rp == pinName) {
                    usedAsRole = true;
                    break;
                }
            }
            if (!usedAsRole) {
                intent.parameters["pin"] = pinName;
                hasGenericPin = true;
                break;
            }
        }
    }

    // Direction: "as output" / "as input".
    static const std::regex kDirection(
        R"(\bas\s+(?:an?\s+)?(output|input)\b|\bas\s+(out|in)\b)",
        std::regex_constants::icase);
    std::smatch dirMatch;
    const bool hasDirection = std::regex_search(text, dirMatch, kDirection);
    if (hasDirection) {
        std::string dir = dirMatch[1].matched ? dirMatch[1].str() : dirMatch[2].str();
        for (auto& c : dir) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        intent.parameters["direction"] =
            (dir == "output" || dir == "out") ? "out" : "in";
    }

    // Baud rate when explicitly provided.
    static const std::regex kBaud(R"((\d+)\s*baud)", std::regex_constants::icase);
    std::smatch baudMatch;
    if (std::regex_search(text, baudMatch, kBaud)) {
        try {
            intent.parameters["baud"] = std::stoi(baudMatch[1].str());
        } catch (...) {
            // Ignore unparseable baud; engine validates.
        }
    }

    auto finish = [&](ParseStatus status, const std::vector<std::string>& missing,
                      double confidence) -> ParseResult {
        ParseResult result;
        intent.status = status;
        result.intent = intent;
        result.status = status;
        result.intent.missing = missing;
        result.intent.confidence = confidence;
        if (status == ParseStatus::Valid) {
            core::Logger::instance().info(
                "intelligence", "requirement parsed",
                core::Json{{"domain", "firmware"},
                           {"operation", intent.operation},
                           {"status", "VALID"}});
        } else {
            core::Json missingJson = core::Json::array();
            for (const auto& item : missing) {
                missingJson.push_back(item);
                result.errors.push_back("Missing firmware requirement: " + item);
            }
            core::Logger::instance().warning(
                "intelligence", "requirement parse incomplete",
                core::Json{{"domain", "firmware"}, {"missing", missingJson}});
        }
        return result;
    };

    // --- Operation selection from explicit content only ---
    if (selectMcu) {
        intent.operation = "select_mcu";
        if (!hasMcu) {
            return finish(ParseStatus::Incomplete, {"mcu"}, 0.5);
        }
        return finish(ParseStatus::Valid, {}, 0.9);
    }

    if (contains(lowered, "validate")) {
        intent.operation = "validate_project";
        return finish(ParseStatus::Valid, {}, 0.85);
    }

    if (configureGpio && (hasDirection || contains(lowered, "gpio"))) {
        intent.operation = "configure_pin";
        intent.parameters["peripheral"] = "gpio";
        if (!hasGenericPin && !hasDirection) {
            return finish(ParseStatus::Incomplete, {"pin", "direction"}, 0.4);
        }
        std::vector<std::string> missing;
        if (!hasGenericPin) {
            missing.push_back("pin");
        }
        if (!hasDirection) {
            missing.push_back("direction");
        }
        if (!missing.empty()) {
            return finish(ParseStatus::Incomplete, missing, 0.5);
        }
        // Function label derived from the provided direction (not hardware
        // invention: it is a symbolic name the engine accepts).
        const std::string dir = intent.parameters.value("direction", "out");
        if (!intent.parameters.contains("function")) {
            intent.parameters["function"] =
                dir == "out" ? "gpio_out" : "gpio_in";
        }
        return finish(ParseStatus::Valid, {}, 0.9);
    }

    if (pwmControl) {
        intent.operation = "configure_peripheral";
        intent.parameters["kind"] = "pwm";
        if (!intent.parameters.contains("peripheral")) {
            intent.parameters["peripheral"] = "PWM";
        }
        if (!hasGenericPin) {
            return finish(ParseStatus::Incomplete, {"pin"}, 0.5);
        }
        if (!intent.parameters.contains("freq_hz")) {
            intent.parameters["freq_hz"] = 1000;
        }
        return finish(ParseStatus::Valid, {}, 0.9);
    }

    if (i2cConfigure ||
        (contains(lowered, "i2c") &&
         (intent.parameters.contains("sda") || intent.parameters.contains("scl")))) {
        intent.operation = "configure_peripheral";
        intent.parameters["kind"] = "i2c";
        if (!intent.parameters.contains("peripheral")) {
            intent.parameters["peripheral"] = "I2C0";
        }
        std::vector<std::string> missing;
        if (!intent.parameters.contains("sda")) {
            missing.push_back("sda");
        }
        if (!intent.parameters.contains("scl")) {
            missing.push_back("scl");
        }
        if (!missing.empty()) {
            return finish(ParseStatus::Incomplete, missing, 0.5);
        }
        return finish(ParseStatus::Valid, {}, 0.9);
    }

    if (uartConfigure ||
        (contains(lowered, "uart") && intent.parameters.contains("tx"))) {
        intent.operation = "configure_peripheral";
        intent.parameters["kind"] = "uart";
        if (!intent.parameters.contains("peripheral")) {
            intent.parameters["peripheral"] = "UART0";
        }
        std::vector<std::string> missing;
        if (!intent.parameters.contains("tx")) {
            missing.push_back("tx");
        }
        if (!intent.parameters.contains("rx")) {
            missing.push_back("rx");
        }
        if (!missing.empty()) {
            return finish(ParseStatus::Incomplete, missing, 0.5);
        }
        if (!intent.parameters.contains("baud")) {
            intent.parameters["baud"] = 115200;
        }
        return finish(ParseStatus::Valid, {}, 0.9);
    }

    // Generate/create/build firmware (entry point: create_project; the
    // UI/job chain continues with select_mcu/configure_*).
    if (mentionsFirmware ||
        (forcedDomain == "firmware" &&
         (contains(lowered, "create") || contains(lowered, "generate") ||
          contains(lowered, "build")))) {
        intent.operation = "create_project";
        if (!intent.parameters.contains("name")) {
            intent.parameters["name"] = "firmware_project";
        }
        if (!hasMcu) {
            return finish(ParseStatus::Incomplete, {"mcu"}, 0.5);
        }
        // Stage any explicit peripheral config for downstream chaining.
        return finish(ParseStatus::Valid, {}, 0.9);
    }

    // Firmware keywords with no recognized operation.
    intent.operation = "create_project";
    if (!intent.parameters.contains("name")) {
        intent.parameters["name"] = "firmware_project";
    }
    return finish(ParseStatus::Incomplete, {"operation", "mcu"}, 0.4);
}

ParseResult RequirementParser::tryVisionRequest(const std::string& text,
                                                const std::string& lowered,
                                                const std::string& forcedDomain) const {
    if (!forcedDomain.empty() && forcedDomain != "vision") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_vision_content__");
        return sentinel;
    }

    const bool hasImage = contains(lowered, "image") || contains(lowered, "photo") || contains(lowered, "picture");
    const bool hasResize = contains(lowered, "resize") || contains(lowered, "scale");
    const bool hasGrayscale = contains(lowered, "grayscale") || contains(lowered, "grey") || contains(lowered, "gray");
    const bool hasEdge = contains(lowered, "edge") || contains(lowered, "canny");
    const bool hasStats = contains(lowered, "dimension") || contains(lowered, "statistic") || contains(lowered, "channel") || contains(lowered, "analyze");
    
    if (forcedDomain.empty() && !hasImage && !hasResize && !hasGrayscale && !hasEdge && !hasStats) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_vision_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "vision";
    intent.object = "image";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    if (hasResize) {
        intent.operation = "resize_image";
        
        static const std::regex kSize(R"((\d+)\s*[x\*]\s*(\d+))", std::regex_constants::icase);
        std::smatch m;
        if (std::regex_search(text, m, kSize)) {
            intent.parameters["width"] = std::stoi(m[1].str());
            intent.parameters["height"] = std::stoi(m[2].str());
        } else {
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"width", "height"};
            result.errors.push_back("Incomplete vision request: resize is missing dimensions");
            return result;
        }
    } else if (hasEdge) {
        intent.operation = "edge_detect";
    } else if (hasStats) {
        intent.operation = "image_statistics";
    } else if (hasGrayscale) {
        intent.operation = "grayscale";
    } else {
        intent.operation = "load_image";
    }

    // Try extracting path if mentioned like "image path/to/img.png"
    static const std::regex kPath(R"((?:file|image)\s+['"]?([^'"]+\.[a-zA-Z0-9]+)['"]?)", std::regex_constants::icase);
    std::smatch m2;
    if (std::regex_search(text, m2, kPath)) {
        intent.parameters["path"] = m2[1].str();
    }

    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Valid;
    result.intent.status = ParseStatus::Valid;
    result.intent.confidence = 0.9;
    return result;
}

ParseResult RequirementParser::tryResearchRequest(const std::string& text,
                                                  const std::string& lowered,
                                                  const std::string& forcedDomain) const {
    if (!forcedDomain.empty() && forcedDomain != "research") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_research_content__");
        return sentinel;
    }

    const bool hasResearch = contains(lowered, "research") ||
                             contains(lowered, "literature") ||
                             contains(lowered, "papers");
    const bool hasSummar = contains(lowered, "summar");
    const bool hasIndexKeyword = contains(lowered, "index") || contains(lowered, "add document") ||
                                 contains(lowered, "add a document") ||
                                 contains(lowered, "store document") ||
                                 contains(lowered, "store the document");
    const bool hasDocument =
        contains(lowered, "document") || contains(lowered, "doc ") ||
        contains(lowered, "note") || contains(lowered, "text");
    const bool hasIndex = hasIndexKeyword && hasDocument;
    const bool hasClear = contains(lowered, "clear") && contains(lowered, "index");
    const bool hasList =
        (contains(lowered, "list") || contains(lowered, "show") ||
         contains(lowered, "what documents")) &&
        (contains(lowered, "document") || contains(lowered, "indexed"));
    const bool hasExport = contains(lowered, "export") && contains(lowered, "index");
    const bool hasSearch =
        contains(lowered, "search") || contains(lowered, "look up") ||
        contains(lowered, "query the") ||
        (contains(lowered, "find") && (contains(lowered, "document") ||
                                       contains(lowered, "papers") ||
                                       contains(lowered, "notes")));
    const bool hasDescribe = contains(lowered, "describe") || contains(lowered, "capabilit");

    if (forcedDomain.empty() && !hasResearch && !hasSummar && !hasIndex && !hasClear &&
        !hasList && !hasExport && !hasSearch && !hasDescribe) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_research_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "research";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    auto extractQuoted = [&text]() -> std::string {
        static const std::regex kQuote(R"(["']([^"'\n]{1,})["'])",
                                       std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(text, match, kQuote)) {
            return trim(match[1].str());
        }
        return {};
    };
    auto extractAllQuoted = [&text]() -> std::vector<std::string> {
        static const std::regex kQuote(R"(["']([^"'\n]{1,})["'])",
                                       std::regex_constants::icase);
        std::vector<std::string> found;
        for (std::sregex_iterator it(text.begin(), text.end(), kQuote), end; it != end; ++it) {
            found.push_back(trim((*it)[1].str()));
        }
        return found;
    };
    auto extractAfterKeywords = [&text]() -> std::string {
        // Requires an explicit preposition so "Search the index" stays
        // incomplete instead of inventing a query from stray words.
        static const std::regex kAfter(
            R"((?:for|about|on|query)\s+(?:documents?|papers?|notes?|results?|information)?\s*(?:about|on|that|which)?\s*(.+)$)",
            std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(text, match, kAfter) && match[1].matched) {
            std::string query = trim(match[1].str());
            while (!query.empty() && (query.back() == '"' || query.back() == '\'' ||
                                      query.back() == '.' || query.back() == '?' ||
                                      query.back() == '!')) {
                query.pop_back();
            }
            query = trim(query);
            if (!query.empty()) {
                return query;
            }
        }
        return {};
    };

    if (hasDescribe && !hasSummar && !hasSearch && !hasIndex) {
        intent.operation = "describe";
        intent.object = "document";
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    if (hasSummar) {
        intent.operation = "summarize_results";
        intent.object = "query";
        const std::string quoted = extractQuoted();
        const std::string query = quoted.empty() ? extractAfterKeywords() : quoted;
        if (query.empty()) {
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"query"};
            result.errors.push_back("Incomplete research request: summarize is missing a query");
            return result;
        }
        intent.parameters["query"] = query;
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    if (hasClear) {
        intent.operation = "clear_index";
        intent.object = "index";
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    if (hasExport) {
        intent.operation = "export_index";
        intent.object = "index";
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    if (hasList) {
        intent.operation = "list_documents";
        intent.object = "document";
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    if (hasIndex) {
        intent.operation = "index_document";
        intent.object = "document";
        const std::vector<std::string> quoted = extractAllQuoted();
        if (quoted.empty()) {
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"title", "text"};
            result.errors.push_back(
                "Incomplete research request: index document needs a quoted title and text");
            return result;
        }
        intent.parameters["title"] = quoted[0];
        if (quoted.size() >= 2) {
            intent.parameters["text"] = quoted[1];
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Valid;
            result.intent.status = ParseStatus::Valid;
            result.intent.confidence = 0.9;
            return result;
        }
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Incomplete;
        result.intent.status = ParseStatus::Incomplete;
        result.intent.missing = {"text"};
        result.errors.push_back("Incomplete research request: index document is missing text");
        return result;
    }

    // Default research operation: search.
    intent.operation = "search";
    intent.object = "query";
    const std::string quotedQuery = extractQuoted();
    const std::string query = quotedQuery.empty() ? extractAfterKeywords() : quotedQuery;
    if (query.empty()) {
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Incomplete;
        result.intent.status = ParseStatus::Incomplete;
        result.intent.missing = {"query"};
        result.errors.push_back("Incomplete research request: search is missing a query");
        return result;
    }
    intent.parameters["query"] = query;
    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Valid;
    result.intent.status = ParseStatus::Valid;
    result.intent.confidence = 0.9;
    return result;
}

ParseResult RequirementParser::tryRoboticsRequest(const std::string& text,
                                                  const std::string& lowered,
                                                  const std::string& forcedDomain) const {
    if (!forcedDomain.empty() && forcedDomain != "robotics") {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_robotics_content__");
        return sentinel;
    }

    auto wordMatch = [&lowered](const std::string& word) {
        return lowered.find(word) != std::string::npos;
    };
    static const std::regex kFkWord(R"(\bfk\b)", std::regex_constants::icase);
    const bool hasFkWord = std::regex_search(text, kFkWord);
    const bool hasDescribe = wordMatch("describe") || wordMatch("capabilit");
    const bool hasRobot = wordMatch("robot");
    const bool hasUrdf = wordMatch("urdf");
    const bool hasTrajectory = wordMatch("trajectory") || wordMatch("joint space");
    const bool hasFk = wordMatch("forward kinem") || wordMatch("kinematics") ||
                       wordMatch("end effector") || wordMatch("end-effector") ||
                       hasFkWord || wordMatch("joint angles") || wordMatch("dh param");
    const bool hasRobotics = wordMatch("robotics") || hasUrdf || hasTrajectory || hasFk ||
                             (hasDescribe && hasRobot);

    if (forcedDomain.empty() && !hasRobotics) {
        ParseResult sentinel;
        sentinel.intent = makeBaseIntent(text);
        sentinel.status = ParseStatus::Invalid;
        sentinel.errors.push_back("__no_robotics_content__");
        return sentinel;
    }

    Intent intent = makeBaseIntent(text);
    intent.domain = "robotics";
    intent.priority = detectPriority(lowered);
    intent.outputs = detectOutputs(lowered);

    auto parseNumberList = [](std::string raw) -> std::vector<double> {
        while (!raw.empty() && (raw.front() == '[' || raw.front() == ' ')) {
            raw.erase(raw.begin());
        }
        while (!raw.empty() && (raw.back() == ']' || raw.back() == ' ' ||
                                raw.back() == ',')) {
            raw.pop_back();
        }
        std::vector<double> values;
        std::string token;
        auto flush = [&]() {
            while (!token.empty() && token.front() == ' ') {
                token.erase(token.begin());
            }
            while (!token.empty() && token.back() == ' ') {
                token.pop_back();
            }
            if (!token.empty()) {
                try {
                    values.push_back(std::stod(token));
                } catch (...) {
                    values.clear();
                }
            }
            token.clear();
        };
        for (const char c : raw) {
            if (c == ',') {
                flush();
            } else {
                token.push_back(c);
            }
        }
        flush();
        return values;
    };
    auto toJsonArray = [](const std::vector<double>& values) {
        core::Json arr = core::Json::array();
        for (const double value : values) {
            arr.push_back(value);
        }
        return arr;
    };
    auto extractQuoted = [&text]() -> std::string {
        static const std::regex kQuote(R"(["']([^"'\n]{1,})["'])",
                                       std::regex_constants::icase);
        std::smatch match;
        if (std::regex_search(text, match, kQuote)) {
            return trim(match[1].str());
        }
        return {};
    };

    if (hasDescribe && !hasUrdf && !hasTrajectory) {
        intent.operation = "describe";
        intent.object = "robot";
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    if (hasUrdf) {
        intent.operation = "export_urdf";
        intent.object = "robot";
        std::string name = extractQuoted();
        if (name.empty()) {
            static const std::regex kNamed(R"(named\s+([A-Za-z0-9_]+))",
                                           std::regex_constants::icase);
            std::smatch match;
            if (std::regex_search(text, match, kNamed)) {
                name = trim(match[1].str());
            }
        }
        if (!name.empty()) {
            intent.parameters["robot_name"] = name;
        }
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    if (hasTrajectory) {
        intent.operation = "plan_trajectory";
        intent.object = "robot";
        static const std::regex kFromTo(
            R"(from\s+(\[?[-\d.,+\s]+\]?)\s+to\s+(\[?[-\d.,+\s]+\]?))",
            std::regex_constants::icase);
        std::smatch match;
        const std::vector<double> start =
            std::regex_search(text, match, kFromTo) ? parseNumberList(match[1].str())
                                                    : std::vector<double>{};
        const std::vector<double> goal =
            std::regex_search(text, match, kFromTo) ? parseNumberList(match[2].str())
                                                    : std::vector<double>{};
        if (start.empty() || goal.empty()) {
            ParseResult result;
            result.intent = intent;
            result.status = ParseStatus::Incomplete;
            result.intent.status = ParseStatus::Incomplete;
            result.intent.missing = {"joint_start", "joint_goal"};
            result.errors.push_back(
                "Incomplete robotics request: trajectory needs 'from <joints> to <joints>'");
            return result;
        }
        intent.parameters["joint_start"] = toJsonArray(start);
        intent.parameters["joint_goal"] = toJsonArray(goal);
        static const std::regex kDuration(
            R"((\d+(?:\.\d+)?)\s*(?:seconds?|secs?|s\b))", std::regex_constants::icase);
        if (std::regex_search(text, match, kDuration)) {
            try {
                intent.parameters["duration_s"] = std::stod(match[1].str());
            } catch (...) {
            }
        }
        ParseResult result;
        result.intent = intent;
        result.status = ParseStatus::Valid;
        result.intent.status = ParseStatus::Valid;
        result.intent.confidence = 0.9;
        return result;
    }

    // Default robotics operation: forward kinematics.
    intent.operation = "forward_kinematics";
    intent.object = "robot";
    static const std::regex kAngles(
        R"((?:joint\s+angles?|angles?)\s*(?:of\s*)?=?\s*\[?([-\d.,\s]+))",
        std::regex_constants::icase);
    std::smatch match;
    if (std::regex_search(text, match, kAngles)) {
        const std::vector<double> angles = parseNumberList(match[1].str());
        if (!angles.empty()) {
            intent.parameters["joint_angles"] = toJsonArray(angles);
        }
    }
    ParseResult result;
    result.intent = intent;
    result.status = ParseStatus::Valid;
    result.intent.status = ParseStatus::Valid;
    result.intent.confidence = 0.9;
    return result;
}

}  // namespace trinity::intelligence

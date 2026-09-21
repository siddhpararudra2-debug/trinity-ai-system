#include "trinity/intelligence/IntentRouter.hpp"

#include <algorithm>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/engines/EngineRegistry.hpp"

namespace trinity::intelligence {
namespace {

RouteResult reject(const std::string& engine, const std::string& operation,
                   const std::string& status, const std::string& reason) {
    RouteResult out;
    out.routed = false;
    out.engine = engine;
    out.operation = operation;
    out.status = status;
    out.reason = reason;
    return out;
}

}  // namespace

core::Json RouteResult::toJson() const {
    return core::Json{{"routed", routed},
                      {"engine", engine},
                      {"operation", operation},
                      {"capability", capability},
                      {"status", status},
                      {"reason", reason},
                      {"tool_call", toolCall.toJson()}};
}

RouteResult RouteResult::fromJson(const core::Json& json) {
    RouteResult out;
    out.routed = json.value("routed", false);
    out.engine = json.value("engine", "");
    out.operation = json.value("operation", "");
    out.capability = json.value("capability", "");
    out.status = json.value("status", "REJECTED");
    out.reason = json.value("reason", "");
    if (json.contains("tool_call")) {
        out.toolCall = ToolCall::fromJson(json["tool_call"]);
    }
    return out;
}

core::Json IntentRouter::translateCadParams(const Intent& intent) {
    // Intent uses normalized canonical keys (overall_size_mm, ...);
    // FrameParams/CadEngine use bare names (overall_size, arm_width, ...).
    // Mapping (documented, single place):
    //   overall_size_mm      -> overall_size
    //   arm_thickness_mm     -> arm_width
    //   width_mm             -> arm_width (quad alias)
    //   thickness_mm         -> plate_thickness
    //   plate_thickness_mm   -> plate_thickness
    //   diameter_mm          -> motor_mount_diameter
    //   spacing_mm           -> fc_mount_spacing
    //   center_plate_size_mm -> center_plate_size
    core::Json frameParams = core::Json::object();
    const auto copy = [&](const std::string& from, const std::string& to) {
        if (intent.parameters.contains(from) && intent.parameters[from].is_number()) {
            frameParams[to] = intent.parameters[from].get<double>();
        }
    };
    copy("overall_size_mm", "overall_size");
    copy("arm_thickness_mm", "arm_width");
    if (!frameParams.contains("arm_width")) {
        copy("width_mm", "arm_width");
    }
    copy("thickness_mm", "plate_thickness");
    copy("plate_thickness_mm", "plate_thickness");
    copy("diameter_mm", "motor_mount_diameter");
    copy("spacing_mm", "fc_mount_spacing");
    copy("center_plate_size_mm", "center_plate_size");
    if (intent.parameters.contains("motor_count") && intent.parameters["motor_count"].is_number()) {
        frameParams["motor_count"] = intent.parameters["motor_count"].get<double>();
    }
    return frameParams;
}

RouteResult IntentRouter::route(const Intent& intent,
                                const engines::EngineRegistry& registry) const {
    // Only validated, complete intents may route. Incomplete/ambiguous
    // intents carry missing requirements that must be resolved first.
    if (intent.status != ParseStatus::Valid) {
        core::Logger::instance().warning(
            "intelligence", "intent routing rejected: intent not valid",
            core::Json{{"intent_id", intent.intentId}, {"status", toString(intent.status)}});
        return reject("", "", "REJECTED",
                      "Intent status is " + toString(intent.status) +
                          "; resolve missing requirements before routing");
    }
    if (!intent.missing.empty()) {
        return reject("", "", "REJECTED", "Intent has unresolved missing requirements");
    }

    std::string engine;
    std::string operation = intent.operation;
    if (intent.domain == "cad") {
        engine = "cad";
    } else if (intent.domain == "math") {
        engine = "math";
    } else {
        return reject(intent.domain, operation, "REJECTED",
                      "Unsupported domain '" + intent.domain + "'; supported: cad, math");
    }

    if (!registry.has(engine)) {
        return reject(engine, operation, "REJECTED",
                      "Engine '" + engine + "' is not registered");
    }
    const std::vector<std::string> caps = registry.listCapabilities(engine);
    if (std::find(caps.begin(), caps.end(), operation) == caps.end()) {
        core::Json available = core::Json::array();
        for (const auto& cap : caps) {
            available.push_back(cap);
        }
        core::Logger::instance().warning(
            "intelligence", "intent routing rejected: unsupported operation",
            core::Json{{"engine", engine}, {"operation", operation}});
        return reject(engine, operation, "REJECTED",
                      "Engine '" + engine + "' has no capability '" + operation + "'");
    }

    if (intent.domain == "cad") {
        // CAD object gate: only quadcopter_frame is implemented in the
        // engine. Other parsed objects (e.g. plate) are syntactically
        // valid but must be rejected truthfully here, never faked.
        if (intent.operation == "generate" && intent.object != "quadcopter_frame") {
            core::Logger::instance().warning(
                "intelligence", "intent routing rejected: unsupported CAD object",
                core::Json{{"object", intent.object}});
            RouteResult out =
                reject(engine, operation, "CAPABILITY_UNAVAILABLE",
                       "CadEngine does not support object '" + intent.object +
                           "'; supported: [quadcopter_frame]");
            out.capability = operation;
            return out;
        }
        ToolCall call;
        call.toolCallId = core::newUuid();
        call.engine = "cad";
        call.operation = intent.operation;
        if (intent.operation == "describe") {
            call.parameters = core::Json::object();
        } else {
            core::Json frameParams = translateCadParams(intent);
            core::Json outputs = intent.outputs.is_array() ? intent.outputs
                                                           : core::Json::array();
            call.parameters = core::Json{{"type", intent.object},
                                         {"parameters", frameParams},
                                         {"outputs", outputs}};
        }
        RouteResult out;
        out.routed = true;
        out.engine = "cad";
        out.operation = intent.operation;
        out.capability = intent.operation;
        out.status = "ROUTED";
        out.toolCall = std::move(call);
        core::Logger::instance().info(
            "intelligence", "intent routed",
            core::Json{{"intent_id", intent.intentId}, {"engine", "cad"}});
        return out;
    }

    // Math domain: expression passes through verbatim; the engine owns
    // evaluation semantics.
    if (!intent.parameters.contains("expression") ||
        !intent.parameters["expression"].is_string()) {
        return reject(engine, operation, "REJECTED",
                      "Math intent requires a string 'expression' parameter");
    }
    ToolCall call;
    call.toolCallId = core::newUuid();
    call.engine = "math";
    call.operation = operation;
    call.parameters = core::Json{
        {"expression", intent.parameters["expression"].get<std::string>()}};
    if (intent.parameters.contains("variables")) {
        call.parameters["variables"] = intent.parameters["variables"];
    }
    RouteResult out;
    out.routed = true;
    out.engine = "math";
    out.operation = operation;
    out.capability = operation;
    out.status = "ROUTED";
    out.toolCall = std::move(call);
    core::Logger::instance().info(
        "intelligence", "intent routed",
        core::Json{{"intent_id", intent.intentId}, {"engine", "math"}});
    return out;
}

}  // namespace trinity::intelligence

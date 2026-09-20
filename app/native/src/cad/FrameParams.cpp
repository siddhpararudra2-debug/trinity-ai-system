#include "trinity/cad/FrameParams.hpp"

#include <cmath>

#include "trinity/core/Error.hpp"

namespace trinity::cad {

const std::map<std::string, double>& defaultFrameParams() {
    static const std::map<std::string, double> defaults = {
        {"overall_size", 50.0},  {"motor_count", 4.0},         {"arm_width", 5.0},
        {"plate_thickness", 1.5}, {"motor_mount_diameter", 6.0}, {"fc_mount_spacing", 25.0},
        {"center_plate_size", 26.0},
    };
    return defaults;
}

FrameParams FrameParams::fromRequest(const core::Json& raw) {
    FrameParams params;
    params.units = "mm";
    params.parameters = defaultFrameParams();

    if (!raw.is_null() && !raw.is_object()) {
        throw core::RequestValidationError("CAD parameters must be an object",
                                           {{"parameters", raw}}, "engines");
    }
    if (!raw.is_null()) {
        for (auto it = raw.begin(); it != raw.end(); ++it) {
            if (params.parameters.find(it.key()) == params.parameters.end()) {
                core::Json allowed = core::Json::array();
                for (const auto& [key, _] : params.parameters) {
                    allowed.push_back(key);
                }
                throw core::RequestValidationError(
                    "Unknown quadcopter_frame parameter '" + it.key() + "'",
                    {{"allowed", allowed}}, "engines");
            }
            if (!it.value().is_number()) {
                throw core::RequestValidationError(
                    "Parameter '" + it.key() + "' must be numeric",
                    {{"parameter", it.key()}}, "engines");
            }
            params.parameters[it.key()] = it.value().get<double>();
        }
    }

    auto& p = params.parameters;
    if (p["motor_count"] != 4.0) {
        throw core::RequestValidationError(
            "V1 only supports 4-motor (X) quadcopter frames",
            {{"motor_count", p["motor_count"]}}, "engines");
    }
    if (p["overall_size"] <= p["center_plate_size"]) {
        throw core::RequestValidationError(
            "overall_size must be larger than center_plate_size",
            {{"overall_size", p["overall_size"]},
             {"center_plate_size", p["center_plate_size"]}},
            "engines");
    }
    for (const char* key :
         {"overall_size", "center_plate_size", "arm_width", "plate_thickness",
          "motor_mount_diameter", "fc_mount_spacing"}) {
        const double value = p[key];
        if (!std::isfinite(value) || value <= 0.0) {
            throw core::RequestValidationError(
                std::string(key) + " must be a finite positive value",
                {{key, value}}, "engines");
        }
    }
    if (p["overall_size"] > 1000.0 || p["plate_thickness"] > 50.0) {
        throw core::RequestValidationError(
            "Frame dimensions are outside V1's supported engineering range",
            {{"overall_size", p["overall_size"]},
             {"plate_thickness", p["plate_thickness"]}},
            "engines");
    }
    if (p["fc_mount_spacing"] > p["center_plate_size"]) {
        throw core::RequestValidationError(
            "fc_mount_spacing must fit within center_plate_size",
            {{"fc_mount_spacing", p["fc_mount_spacing"]},
             {"center_plate_size", p["center_plate_size"]}},
            "engines");
    }
    return params;
}

core::Json FrameParams::toJson() const {
    core::Json paramsJson = core::Json::object();
    for (const auto& [key, value] : parameters) {
        paramsJson[key] = value;
    }
    return core::Json{{"type", "quadcopter_frame"}, {"units", units}, {"parameters", paramsJson}};
}

}  // namespace trinity::cad

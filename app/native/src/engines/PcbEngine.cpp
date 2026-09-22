#include "trinity/engines/PcbEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/pcb/KiCadExport.hpp"
#include "trinity/pcb/Validators.hpp"

namespace trinity::engines {
namespace fs = std::filesystem;

namespace {

// Simplified deterministic footprints (body extents + pad grids).
// These are starter patterns, not manufacturer-accurate land patterns.
pcb::Footprint esp32Footprint() {
    pcb::Footprint fp;
    fp.name = "ESP32-WROOM-32";
    fp.widthMm = 18.0;
    fp.heightMm = 25.5;
    for (int i = 0; i < 19; ++i) {
        const double y = -15.24 + i * 1.27 + 7.62;  // 19 pads, 1.27 pitch
        pcb::FootprintPad left;
        left.number = std::to_string(i + 1);
        left.posMm = {-7.5, y - 7.62};
        left.sizeMm = {1.0, 1.6};
        pcb::FootprintPad right;
        right.number = std::to_string(i + 20);
        right.posMm = {7.5, y - 7.62};
        right.sizeMm = {1.0, 1.6};
        fp.pads.push_back(left);
        fp.pads.push_back(right);
    }
    return fp;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

struct PartPreset {
    std::string footprint;
    pcb::ComponentType type;
    std::string refPrefix;
    std::string defaultValue;
};

const std::map<std::string, PartPreset>& partTable() {
    static const std::map<std::string, PartPreset> table = {
        {"esp32", {"ESP32-WROOM-32", pcb::ComponentType::Mcu, "U", "ESP32-WROOM-32"}},
        {"imu", {"IMU-QFN-24", pcb::ComponentType::Imu, "U", "MPU-6050"}},
        {"mpu6050", {"IMU-QFN-24", pcb::ComponentType::Imu, "U", "MPU-6050"}},
        {"mpu9250", {"IMU-QFN-24", pcb::ComponentType::Imu, "U", "MPU-9250"}},
        {"accelerometer", {"IMU-QFN-24", pcb::ComponentType::Imu, "U", "MPU-6050"}},
        {"gyroscope", {"IMU-QFN-24", pcb::ComponentType::Imu, "U", "MPU-6050"}},
        {"regulator", {"SOT-223", pcb::ComponentType::Regulator, "U", "AMS1117-3.3"}},
        {"ams1117", {"SOT-223", pcb::ComponentType::Regulator, "U", "AMS1117-3.3"}},
        {"ldo", {"SOT-223", pcb::ComponentType::Regulator, "U", "AMS1117-3.3"}},
        {"resistor", {"0603", pcb::ComponentType::Passive, "R", "10k"}},
        {"capacitor", {"0603", pcb::ComponentType::Passive, "C", "100n"}},
        {"led", {"0603", pcb::ComponentType::Passive, "D", "RED"}},
    };
    return table;
}

pcb::Footprint imuFootprint() {
    pcb::Footprint fp;
    fp.name = "IMU-QFN-24";
    fp.widthMm = 4.0;
    fp.heightMm = 4.0;
    int num = 1;
    for (int side = 0; side < 4; ++side) {
        for (int i = 0; i < 6; ++i) {
            const double off = -1.25 + i * 0.5;
            pcb::FootprintPad pad;
            pad.number = std::to_string(num++);
            pad.sizeMm = {0.25, 0.85};
            if (side == 0) {
                pad.posMm = {off, -2.0};
            } else if (side == 1) {
                pad.posMm = {2.0, off};
                pad.sizeMm = {0.85, 0.25};
            } else if (side == 2) {
                pad.posMm = {-off, 2.0};
            } else {
                pad.posMm = {-2.0, -off};
                pad.sizeMm = {0.85, 0.25};
            }
            fp.pads.push_back(pad);
        }
    }
    return fp;
}

pcb::Footprint sot223Footprint() {
    pcb::Footprint fp;
    fp.name = "SOT-223";
    fp.widthMm = 6.5;
    fp.heightMm = 7.0;
    const std::pair<const char*, double> pads[] = {
        {"1", -2.3}, {"2", 0.0}, {"3", 2.3}, {"4", 0.0},
    };
    for (const auto& [num, x] : pads) {
        pcb::FootprintPad pad;
        pad.number = num;
        pad.posMm = {x, num == std::string("4") ? 3.0 : -2.5};
        pad.sizeMm = {1.6, 2.0};
        fp.pads.push_back(pad);
    }
    return fp;
}

pcb::Footprint size0603Footprint() {
    pcb::Footprint fp;
    fp.name = "0603";
    fp.widthMm = 1.6;
    fp.heightMm = 0.8;
    for (const auto& [num, x] : {std::pair<const char*, double>{"1", -0.8},
                                 {"2", 0.8}}) {
        pcb::FootprintPad pad;
        pad.number = num;
        pad.posMm = {x, 0.0};
        pad.sizeMm = {0.9, 0.9};
        fp.pads.push_back(pad);
    }
    return fp;
}

const std::map<std::string, pcb::Footprint>& footprintTable() {
    static const std::map<std::string, pcb::Footprint> table = {
        {"ESP32-WROOM-32", esp32Footprint()},
        {"IMU-QFN-24", imuFootprint()},
        {"SOT-223", sot223Footprint()},
        {"0603", size0603Footprint()},
    };
    return table;
}

std::vector<std::string> presetNames() {
    std::vector<std::string> names;
    for (const auto& [name, fp] : footprintTable()) {
        names.push_back(name);
    }
    return names;
}

double finiteNumber(const core::Json& params, const std::string& key,
                    const std::string& operation) {
    if (!params.contains(key) || !params[key].is_number()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires a numeric '" + key + "'",
            {{"operation", operation}}, "engines");
    }
    const double value = params[key].get<double>();
    if (!std::isfinite(value)) {
        throw core::RequestValidationError("Parameter '" + key + "' must be finite",
                                           {{"operation", operation}}, "engines");
    }
    return value;
}

pcb::PcbDesign designParam(const core::Json& params, const std::string& operation) {
    if (!params.contains("design") || !params["design"].is_object()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires a 'design' object",
            {{"operation", operation}}, "engines");
    }
    try {
        return pcb::PcbDesign::fromJson(params["design"]);
    } catch (const std::exception& exc) {
        throw core::RequestValidationError(
            std::string("Invalid design object: ") + exc.what(),
            {{"operation", operation}}, "engines");
    }
}

std::string shortId() {
    std::string uuid = core::newUuid();
    uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
    return uuid.substr(0, 8);
}

/// Build component pins from a preset footprint's pads, applying
/// known power-net names for the starter parts.
std::vector<pcb::Pin> pinsForPreset(const std::string& footprint,
                                    const std::string& part) {
    const auto& table = footprintTable();
    const auto it = table.find(footprint);
    std::vector<pcb::Pin> pins;
    if (it == table.end()) {
        return pins;
    }
    const std::string partLower = lower(part);
    for (const auto& pad : it->second.pads) {
        pcb::Pin pin;
        pin.number = pad.number;
        pin.name = "P" + pad.number;
        pin.electrical = pcb::PinElectricalType::Passive;
        pin.posMm = pad.posMm;
        if (footprint == "ESP32-WROOM-32") {
            if (pad.number == "1") {
                pin.name = "3V3";
                pin.electrical = pcb::PinElectricalType::PowerIn;
            } else if (pad.number == "19" || pad.number == "20" ||
                       pad.number == "38") {
                pin.name = "GND";
                pin.electrical = pcb::PinElectricalType::PowerIn;
            }
        } else if (footprint == "IMU-QFN-24") {
            if (pad.number == "1") {
                pin.name = "VDD";
                pin.electrical = pcb::PinElectricalType::PowerIn;
            } else if (pad.number == "13") {
                pin.name = "GND";
                pin.electrical = pcb::PinElectricalType::PowerIn;
            } else if (pad.number == "2") {
                pin.name = "SCL";
            } else if (pad.number == "3") {
                pin.name = "SDA";
            }
        } else if (footprint == "SOT-223") {
            if (pad.number == "1") {
                pin.name = "VIN";
                pin.electrical = pcb::PinElectricalType::PowerIn;
            } else if (pad.number == "2" || pad.number == "4") {
                pin.name = "GND";
                pin.electrical = pcb::PinElectricalType::PowerIn;
            } else if (pad.number == "3") {
                pin.name = "VOUT";
                pin.electrical = pcb::PinElectricalType::PowerOut;
            }
        }
        (void)partLower;
        pins.push_back(pin);
    }
    return pins;
}

}  // namespace

PcbEngine::PcbEngine() {
    name_ = "pcb";
    version_ = "0.1.0";
    capabilities_ = {"describe",      "create_board", "add_component", "add_net",
                     "place_component", "validate_design", "export"};
}

EngineResult PcbEngine::execute(const EngineRequest& request) {
    try {
        if (request.operation == "describe") {
            core::Json presets = core::Json::array();
            for (const auto& name : presetNames()) {
                presets.push_back(name);
            }
            EngineResult out =
                successResult(request, {{"engine", name_},
                                        {"version", version_},
                                        {"capabilities", capabilities_},
                                        {"footprints", presets}});
            out.validation = validate(out);
            return out;
        }
        requireCapability(request);
        if (request.operation == "create_board") {
            return executeCreateBoard(request);
        }
        if (request.operation == "add_component") {
            return executeAddComponent(request);
        }
        if (request.operation == "add_net") {
            return executeAddNet(request);
        }
        if (request.operation == "place_component") {
            return executePlaceComponent(request);
        }
        if (request.operation == "validate_design") {
            return executeValidateDesign(request);
        }
        if (request.operation == "export") {
            return executeExport(request);
        }
        EngineResult out = capabilityUnavailable(
            request, "PCB operation '" + request.operation + "' is not implemented yet");
        core::Logger::instance().warning(
            "engines", "pcb unsupported operation",
            core::Json{{"operation", request.operation}});
        return out;
    } catch (const core::TrinityError& exc) {
        EngineResult out = failureResult(
            request, exc.what(), exc.toJson().value("details", core::Json::object()));
        out.errors.clear();
        out.addError(exc.info());
        out.validation = validate(out);
        return out;
    }
}

EngineResult PcbEngine::executeCreateBoard(const EngineRequest& request) {
    requireParams(request, {"width_mm", "height_mm"});
    const double width = finiteNumber(request.parameters, "width_mm", "create_board");
    const double height = finiteNumber(request.parameters, "height_mm", "create_board");
    double thickness = 1.6;
    if (request.parameters.contains("thickness_mm")) {
        thickness = finiteNumber(request.parameters, "thickness_mm", "create_board");
    }
    if (width <= 0.0 || height <= 0.0) {
        throw core::RequestValidationError("Board width and height must be positive",
                                           {{"width_mm", width}, {"height_mm", height}},
                                           "engines");
    }
    if (thickness <= 0.0 || thickness > 10.0) {
        throw core::RequestValidationError("Board thickness must be in (0, 10] mm",
                                           {{"thickness_mm", thickness}}, "engines");
    }
    pcb::PcbDesign design;
    design.board.widthMm = width;
    design.board.heightMm = height;
    design.board.thicknessMm = thickness;
    design.metadata = core::Json{{"created_by", "pcb.create_board"}};

    // Optional starter parts: [{part, ref?, value?}] with deterministic refs.
    std::map<std::string, int> counters;
    if (request.parameters.contains("components") &&
        request.parameters["components"].is_array()) {
        for (const auto& item : request.parameters["components"]) {
            if (!item.is_object() || !item.contains("part") || !item["part"].is_string()) {
                throw core::RequestValidationError(
                    "Board components need a string 'part'",
                    {{"operation", "create_board"}}, "engines");
            }
            const std::string part = lower(item["part"].get<std::string>());
            const auto preset = partTable().find(part);
            if (preset == partTable().end()) {
                throw core::RequestValidationError(
                    "Unknown part '" + item["part"].get<std::string>() +
                        "': no footprint preset",
                    {{"operation", "create_board"}}, "engines");
            }
            const int n = ++counters[preset->second.refPrefix];
            pcb::Component comp;
            comp.ref = item.value("ref", preset->second.refPrefix + std::to_string(n));
            comp.value = item.value("value", preset->second.defaultValue);
            comp.footprint = preset->second.footprint;
            comp.type = preset->second.type;
            comp.pins = pinsForPreset(comp.footprint, part);
            if (design.findComponent(comp.ref) != nullptr) {
                throw core::RequestValidationError(
                    "Duplicate reference designator '" + comp.ref + "'", {}, "engines");
            }
            design.components.push_back(comp);
            design.footprints[comp.footprint] =
                footprintTable().at(comp.footprint);
        }
    }

    EngineResult out =
        successResult(request, {{"design", design.toJson()},
                                {"board", design.board.toJson()},
                                {"component_count",
                                 static_cast<int>(design.components.size())}});
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "pcb create_board",
        core::Json{{"width_mm", width},
                   {"height_mm", height},
                   {"components", design.components.size()}});
    return out;
}

EngineResult PcbEngine::executeAddComponent(const EngineRequest& request) {
    requireParams(request, {"design", "ref", "footprint"});
    pcb::PcbDesign design = designParam(request.parameters, "add_component");
    if (!request.parameters["ref"].is_string() ||
        request.parameters["ref"].get<std::string>().empty()) {
        throw core::RequestValidationError("Component 'ref' must be a non-empty string",
                                           {}, "engines");
    }
    const std::string ref = request.parameters["ref"].get<std::string>();
    if (design.findComponent(ref) != nullptr) {
        throw core::RequestValidationError("Duplicate reference designator '" + ref +
                                               "'",
                                           {{"ref", ref}}, "engines");
    }
    if (!request.parameters["footprint"].is_string()) {
        throw core::RequestValidationError("Component 'footprint' must be a string",
                                           {}, "engines");
    }
    std::string footprint = request.parameters["footprint"].get<std::string>();
    // Lowercase aliases resolve to preset names ("esp32" -> "ESP32-WROOM-32").
    if (footprintTable().find(footprint) == footprintTable().end()) {
        const std::string alias = lower(footprint);
        bool resolved = false;
        for (const auto& [preset, fp] : footprintTable()) {
            if (lower(preset) == alias) {
                footprint = preset;
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            const auto parts = partTable().find(alias);
            if (parts != partTable().end()) {
                footprint = parts->second.footprint;
                resolved = true;
            }
        }
        if (!resolved) {
            core::Json supported = core::Json::array();
            for (const auto& name : presetNames()) {
                supported.push_back(name);
            }
            throw core::RequestValidationError(
                "Unknown footprint '" + request.parameters["footprint"].get<std::string>() +
                    "'",
                {{"supported", supported}}, "engines");
        }
    }
    pcb::Component comp;
    comp.ref = ref;
    comp.value = request.parameters.value("value", ref);
    comp.footprint = footprint;
    comp.type = pcb::componentTypeFromString(
        request.parameters.value("type", std::string("unknown")));
    if (comp.type == pcb::ComponentType::Unknown) {
        for (const auto& [part, preset] : partTable()) {
            if (preset.footprint == footprint) {
                comp.type = preset.type;
                break;
            }
        }
    }
    if (request.parameters.contains("rotation_deg")) {
        comp.rotationDeg = finiteNumber(request.parameters, "rotation_deg", "add_component");
    }
    comp.pins = pinsForPreset(footprint, comp.value);
    design.components.push_back(comp);
    design.footprints[footprint] = footprintTable().at(footprint);

    EngineResult out =
        successResult(request, {{"design", design.toJson()}, {"ref", ref}});
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "pcb add_component",
        core::Json{{"ref", ref}, {"footprint", footprint}});
    return out;
}

EngineResult PcbEngine::executeAddNet(const EngineRequest& request) {
    requireParams(request, {"design", "name", "pins"});
    pcb::PcbDesign design = designParam(request.parameters, "add_net");
    if (!request.parameters["name"].is_string() ||
        request.parameters["name"].get<std::string>().empty()) {
        throw core::RequestValidationError("Net 'name' must be a non-empty string", {},
                                           "engines");
    }
    const std::string name = request.parameters["name"].get<std::string>();
    if (!request.parameters["pins"].is_array() || request.parameters["pins"].empty()) {
        throw core::RequestValidationError("Net 'pins' must be a non-empty array", {},
                                           "engines");
    }
    std::set<std::string> seen;
    for (const auto& item : request.parameters["pins"]) {
        if (!item.is_string()) {
            throw core::RequestValidationError("Net pins must be 'REF.NUM' strings", {},
                                               "engines");
        }
        const std::string pinRef = item.get<std::string>();
        if (!seen.insert(pinRef).second) {
            throw core::RequestValidationError("Duplicate pin '" + pinRef + "' in net '" +
                                                   name + "'",
                                               {{"net", name}}, "engines");
        }
        std::string ref, num;
        if (!pcb::splitPinRef(pinRef, ref, num)) {
            throw core::RequestValidationError("Malformed pin reference '" + pinRef +
                                                   "': expected 'REF.NUM'",
                                               {{"net", name}}, "engines");
        }
        const pcb::Component* comp = design.findComponent(ref);
        if (comp == nullptr) {
            throw core::RequestValidationError("Net '" + name + "' references unknown " +
                                                   "component '" + ref + "'",
                                               {{"net", name}}, "engines");
        }
        bool pinKnown = false;
        for (const auto& pin : comp->pins) {
            if (pin.number == num) {
                pinKnown = true;
                break;
            }
        }
        if (!pinKnown) {
            throw core::RequestValidationError("Net '" + name + "' references unknown " +
                                                   "pin '" + pinRef + "'",
                                               {{"net", name}}, "engines");
        }
    }
    int nextId = 1;
    for (const auto& net : design.nets) {
        nextId = std::max(nextId, net.id + 1);
    }
    pcb::Net net;
    net.id = nextId;
    net.name = name;
    for (const auto& item : request.parameters["pins"]) {
        net.pins.push_back(item.get<std::string>());
    }
    design.nets.push_back(net);
    for (auto& comp : design.components) {
        for (auto& pin : comp.pins) {
            if (seen.count(comp.ref + "." + pin.number) != 0u) {
                pin.net = name;
            }
        }
    }
    EngineResult out =
        successResult(request, {{"design", design.toJson()}, {"net", net.toJson()}});
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "pcb add_net",
        core::Json{{"net", name}, {"pins", net.pins.size()}});
    return out;
}

EngineResult PcbEngine::executePlaceComponent(const EngineRequest& request) {
    requireParams(request, {"design", "ref", "x_mm", "y_mm"});
    pcb::PcbDesign design = designParam(request.parameters, "place_component");
    if (!request.parameters["ref"].is_string()) {
        throw core::RequestValidationError("Placement 'ref' must be a string", {},
                                           "engines");
    }
    const std::string ref = request.parameters["ref"].get<std::string>();
    const pcb::Component* comp = design.findComponent(ref);
    if (comp == nullptr) {
        throw core::RequestValidationError("Unknown component '" + ref + "'", {},
                                           "engines");
    }
    const double x = finiteNumber(request.parameters, "x_mm", "place_component");
    const double y = finiteNumber(request.parameters, "y_mm", "place_component");
    double rotation = 0.0;
    if (request.parameters.contains("rotation_deg")) {
        rotation = finiteNumber(request.parameters, "rotation_deg", "place_component");
    }
    bool rotationOk = false;
    for (double allowed : {0.0, 90.0, 180.0, 270.0}) {
        if (std::fabs(rotation - allowed) < 1e-9) {
            rotationOk = true;
            break;
        }
    }
    if (!rotationOk) {
        throw core::RequestValidationError("Rotation must be one of 0/90/180/270",
                                           {{"rotation_deg", rotation}}, "engines");
    }
    std::string layer = request.parameters.value("layer", std::string("F.Cu"));
    if (layer != "F.Cu" && layer != "B.Cu") {
        throw core::RequestValidationError("Layer must be 'F.Cu' or 'B.Cu'",
                                           {{"layer", layer}}, "engines");
    }
    // Immediate bounds check (validate_design re-checks everything).
    const auto fp = design.footprints.find(comp->footprint);
    if (fp == design.footprints.end()) {
        throw core::RequestValidationError("Component '" + ref + "' has no known " +
                                               "footprint geometry",
                                           {{"ref", ref}}, "engines");
    }
    double w = fp->second.widthMm;
    double h = fp->second.heightMm;
    if (std::fabs(rotation - 90.0) < 1e-9 || std::fabs(rotation - 270.0) < 1e-9) {
        std::swap(w, h);
    }
    if (x - w / 2.0 < design.board.originMm.x - 1e-9 ||
        y - h / 2.0 < design.board.originMm.y - 1e-9 ||
        x + w / 2.0 > design.board.originMm.x + design.board.widthMm + 1e-9 ||
        y + h / 2.0 > design.board.originMm.y + design.board.heightMm + 1e-9) {
        throw core::RequestValidationError("Placement of '" + ref + "' is outside " +
                                               "the board bounds",
                                           {{"ref", ref}}, "engines");
    }
    bool updated = false;
    for (auto& place : design.placements) {
        if (place.ref == ref) {
            place.xMm = x;
            place.yMm = y;
            place.rotationDeg = rotation;
            place.layer = layer;
            updated = true;
            break;
        }
    }
    if (!updated) {
        pcb::Placement place;
        place.ref = ref;
        place.xMm = x;
        place.yMm = y;
        place.rotationDeg = rotation;
        place.layer = layer;
        design.placements.push_back(place);
    }
    EngineResult out = successResult(
        request, {{"design", design.toJson()},
                  {"placement",
                   core::Json{{"ref", ref},
                              {"x_mm", x},
                              {"y_mm", y},
                              {"rotation_deg", rotation},
                              {"layer", layer}}}});
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "pcb place_component",
        core::Json{{"ref", ref}, {"x_mm", x}, {"y_mm", y}});
    return out;
}

EngineResult PcbEngine::executeValidateDesign(const EngineRequest& request) {
    requireParams(request, {"design"});
    const pcb::PcbDesign design = designParam(request.parameters, "validate_design");
    const validation::ValidationResult rules = pcb::validatePcbDesign(design);
    core::Json ruleList = core::Json::array();
    for (const auto& msg : rules.messages) {
        ruleList.push_back(
            core::Json{{"rule", msg.rule},
                       {"severity", validation::toString(msg.severity)},
                       {"passed", msg.passed},
                       {"message", msg.message},
                       {"details", msg.details}});
    }
    EngineResult out = successResult(request, {{"design", design.toJson()},
                                               {"passed", rules.passed()},
                                               {"rules", ruleList},
                                               {"message", rules.message}});
    out.success = rules.passed();
    if (!rules.passed()) {
        out.errors.clear();
        out.addError(core::makeError(core::ErrorCode::GeometryValidationError,
                                     "PCB design failed validation checks", "engines",
                                     {{"checks", rules.checks}}));
    }
    out.validation = rules;
    out.validation->operation = request.operation;
    core::Logger::instance().info(
        "engines", "pcb validate_design",
        core::Json{{"passed", rules.passed()},
                   {"components", design.components.size()}});
    return out;
}

EngineResult PcbEngine::executeExport(const EngineRequest& request) {
    requireParams(request, {"design"});
    const pcb::PcbDesign design = designParam(request.parameters, "export");
    const std::string project = request.parameters.value("project", std::string(""));
    // Export only validated designs — never write files for bad boards.
    const validation::ValidationResult rules = pcb::validatePcbDesign(design);
    if (!rules.passed()) {
        EngineResult refused = failureResult(
            request, "Refusing to export: design failed validation",
            {{"checks", rules.checks}});
        refused.validation = rules;
        refused.validation->operation = request.operation;
        return refused;
    }
    const std::string projectName = project.empty() ? "trinity_pcb" : project;
    const pcb::KicadExport exported = pcb::exportKicadPcb(design, projectName);
    if (!exported.ok) {
        throw core::EngineExecutionError("KiCad export failed: " + exported.error, {},
                                         "engines");
    }
    std::error_code ec;
    const fs::path workDir = fs::temp_directory_path(ec) / ("trinity_pcb_" + shortId());
    fs::create_directories(workDir, ec);
    if (ec) {
        throw core::EngineExecutionError(
            "Cannot create PCB scratch directory: " + ec.message(), {}, "engines");
    }
    const std::string kicadPath = (workDir / (projectName + ".kicad_pcb")).string();
    {
        std::ofstream board(kicadPath, std::ios::binary | std::ios::trunc);
        if (!board) {
            throw core::EngineExecutionError("Cannot write KiCad artifact", {},
                                             "engines");
        }
        board << exported.content;
        board.close();
        if (!board) {
            throw core::EngineExecutionError("Failed while writing KiCad artifact", {},
                                             "engines");
        }
    }
    const std::string jsonPath = (workDir / (projectName + ".design.json")).string();
    {
        std::ofstream spec(jsonPath, std::ios::binary | std::ios::trunc);
        if (!spec) {
            throw core::EngineExecutionError("Cannot write design JSON artifact", {},
                                             "engines");
        }
        spec << design.toJson().dump(2);
        spec.close();
        if (!spec) {
            throw core::EngineExecutionError("Failed while writing design JSON", {},
                                             "engines");
        }
    }
    EngineResult out = successResult(request, core::Json::object());
    out.pendingArtifacts.emplace_back(kicadPath, "kicad_pcb");
    out.pendingArtifacts.emplace_back(jsonPath, "json");
    out.result = {{"project", projectName},
                  {"design", design.toJson()},
                  {"component_count", static_cast<int>(design.components.size())},
                  {"net_count", static_cast<int>(design.nets.size())}};
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "pcb export",
        core::Json{{"project", projectName},
                   {"components", design.components.size()}});
    return out;
}

validation::ValidationResult PcbEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "pcb"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "PCB operation failed";
        validation::ValidationMessage msg;
        msg.rule = "pcb.success";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Engine reported failure";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    if (result.operation == "validate_design" && result.result.contains("rules")) {
        const bool passed = result.result.value("passed", false);
        validation.status = passed ? validation::ValidationStatus::Validated
                                   : validation::ValidationStatus::Invalid;
        validation.message = result.result.value("message", "");
        validation::ValidationMessage msg;
        msg.rule = "pcb.design_rules";
        msg.severity = passed ? validation::Severity::Info : validation::Severity::Error;
        msg.passed = passed;
        msg.message = validation.message;
        validation.addMessage(std::move(msg));
        return validation;
    }
    if (!result.result.contains("design") || !result.result["design"].is_object()) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "PCB result carries no design object";
        return validation;
    }
    validation.status = validation::ValidationStatus::Validated;
    validation.message = "PCB operation produced a design object";
    validation::ValidationMessage msg;
    msg.rule = "pcb.design_present";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = validation.message;
    validation.addMessage(std::move(msg));
    return validation;
}

}  // namespace trinity::engines

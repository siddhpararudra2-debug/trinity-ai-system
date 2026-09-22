#include "trinity/pcb/PcbDesign.hpp"

namespace trinity::pcb {

core::Json Vec2::toJson() const {
    return core::Json{{"x", x}, {"y", y}};
}

Vec2 Vec2::fromJson(const core::Json& json) {
    Vec2 out;
    out.x = json.value("x", 0.0);
    out.y = json.value("y", 0.0);
    return out;
}

std::string toString(PinElectricalType type) {
    switch (type) {
        case PinElectricalType::PowerIn:
            return "power_in";
        case PinElectricalType::PowerOut:
            return "power_out";
        case PinElectricalType::Passive:
            return "passive";
        case PinElectricalType::Input:
            return "input";
        case PinElectricalType::Output:
            return "output";
        case PinElectricalType::Bidirectional:
            return "bidirectional";
        case PinElectricalType::Unknown:
        default:
            return "unknown";
    }
}

PinElectricalType pinTypeFromString(const std::string& type) {
    if (type == "power_in") return PinElectricalType::PowerIn;
    if (type == "power_out") return PinElectricalType::PowerOut;
    if (type == "passive") return PinElectricalType::Passive;
    if (type == "input") return PinElectricalType::Input;
    if (type == "output") return PinElectricalType::Output;
    if (type == "bidirectional") return PinElectricalType::Bidirectional;
    return PinElectricalType::Unknown;
}

std::string toString(ComponentType type) {
    switch (type) {
        case ComponentType::Mcu:
            return "mcu";
        case ComponentType::Imu:
            return "imu";
        case ComponentType::Regulator:
            return "regulator";
        case ComponentType::Passive:
            return "passive";
        case ComponentType::Connector:
            return "connector";
        case ComponentType::Other:
            return "other";
        case ComponentType::Unknown:
        default:
            return "unknown";
    }
}

ComponentType componentTypeFromString(const std::string& type) {
    if (type == "mcu") return ComponentType::Mcu;
    if (type == "imu") return ComponentType::Imu;
    if (type == "regulator") return ComponentType::Regulator;
    if (type == "passive") return ComponentType::Passive;
    if (type == "connector") return ComponentType::Connector;
    if (type == "other") return ComponentType::Other;
    return ComponentType::Unknown;
}

core::Json Pin::toJson() const {
    return core::Json{{"number", number},
                      {"name", name},
                      {"electrical", toString(electrical)},
                      {"pos_mm", posMm.toJson()},
                      {"net", net}};
}

Pin Pin::fromJson(const core::Json& json) {
    Pin out;
    out.number = json.value("number", "");
    out.name = json.value("name", "");
    out.electrical = pinTypeFromString(json.value("electrical", "unknown"));
    if (json.contains("pos_mm")) {
        out.posMm = Vec2::fromJson(json["pos_mm"]);
    }
    out.net = json.value("net", "");
    return out;
}

core::Json Component::toJson() const {
    core::Json pinsJson = core::Json::array();
    for (const auto& pin : pins) {
        pinsJson.push_back(pin.toJson());
    }
    return core::Json{{"ref", ref},
                      {"value", value},
                      {"footprint", footprint},
                      {"type", toString(type)},
                      {"rotation_deg", rotationDeg},
                      {"layer", layer},
                      {"pins", pinsJson}};
}

Component Component::fromJson(const core::Json& json) {
    Component out;
    out.ref = json.value("ref", "");
    out.value = json.value("value", "");
    out.footprint = json.value("footprint", "");
    out.type = componentTypeFromString(json.value("type", "unknown"));
    out.rotationDeg = json.value("rotation_deg", 0.0);
    out.layer = json.value("layer", "F.Cu");
    if (json.contains("pins") && json["pins"].is_array()) {
        for (const auto& item : json["pins"]) {
            out.pins.push_back(Pin::fromJson(item));
        }
    }
    return out;
}

core::Json Net::toJson() const {
    core::Json pinsJson = core::Json::array();
    for (const auto& pin : pins) {
        pinsJson.push_back(pin);
    }
    return core::Json{{"id", id}, {"name", name}, {"pins", pinsJson}};
}

Net Net::fromJson(const core::Json& json) {
    Net out;
    out.id = json.value("id", 0);
    out.name = json.value("name", "");
    if (json.contains("pins") && json["pins"].is_array()) {
        for (const auto& item : json["pins"]) {
            if (item.is_string()) {
                out.pins.push_back(item.get<std::string>());
            }
        }
    }
    return out;
}

core::Json FootprintPad::toJson() const {
    return core::Json{{"number", number},
                      {"pos_mm", posMm.toJson()},
                      {"size_mm", sizeMm.toJson()},
                      {"shape", shape},
                      {"kind", kind}};
}

FootprintPad FootprintPad::fromJson(const core::Json& json) {
    FootprintPad out;
    out.number = json.value("number", "");
    if (json.contains("pos_mm")) {
        out.posMm = Vec2::fromJson(json["pos_mm"]);
    }
    if (json.contains("size_mm")) {
        out.sizeMm = Vec2::fromJson(json["size_mm"]);
    }
    out.shape = json.value("shape", "rect");
    out.kind = json.value("kind", "smd");
    return out;
}

core::Json Footprint::toJson() const {
    core::Json padsJson = core::Json::array();
    for (const auto& pad : pads) {
        padsJson.push_back(pad.toJson());
    }
    return core::Json{{"name", name},
                      {"width_mm", widthMm},
                      {"height_mm", heightMm},
                      {"pads", padsJson}};
}

Footprint Footprint::fromJson(const core::Json& json) {
    Footprint out;
    out.name = json.value("name", "");
    out.widthMm = json.value("width_mm", 0.0);
    out.heightMm = json.value("height_mm", 0.0);
    if (json.contains("pads") && json["pads"].is_array()) {
        for (const auto& item : json["pads"]) {
            out.pads.push_back(FootprintPad::fromJson(item));
        }
    }
    return out;
}

core::Json Placement::toJson() const {
    return core::Json{{"ref", ref},
                      {"x_mm", xMm},
                      {"y_mm", yMm},
                      {"rotation_deg", rotationDeg},
                      {"layer", layer}};
}

Placement Placement::fromJson(const core::Json& json) {
    Placement out;
    out.ref = json.value("ref", "");
    out.xMm = json.value("x_mm", 0.0);
    out.yMm = json.value("y_mm", 0.0);
    out.rotationDeg = json.value("rotation_deg", 0.0);
    out.layer = json.value("layer", "F.Cu");
    return out;
}

core::Json BoardOutline::toJson() const {
    core::Json layersJson = core::Json::array();
    for (const auto& layer : layers) {
        layersJson.push_back(layer);
    }
    return core::Json{{"width_mm", widthMm},
                      {"height_mm", heightMm},
                      {"thickness_mm", thicknessMm},
                      {"origin_mm", originMm.toJson()},
                      {"layers", layersJson}};
}

BoardOutline BoardOutline::fromJson(const core::Json& json) {
    BoardOutline out;
    out.widthMm = json.value("width_mm", 0.0);
    out.heightMm = json.value("height_mm", 0.0);
    out.thicknessMm = json.value("thickness_mm", 1.6);
    if (json.contains("origin_mm")) {
        out.originMm = Vec2::fromJson(json["origin_mm"]);
    }
    out.layers = {"F.Cu", "B.Cu"};
    if (json.contains("layers") && json["layers"].is_array()) {
        out.layers.clear();
        for (const auto& item : json["layers"]) {
            if (item.is_string()) {
                out.layers.push_back(item.get<std::string>());
            }
        }
    }
    return out;
}

core::Json DesignRule::toJson() const {
    return core::Json{{"name", name},
                      {"min_clearance_mm", minClearanceMm},
                      {"min_track_mm", minTrackMm}};
}

DesignRule DesignRule::fromJson(const core::Json& json) {
    DesignRule out;
    out.name = json.value("name", "default");
    out.minClearanceMm = json.value("min_clearance_mm", 0.2);
    out.minTrackMm = json.value("min_track_mm", 0.2);
    return out;
}

core::Json PcbDesign::toJson() const {
    core::Json comps = core::Json::array();
    for (const auto& comp : components) {
        comps.push_back(comp.toJson());
    }
    core::Json netJson = core::Json::array();
    for (const auto& net : nets) {
        netJson.push_back(net.toJson());
    }
    core::Json fpJson = core::Json::object();
    for (const auto& [name, fp] : footprints) {
        fpJson[name] = fp.toJson();
    }
    core::Json placeJson = core::Json::array();
    for (const auto& place : placements) {
        placeJson.push_back(place.toJson());
    }
    core::Json rulesJson = core::Json::array();
    for (const auto& rule : rules) {
        rulesJson.push_back(rule.toJson());
    }
    return core::Json{{"board", board.toJson()},
                      {"components", comps},
                      {"nets", netJson},
                      {"footprints", fpJson},
                      {"placements", placeJson},
                      {"rules", rulesJson},
                      {"metadata", metadata}};
}

PcbDesign PcbDesign::fromJson(const core::Json& json) {
    PcbDesign out;
    if (json.contains("board")) {
        out.board = BoardOutline::fromJson(json["board"]);
    }
    if (json.contains("components") && json["components"].is_array()) {
        for (const auto& item : json["components"]) {
            out.components.push_back(Component::fromJson(item));
        }
    }
    if (json.contains("nets") && json["nets"].is_array()) {
        for (const auto& item : json["nets"]) {
            out.nets.push_back(Net::fromJson(item));
        }
    }
    if (json.contains("footprints") && json["footprints"].is_object()) {
        for (auto it = json["footprints"].begin(); it != json["footprints"].end(); ++it) {
            out.footprints[it.key()] = Footprint::fromJson(it.value());
        }
    }
    if (json.contains("placements") && json["placements"].is_array()) {
        for (const auto& item : json["placements"]) {
            out.placements.push_back(Placement::fromJson(item));
        }
    }
    if (json.contains("rules") && json["rules"].is_array()) {
        out.rules.clear();
        for (const auto& item : json["rules"]) {
            out.rules.push_back(DesignRule::fromJson(item));
        }
    }
    if (out.rules.empty()) {
        out.rules = {DesignRule{"default"}};
    }
    out.metadata = json.value("metadata", core::Json::object());
    return out;
}

const Component* PcbDesign::findComponent(const std::string& ref) const {
    for (const auto& comp : components) {
        if (comp.ref == ref) {
            return &comp;
        }
    }
    return nullptr;
}

const Placement* PcbDesign::findPlacement(const std::string& ref) const {
    for (const auto& place : placements) {
        if (place.ref == ref) {
            return &place;
        }
    }
    return nullptr;
}

const Net* PcbDesign::findNet(const std::string& name) const {
    for (const auto& net : nets) {
        if (net.name == name) {
            return &net;
        }
    }
    return nullptr;
}

bool splitPinRef(const std::string& pinRef, std::string& refOut, std::string& numOut) {
    const auto dot = pinRef.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= pinRef.size()) {
        return false;
    }
    refOut = pinRef.substr(0, dot);
    numOut = pinRef.substr(dot + 1);
    return true;
}

}  // namespace trinity::pcb

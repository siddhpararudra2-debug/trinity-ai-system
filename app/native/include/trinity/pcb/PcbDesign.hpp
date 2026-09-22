#pragma once

// PCB intermediate representation: tool-agnostic board data (millimetres).
// Deliberately independent from KiCad file syntax — the exporter in
// pcb/KiCadExport is the only place that knows s-expression layout.
// Every struct round-trips through JSON so designs flow between jobs
// and workflow nodes as structured data, never display strings.

#include <map>
#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::pcb {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    core::Json toJson() const;
    static Vec2 fromJson(const core::Json& json);
};

enum class PinElectricalType {
    Unknown,
    PowerIn,
    PowerOut,
    Passive,
    Input,
    Output,
    Bidirectional,
};

std::string toString(PinElectricalType type);
PinElectricalType pinTypeFromString(const std::string& type);

enum class ComponentType {
    Unknown,
    Mcu,
    Imu,
    Regulator,
    Passive,
    Connector,
    Other,
};

std::string toString(ComponentType type);
ComponentType componentTypeFromString(const std::string& type);

struct Pin {
    std::string number;  // e.g. "12"
    std::string name;    // e.g. "GND"
    PinElectricalType electrical = PinElectricalType::Unknown;
    Vec2 posMm;  // relative to the component origin
    std::string net;  // assigned net name, empty when unconnected

    core::Json toJson() const;
    static Pin fromJson(const core::Json& json);
};

struct Component {
    std::string ref;  // reference designator, e.g. "U1"
    std::string value;  // e.g. "ESP32-WROOM-32"
    std::string footprint;  // preset name, e.g. "ESP32-WROOM-32"
    ComponentType type = ComponentType::Unknown;
    double rotationDeg = 0.0;
    std::string layer = "F.Cu";
    std::vector<Pin> pins;

    core::Json toJson() const;
    static Component fromJson(const core::Json& json);
};

struct Net {
    int id = 0;  // 0 is reserved for the unconnected placeholder
    std::string name;
    std::vector<std::string> pins;  // "REF.NUM" entries

    core::Json toJson() const;
    static Net fromJson(const core::Json& json);
};

struct FootprintPad {
    std::string number;
    Vec2 posMm;  // relative to the footprint origin
    Vec2 sizeMm;
    std::string shape = "rect";  // "rect" | "circle" | "oval"
    std::string kind = "smd";  // "smd" | "thru_hole"

    core::Json toJson() const;
    static FootprintPad fromJson(const core::Json& json);
};

struct Footprint {
    std::string name;
    double widthMm = 0.0;  // courtyard/body extent for bounds checks
    double heightMm = 0.0;
    std::vector<FootprintPad> pads;

    core::Json toJson() const;
    static Footprint fromJson(const core::Json& json);
};

struct Placement {
    std::string ref;
    double xMm = 0.0;
    double yMm = 0.0;
    double rotationDeg = 0.0;
    std::string layer = "F.Cu";

    core::Json toJson() const;
    static Placement fromJson(const core::Json& json);
};

struct BoardOutline {
    double widthMm = 0.0;
    double heightMm = 0.0;
    double thicknessMm = 1.6;
    Vec2 originMm{0.0, 0.0};
    std::vector<std::string> layers = {"F.Cu", "B.Cu"};

    core::Json toJson() const;
    static BoardOutline fromJson(const core::Json& json);
};

struct DesignRule {
    std::string name;
    double minClearanceMm = 0.2;
    double minTrackMm = 0.2;

    core::Json toJson() const;
    static DesignRule fromJson(const core::Json& json);
};

struct PcbDesign {
    BoardOutline board;
    std::vector<Component> components;
    std::vector<Net> nets;
    std::map<std::string, Footprint> footprints;  // preset copies used
    std::vector<Placement> placements;
    std::vector<DesignRule> rules = {DesignRule{"default"}};
    core::Json metadata = core::Json::object();

    core::Json toJson() const;
    static PcbDesign fromJson(const core::Json& json);

    const Component* findComponent(const std::string& ref) const;
    const Placement* findPlacement(const std::string& ref) const;
    const Net* findNet(const std::string& name) const;
};

/// "REF.NUM" pin reference split; false when malformed.
bool splitPinRef(const std::string& pinRef, std::string& refOut, std::string& numOut);

}  // namespace trinity::pcb

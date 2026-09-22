#include "trinity/firmware/FirmwareProject.hpp"

namespace trinity::firmware {

std::string toString(PinDirection direction) {
    switch (direction) {
        case PinDirection::Input:
            return "in";
        case PinDirection::Output:
            return "out";
        case PinDirection::Bidirectional:
            return "inout";
        case PinDirection::Unknown:
        default:
            return "unknown";
    }
}

PinDirection pinDirectionFromString(const std::string& direction) {
    if (direction == "in" || direction == "input") return PinDirection::Input;
    if (direction == "out" || direction == "output") return PinDirection::Output;
    if (direction == "inout" || direction == "bidirectional")
        return PinDirection::Bidirectional;
    return PinDirection::Unknown;
}

std::string toString(RequirementKind kind) {
    switch (kind) {
        case RequirementKind::GpioInput:
            return "read_gpio";
        case RequirementKind::GpioOutput:
            return "write_gpio";
        case RequirementKind::Pwm:
            return "write_pwm";
        case RequirementKind::Uart:
            return "serial_communication";
        case RequirementKind::I2c:
            return "i2c_communication";
        case RequirementKind::Spi:
            return "spi_communication";
        case RequirementKind::Unknown:
        default:
            return "unknown";
    }
}

RequirementKind requirementKindFromString(const std::string& kind) {
    if (kind == "read_gpio") return RequirementKind::GpioInput;
    if (kind == "write_gpio") return RequirementKind::GpioOutput;
    if (kind == "write_pwm") return RequirementKind::Pwm;
    if (kind == "serial_communication") return RequirementKind::Uart;
    if (kind == "i2c_communication") return RequirementKind::I2c;
    if (kind == "spi_communication") return RequirementKind::Spi;
    return RequirementKind::Unknown;
}

core::Json McuPin::toJson() const {
    return core::Json{{"name", name},
                      {"digital", digital},
                      {"adc", adc},
                      {"pwm", pwm}};
}

McuPin McuPin::fromJson(const core::Json& json) {
    McuPin out;
    out.name = json.value("name", "");
    out.digital = json.value("digital", true);
    out.adc = json.value("adc", false);
    out.pwm = json.value("pwm", false);
    return out;
}

core::Json PeripheralInfo::toJson() const {
    return core::Json{{"name", name}, {"kind", kind}, {"instances", instances}};
}

PeripheralInfo PeripheralInfo::fromJson(const core::Json& json) {
    PeripheralInfo out;
    out.name = json.value("name", "");
    out.kind = json.value("kind", "");
    out.instances = json.value("instances", 1);
    return out;
}

core::Json MCU::toJson() const {
    core::Json pinsJson = core::Json::array();
    for (const auto& pin : pins) {
        pinsJson.push_back(pin.toJson());
    }
    core::Json periJson = core::Json::array();
    for (const auto& peri : peripherals) {
        periJson.push_back(peri.toJson());
    }
    return core::Json{{"family", family},
                      {"manufacturer", manufacturer},
                      {"model", model},
                      {"architecture", architecture},
                      {"clock_hz_max", clockHzMax},
                      {"pins", pinsJson},
                      {"peripherals", periJson},
                      {"toolchain", toolchain}};
}

MCU MCU::fromJson(const core::Json& json) {
    MCU out;
    out.family = json.value("family", "");
    out.manufacturer = json.value("manufacturer", "");
    out.model = json.value("model", "");
    out.architecture = json.value("architecture", "");
    out.clockHzMax = json.value("clock_hz_max", 0LL);
    if (json.contains("pins") && json["pins"].is_array()) {
        for (const auto& item : json["pins"]) {
            out.pins.push_back(McuPin::fromJson(item));
        }
    }
    if (json.contains("peripherals") && json["peripherals"].is_array()) {
        for (const auto& item : json["peripherals"]) {
            out.peripherals.push_back(PeripheralInfo::fromJson(item));
        }
    }
    out.toolchain = json.value("toolchain", "");
    return out;
}

const McuPin* MCU::findPin(const std::string& name) const {
    for (const auto& pin : pins) {
        if (pin.name == name) {
            return &pin;
        }
    }
    return nullptr;
}

bool MCU::hasPeripheralKind(const std::string& kind) const {
    for (const auto& peri : peripherals) {
        if (peri.kind == kind) {
            return true;
        }
    }
    return false;
}

core::Json PinMapping::toJson() const {
    return core::Json{{"mcu_pin", mcuPin},
                      {"function", function},
                      {"peripheral", peripheral},
                      {"direction", toString(direction)},
                      {"electrical_mode", electricalMode},
                      {"pull", pull},
                      {"alt_function", altFunction}};
}

PinMapping PinMapping::fromJson(const core::Json& json) {
    PinMapping out;
    out.mcuPin = json.value("mcu_pin", "");
    out.function = json.value("function", "");
    out.peripheral = json.value("peripheral", "");
    out.direction = pinDirectionFromString(json.value("direction", "unknown"));
    out.electricalMode = json.value("electrical_mode", "");
    out.pull = json.value("pull", "");
    out.altFunction = json.value("alt_function", "");
    return out;
}

core::Json FirmwareRequirement::toJson() const {
    return core::Json{{"kind", toString(kind)}, {"params", params}};
}

FirmwareRequirement FirmwareRequirement::fromJson(const core::Json& json) {
    FirmwareRequirement out;
    out.kind = requirementKindFromString(json.value("kind", "unknown"));
    out.params = json.value("params", core::Json::object());
    return out;
}

core::Json SourceFile::toJson() const {
    return core::Json{{"path", path}, {"content", content}};
}

SourceFile SourceFile::fromJson(const core::Json& json) {
    SourceFile out;
    out.path = json.value("path", "");
    out.content = json.value("content", "");
    return out;
}

core::Json BuildConfiguration::toJson() const {
    return core::Json{{"profile", profile},
                      {"arch", arch},
                      {"toolchain", toolchain},
                      {"opt_level", optLevel},
                      {"output_dir", outputDir}};
}

BuildConfiguration BuildConfiguration::fromJson(const core::Json& json) {
    BuildConfiguration out;
    out.profile = json.value("profile", "debug");
    out.arch = json.value("arch", "");
    out.toolchain = json.value("toolchain", "");
    out.optLevel = json.value("opt_level", "-O2");
    out.outputDir = json.value("output_dir", "build");
    return out;
}

core::Json FirmwareProject::toJson() const {
    core::Json maps = core::Json::array();
    for (const auto& mapping : pinMappings) {
        maps.push_back(mapping.toJson());
    }
    core::Json reqs = core::Json::array();
    for (const auto& req : requirements) {
        reqs.push_back(req.toJson());
    }
    core::Json srcs = core::Json::array();
    for (const auto& src : sources) {
        srcs.push_back(src.toJson());
    }
    return core::Json{{"name", name},
                      {"mcu", mcu.toJson()},
                      {"has_mcu", hasMcu},
                      {"clock_hz", clockHz},
                      {"pin_mappings", maps},
                      {"requirements", reqs},
                      {"sources", srcs},
                      {"build", build.toJson()},
                      {"metadata", metadata}};
}

FirmwareProject FirmwareProject::fromJson(const core::Json& json) {
    FirmwareProject out;
    out.name = json.value("name", "");
    if (json.contains("mcu")) {
        out.mcu = MCU::fromJson(json["mcu"]);
    }
    out.hasMcu = json.value("has_mcu", false);
    out.clockHz = json.value("clock_hz", 0LL);
    if (json.contains("pin_mappings") && json["pin_mappings"].is_array()) {
        for (const auto& item : json["pin_mappings"]) {
            out.pinMappings.push_back(PinMapping::fromJson(item));
        }
    }
    if (json.contains("requirements") && json["requirements"].is_array()) {
        for (const auto& item : json["requirements"]) {
            out.requirements.push_back(FirmwareRequirement::fromJson(item));
        }
    }
    if (json.contains("sources") && json["sources"].is_array()) {
        for (const auto& item : json["sources"]) {
            out.sources.push_back(SourceFile::fromJson(item));
        }
    }
    if (json.contains("build")) {
        out.build = BuildConfiguration::fromJson(json["build"]);
    }
    out.metadata = json.value("metadata", core::Json::object());
    return out;
}

const PinMapping* FirmwareProject::findMapping(const std::string& mcuPin) const {
    for (const auto& mapping : pinMappings) {
        if (mapping.mcuPin == mcuPin) {
            return &mapping;
        }
    }
    return nullptr;
}

}  // namespace trinity::firmware

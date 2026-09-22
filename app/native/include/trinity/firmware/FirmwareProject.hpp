#pragma once

// Firmware intermediate representation: tool-agnostic project data.
// Independent from PlatformIO, Arduino IDE, or any vendor SDK — the
// code generator (firmware/CodeGen) is the only place that knows the
// emitted source layout. Projects round-trip through JSON so they flow
// between jobs and workflow nodes as structured data.

#include <map>
#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::firmware {

enum class PinDirection {
    Unknown,
    Input,
    Output,
    Bidirectional,
};

std::string toString(PinDirection direction);
PinDirection pinDirectionFromString(const std::string& direction);

enum class RequirementKind {
    Unknown,
    GpioInput,   // read_gpio
    GpioOutput,  // write_gpio
    Pwm,         // write_pwm
    Uart,        // serial_communication
    I2c,         // i2c_communication
    Spi,         // spi_communication
};

std::string toString(RequirementKind kind);
RequirementKind requirementKindFromString(const std::string& kind);

struct McuPin {
    std::string name;  // e.g. "GPIO2", "PA5"
    bool digital = true;
    bool adc = false;
    bool pwm = false;

    core::Json toJson() const;
    static McuPin fromJson(const core::Json& json);
};

struct PeripheralInfo {
    std::string name;  // e.g. "UART0"
    std::string kind;  // "gpio" | "uart" | "i2c" | "spi" | "pwm"
    int instances = 1;

    core::Json toJson() const;
    static PeripheralInfo fromJson(const core::Json& json);
};

struct MCU {
    std::string family;  // e.g. "ESP32"
    std::string manufacturer;  // e.g. "Espressif"
    std::string model;  // e.g. "ESP32"
    std::string architecture;  // e.g. "xtensa-lx6"
    long long clockHzMax = 0;
    std::vector<McuPin> pins;
    std::vector<PeripheralInfo> peripherals;
    std::string toolchain;  // e.g. "xtensa-esp32-elf-g++"

    core::Json toJson() const;
    static MCU fromJson(const core::Json& json);

    const McuPin* findPin(const std::string& name) const;
    bool hasPeripheralKind(const std::string& kind) const;
};

struct PinMapping {
    std::string mcuPin;  // e.g. "GPIO2"
    std::string function;  // e.g. "gpio_out", "uart_tx"
    std::string peripheral;  // e.g. "UART0", empty for plain GPIO
    PinDirection direction = PinDirection::Unknown;
    std::string electricalMode;  // e.g. "digital", "analog"
    std::string pull;  // "" | "up" | "down"
    std::string altFunction;  // optional alternate-function label

    core::Json toJson() const;
    static PinMapping fromJson(const core::Json& json);
};

struct FirmwareRequirement {
    RequirementKind kind = RequirementKind::Unknown;
    core::Json params = core::Json::object();  // e.g. {pin, baud, ...}

    core::Json toJson() const;
    static FirmwareRequirement fromJson(const core::Json& json);
};

struct SourceFile {
    std::string path;  // e.g. "main.cpp", "config.h"
    std::string content;

    core::Json toJson() const;
    static SourceFile fromJson(const core::Json& json);
};

struct BuildConfiguration {
    std::string profile = "debug";  // "debug" | "release"
    std::string arch;  // e.g. "xtensa-lx6"
    std::string toolchain;  // resolved compiler executable
    std::string optLevel = "-O2";
    std::string outputDir = "build";

    core::Json toJson() const;
    static BuildConfiguration fromJson(const core::Json& json);
};

struct FirmwareProject {
    std::string name;
    MCU mcu;  // empty model until select_mcu
    bool hasMcu = false;
    long long clockHz = 0;
    std::vector<PinMapping> pinMappings;
    std::vector<FirmwareRequirement> requirements;
    std::vector<SourceFile> sources;
    BuildConfiguration build;
    core::Json metadata = core::Json::object();

    core::Json toJson() const;
    static FirmwareProject fromJson(const core::Json& json);

    const PinMapping* findMapping(const std::string& mcuPin) const;
};

}  // namespace trinity::firmware

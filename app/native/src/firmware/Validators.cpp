#include "trinity/firmware/Validators.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace trinity::firmware {

namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// "UART0" -> "uart", "I2C1" -> "i2c", "PWM" -> "pwm".
std::string peripheralKindPrefix(const std::string& peripheral) {
    const size_t digit = peripheral.find_first_of("0123456789");
    return lowerAscii(peripheral.substr(0, digit));
}

void addRule(validation::ValidationResult& out, const std::string& rule,
             validation::Severity severity, bool passed, const std::string& message,
             const core::Json& details = core::Json::object()) {
    validation::ValidationMessage msg;
    msg.rule = rule;
    msg.severity = severity;
    msg.passed = passed;
    msg.message = message;
    msg.details = details;
    out.addMessage(std::move(msg));
}

}  // namespace

validation::ValidationResult validateFirmwareProject(const FirmwareProject& project) {
    validation::ValidationResult out;
    out.operation = "validate_project";
    out.checks = core::Json::object();
    bool ok = true;

    if (!project.name.empty()) {
        addRule(out, "project.name", validation::Severity::Info, true,
                "Project has a name", {{"name", project.name}});
    } else {
        ok = false;
        addRule(out, "project.name", validation::Severity::Error, false,
                "Project name is empty");
    }

    if (project.hasMcu && !project.mcu.model.empty()) {
        addRule(out, "project.mcu_selected", validation::Severity::Info, true,
                "MCU is selected", {{"model", project.mcu.model}});
        if (project.clockHz > 0 && project.clockHz <= project.mcu.clockHzMax) {
            addRule(out, "mcu.clock_hz", validation::Severity::Info, true,
                    "Clock frequency is within MCU maximum",
                    {{"clock_hz", project.clockHz},
                     {"max_hz", project.mcu.clockHzMax}});
        } else {
            ok = false;
            addRule(out, "mcu.clock_hz", validation::Severity::Error, false,
                    "Clock frequency is 0 or exceeds MCU maximum",
                    {{"clock_hz", project.clockHz},
                     {"max_hz", project.mcu.clockHzMax}});
        }
    } else {
        ok = false;
        addRule(out, "project.mcu_selected", validation::Severity::Error, false,
                "No MCU selected — run select_mcu first");
    }

    {
        std::set<std::string> pins;
        for (const auto& mapping : project.pinMappings) {
            if (mapping.mcuPin.empty()) {
                ok = false;
                addRule(out, "pin.name", validation::Severity::Error, false,
                        "Pin mapping with empty MCU pin name");
                continue;
            }
            if (project.hasMcu && project.mcu.findPin(mapping.mcuPin) == nullptr) {
                ok = false;
                addRule(out, "pin.known", validation::Severity::Error, false,
                        "Pin '" + mapping.mcuPin + "' does not exist on " +
                            project.mcu.model,
                        {{"pin", mapping.mcuPin}});
            }
            if (!pins.insert(mapping.mcuPin).second) {
                ok = false;
                addRule(out, "pin.unique", validation::Severity::Error, false,
                        "Pin '" + mapping.mcuPin + "' is configured more than once",
                        {{"pin", mapping.mcuPin}});
            }
            if (mapping.direction == PinDirection::Unknown) {
                ok = false;
                addRule(out, "pin.direction", validation::Severity::Error, false,
                        "Pin '" + mapping.mcuPin + "' has unknown direction",
                        {{"pin", mapping.mcuPin}});
            }
            if (project.hasMcu && project.mcu.findPin(mapping.mcuPin) != nullptr) {
                const McuPin* pinInfo = project.mcu.findPin(mapping.mcuPin);
                if (pinInfo != nullptr && mapping.function == "pwm" && !pinInfo->pwm) {
                    ok = false;
                    addRule(out, "pin.pwm_supported", validation::Severity::Error, false,
                            "Pin '" + mapping.mcuPin + "' does not support PWM on " +
                                project.mcu.model,
                            {{"pin", mapping.mcuPin}});
                }
                if (pinInfo != nullptr && mapping.electricalMode == "analog" &&
                    !pinInfo->adc) {
                    ok = false;
                    addRule(out, "pin.adc_supported", validation::Severity::Error, false,
                            "Pin '" + mapping.mcuPin + "' does not support ADC on " +
                                project.mcu.model,
                            {{"pin", mapping.mcuPin}});
                }
            }
            if (!mapping.pull.empty() && mapping.pull != "up" &&
                mapping.pull != "down") {
                ok = false;
                addRule(out, "pin.pull", validation::Severity::Error, false,
                        "Pin '" + mapping.mcuPin + "' has invalid pull '" +
                            mapping.pull + "'",
                        {{"pin", mapping.mcuPin}, {"pull", mapping.pull}});
            }
            if (!mapping.peripheral.empty() && project.hasMcu) {
                const std::string prefix = peripheralKindPrefix(mapping.peripheral);
                bool found = false;
                for (const auto& peri : project.mcu.peripherals) {
                    if (peri.name == mapping.peripheral || peri.kind == prefix) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ok = false;
                    addRule(out, "pin.peripheral_known", validation::Severity::Error,
                            false,
                            "Peripheral '" + mapping.peripheral +
                                "' is not available on " + project.mcu.model,
                            {{"pin", mapping.mcuPin},
                             {"peripheral", mapping.peripheral}});
                }
            }
        }
        if (!project.pinMappings.empty() && ok) {
            addRule(out, "pin.unique", validation::Severity::Info, true,
                    "All configured pins are unique and known");
        }
    }

    {
        for (const auto& req : project.requirements) {
            if (req.kind == RequirementKind::Unknown) {
                ok = false;
                addRule(out, "requirement.kind", validation::Severity::Error, false,
                        "Requirement with unknown kind");
                continue;
            }
            if (req.kind == RequirementKind::Uart && project.hasMcu &&
                !project.mcu.hasPeripheralKind("uart")) {
                ok = false;
                addRule(out, "requirement.uart_supported", validation::Severity::Error,
                        false, "UART is not available on " + project.mcu.model);
            }
            if (req.kind == RequirementKind::I2c && project.hasMcu &&
                !project.mcu.hasPeripheralKind("i2c")) {
                ok = false;
                addRule(out, "requirement.i2c_supported", validation::Severity::Error,
                        false, "I2C is not available on " + project.mcu.model);
            }
            if (req.kind == RequirementKind::Spi) {
                // SPI is intentionally out of scope for V1: refuse truthfully.
                ok = false;
                addRule(out, "requirement.spi_supported", validation::Severity::Error,
                        false, "SPI is not supported by FirmwareEngine V1");
            }
            if (req.kind == RequirementKind::Pwm && project.hasMcu &&
                !project.mcu.hasPeripheralKind("pwm")) {
                ok = false;
                addRule(out, "requirement.pwm_supported", validation::Severity::Error,
                        false, "PWM is not available on " + project.mcu.model);
            }
        }
        if (ok) {
            addRule(out, "requirement.supported", validation::Severity::Info, true,
                    "All requirements map to MCU capabilities");
        }
    }

    if (!project.sources.empty()) {
        bool hasMain = false;
        bool hasConfig = false;
        for (const auto& src : project.sources) {
            if (src.path == "main.cpp") hasMain = true;
            if (src.path == "config.h") hasConfig = true;
        }
        if (hasMain && hasConfig) {
            addRule(out, "source.generated", validation::Severity::Info, true,
                    "Generated sources include main.cpp and config.h",
                    {{"file_count", static_cast<int>(project.sources.size())}});
        } else {
            ok = false;
            addRule(out, "source.generated", validation::Severity::Error, false,
                    "Generated sources are incomplete",
                    {{"has_main", hasMain}, {"has_config", hasConfig}});
        }
    } else {
        addRule(out, "source.generated", validation::Severity::Warning, true,
                "No generated sources yet — run generate_firmware");
    }

    out.status = ok ? validation::ValidationStatus::Validated
                    : validation::ValidationStatus::Invalid;
    out.message =
        ok ? "Firmware project passed all checks" : "Firmware project failed checks";
    out.checks = {{"pin_count", static_cast<int>(project.pinMappings.size())},
                  {"requirement_count", static_cast<int>(project.requirements.size())},
                  {"source_count", static_cast<int>(project.sources.size())},
                  {"mcu", project.mcu.model}};
    return out;
}

}  // namespace trinity::firmware

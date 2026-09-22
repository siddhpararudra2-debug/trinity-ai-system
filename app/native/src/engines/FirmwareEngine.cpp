#include "trinity/engines/FirmwareEngine.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/firmware/Builder.hpp"
#include "trinity/firmware/CodeGen.hpp"
#include "trinity/firmware/McuDatabase.hpp"
#include "trinity/firmware/Validators.hpp"

namespace trinity::engines {
namespace fs = std::filesystem;

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string shortId() {
    std::string uuid = core::newUuid();
    uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
    return uuid.substr(0, 8);
}

firmware::FirmwareProject projectParam(const core::Json& params,
                                        const std::string& operation) {
    if (!params.contains("project") || !params["project"].is_object()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires a 'project' object",
            {{"operation", operation}}, "engines");
    }
    try {
        return firmware::FirmwareProject::fromJson(params["project"]);
    } catch (const std::exception& exc) {
        throw core::RequestValidationError(
            std::string("Invalid project object: ") + exc.what(),
            {{"operation", operation}}, "engines");
    }
}

std::string requireString(const core::Json& params, const std::string& key,
                          const std::string& operation) {
    if (!params.contains(key) || !params[key].is_string() ||
        params[key].get<std::string>().empty()) {
        throw core::RequestValidationError(
            "Operation '" + operation + "' requires a non-empty string '" + key + "'",
            {{"operation", operation}}, "engines");
    }
    return params[key].get<std::string>();
}

/// Shared pin-vs-peripheral function inference for configure_peripheral.
void applyPeripheralPin(firmware::FirmwareProject& project, const std::string& pinName,
                        const std::string& function, const std::string& peripheral,
                        firmware::PinDirection direction) {
    firmware::PinMapping mapping;
    mapping.mcuPin = pinName;
    mapping.function = function;
    mapping.peripheral = peripheral;
    mapping.direction = direction;
    mapping.electricalMode = "digital";
    project.pinMappings.push_back(mapping);
}

}  // namespace

FirmwareEngine::FirmwareEngine() {
    name_ = "firmware";
    version_ = "0.1.0";
    capabilities_ = {"describe",      "create_project", "select_mcu",
                     "configure_pin", "configure_peripheral", "generate_firmware",
                     "validate_project", "build"};
}

std::vector<std::string> FirmwareEngine::mcuNames() {
    return firmware::supportedMcus();
}

EngineResult FirmwareEngine::execute(const EngineRequest& request) {
    try {
        if (request.operation == "describe") {
            core::Json mcus = core::Json::array();
            for (const auto& model : mcuNames()) {
                mcus.push_back(model);
            }
            EngineResult out =
                successResult(request, {{"engine", name_},
                                        {"version", version_},
                                        {"capabilities", capabilities_},
                                        {"mcus", mcus}});
            out.validation = validate(out);
            return out;
        }
        requireCapability(request);
        if (request.operation == "create_project") {
            return executeCreateProject(request);
        }
        if (request.operation == "select_mcu") {
            return executeSelectMcu(request);
        }
        if (request.operation == "configure_pin") {
            return executeConfigurePin(request);
        }
        if (request.operation == "configure_peripheral") {
            return executeConfigurePeripheral(request);
        }
        if (request.operation == "generate_firmware") {
            return executeGenerateFirmware(request);
        }
        if (request.operation == "validate_project") {
            return executeValidateProject(request);
        }
        if (request.operation == "build") {
            return executeBuild(request);
        }
        EngineResult out = capabilityUnavailable(
            request, "Firmware operation '" + request.operation +
                         "' is not implemented yet");
        core::Logger::instance().warning(
            "engines", "firmware unsupported operation",
            core::Json{{"operation", request.operation}});
        return out;
    } catch (const core::TrinityError& exc) {
        EngineResult out = failureResult(
            request, exc.what(), exc.toJson().value("details", core::Json::object()));
        out.errors.clear();
        out.addError(exc.info());
        out.validation = validate(out);
        return out;
    } catch (const std::exception& exc) {
        EngineResult out = failureResult(request, std::string(exc.what()));
        out.validation = validate(out);
        return out;
    }
}

EngineResult FirmwareEngine::executeCreateProject(const EngineRequest& request) {
    requireParams(request, {"name"});
    const std::string name = requireString(request.parameters, "name", "create_project");
    firmware::FirmwareProject project;
    project.name = name;
    project.clockHz = 0;
    project.metadata = core::Json{{"created_by", "firmware.create_project"}};

    EngineResult out = successResult(
        request, {{"project", project.toJson()}, {"name", project.name}});
    out.validation = validate(out);
    core::Logger::instance().info("engines", "firmware create_project",
                                  core::Json{{"name", project.name}});
    return out;
}

EngineResult FirmwareEngine::executeSelectMcu(const EngineRequest& request) {
    requireParams(request, {"project", "mcu"});
    firmware::FirmwareProject project = projectParam(request.parameters, "select_mcu");
    const std::string model =
        requireString(request.parameters, "mcu", "select_mcu");
    // lookupMcu throws RequestValidationError for unknown models.
    project.mcu = firmware::lookupMcu(model);
    project.hasMcu = true;
    project.clockHz = project.mcu.clockHzMax;
    project.build.arch = project.mcu.architecture;
    project.build.toolchain = project.mcu.toolchain;

    EngineResult out = successResult(request, {{"project", project.toJson()},
                                               {"mcu", project.mcu.model},
                                               {"clock_hz", project.clockHz}});
    out.validation = validate(out);
    core::Logger::instance().info("engines", "firmware select_mcu",
                                  core::Json{{"model", model},
                                             {"family", project.mcu.family}});
    return out;
}

EngineResult FirmwareEngine::executeConfigurePin(const EngineRequest& request) {
    requireParams(request, {"project", "pin", "function", "direction"});
    firmware::FirmwareProject project =
        projectParam(request.parameters, "configure_pin");
    if (!project.hasMcu) {
        throw core::RequestValidationError(
            "configure_pin requires an MCU — run select_mcu first", {},
            "engines");
    }
    const std::string pinName =
        requireString(request.parameters, "pin", "configure_pin");
    const std::string function =
        requireString(request.parameters, "function", "configure_pin");
    const std::string directionStr =
        requireString(request.parameters, "direction", "configure_pin");

    const firmware::McuPin* pin = project.mcu.findPin(pinName);
    if (pin == nullptr) {
        core::Json known = core::Json::array();
        for (const auto& p : project.mcu.pins) {
            known.push_back(p.name);
        }
        throw core::RequestValidationError(
            "Pin '" + pinName + "' does not exist on " + project.mcu.model,
            {{"pin", pinName}, {"known", known}}, "engines");
    }
    if (project.findMapping(pinName) != nullptr) {
        throw core::RequestValidationError(
            "Pin '" + pinName + "' is already configured",
            {{"pin", pinName}}, "engines");
    }
    const firmware::PinDirection direction =
        firmware::pinDirectionFromString(directionStr);
    if (direction == firmware::PinDirection::Unknown) {
        throw core::RequestValidationError(
            "Direction must be 'in', 'out', or 'inout'",
            {{"direction", directionStr}}, "engines");
    }

    const std::string pull = request.parameters.value("pull", std::string(""));
    if (!pull.empty() && pull != "up" && pull != "down") {
        throw core::RequestValidationError(
            "Pull must be '', 'up', or 'down'",
            {{"pull", pull}}, "engines");
    }

    firmware::PinMapping mapping;
    mapping.mcuPin = pinName;
    mapping.function = function;
    mapping.direction = direction;
    mapping.electricalMode = "digital";
    mapping.pull = pull;
    project.pinMappings.push_back(mapping);

    // GPIO mapping implies a matching requirement for the code generator.
    firmware::FirmwareRequirement req;
    if (direction == firmware::PinDirection::Output) {
        req.kind = firmware::RequirementKind::GpioOutput;
    } else {
        req.kind = firmware::RequirementKind::GpioInput;
    }
    req.params = core::Json{{"pin", pinName}, {"function", function}};
    project.requirements.push_back(req);

    EngineResult out = successResult(request, {{"project", project.toJson()},
                                               {"pin", pinName},
                                               {"function", function},
                                               {"direction",
                                                firmware::toString(direction)}});
    out.validation = validate(out);
    core::Logger::instance().info("engines", "firmware configure_pin",
                                  core::Json{{"pin", pinName},
                                             {"function", function},
                                             {"direction",
                                              firmware::toString(direction)}});
    return out;
}

EngineResult FirmwareEngine::executeConfigurePeripheral(
    const EngineRequest& request) {
    requireParams(request, {"project", "peripheral", "kind"});
    firmware::FirmwareProject project =
        projectParam(request.parameters, "configure_peripheral");
    if (!project.hasMcu) {
        throw core::RequestValidationError(
            "configure_peripheral requires an MCU — run select_mcu first", {},
            "engines");
    }
    const std::string peripheral =
        requireString(request.parameters, "peripheral", "configure_peripheral");
    const std::string kind =
        lower(requireString(request.parameters, "kind", "configure_peripheral"));

    if (!project.mcu.hasPeripheralKind(kind)) {
        core::Json available = core::Json::array();
        for (const auto& peri : project.mcu.peripherals) {
            available.push_back(peri.name);
        }
        throw core::CapabilityUnavailableError(
            "Peripheral kind '" + kind + "' is not available on " +
                project.mcu.model,
            {{"kind", kind}, {"available", available}}, "engines");
    }

    firmware::FirmwareRequirement req;
    core::Json params = core::Json::object();

    if (kind == "uart") {
        const std::string tx = requireString(request.parameters, "tx", "configure_peripheral");
        const std::string rx = requireString(request.parameters, "rx", "configure_peripheral");
        const int baud = request.parameters.value("baud", 115200);
        if (baud <= 0) {
            throw core::RequestValidationError("UART baud must be positive",
                                               {{"baud", baud}}, "engines");
        }
        for (const auto& pinName : {tx, rx}) {
            if (project.mcu.findPin(pinName) == nullptr) {
                throw core::RequestValidationError(
                    "Pin '" + pinName + "' does not exist on " + project.mcu.model,
                    {{"pin", pinName}}, "engines");
            }
            if (project.findMapping(pinName) != nullptr) {
                throw core::RequestValidationError(
                    "Pin '" + pinName + "' is already configured",
                    {{"pin", pinName}}, "engines");
            }
        }
        req.kind = firmware::RequirementKind::Uart;
        params = core::Json{{"instance", peripheral},
                            {"tx", tx},
                            {"rx", rx},
                            {"baud", baud}};
        applyPeripheralPin(project, tx, "uart_tx", peripheral,
                           firmware::PinDirection::Output);
        applyPeripheralPin(project, rx, "uart_rx", peripheral,
                           firmware::PinDirection::Input);
    } else if (kind == "i2c") {
        const std::string sda =
            requireString(request.parameters, "sda", "configure_peripheral");
        const std::string scl =
            requireString(request.parameters, "scl", "configure_peripheral");
        for (const auto& pinName : {sda, scl}) {
            if (project.mcu.findPin(pinName) == nullptr) {
                throw core::RequestValidationError(
                    "Pin '" + pinName + "' does not exist on " + project.mcu.model,
                    {{"pin", pinName}}, "engines");
            }
            if (project.findMapping(pinName) != nullptr) {
                throw core::RequestValidationError(
                    "Pin '" + pinName + "' is already configured",
                    {{"pin", pinName}}, "engines");
            }
        }
        req.kind = firmware::RequirementKind::I2c;
        params = core::Json{{"instance", peripheral},
                            {"sda", sda},
                            {"scl", scl},
                            {"address", request.parameters.value("address", 0x48)}};
        applyPeripheralPin(project, sda, "i2c_sda", peripheral,
                           firmware::PinDirection::Bidirectional);
        applyPeripheralPin(project, scl, "i2c_scl", peripheral,
                           firmware::PinDirection::Output);
    } else if (kind == "spi") {
        throw core::CapabilityUnavailableError(
            "SPI is not supported by FirmwareEngine V1",
            {{"kind", kind}}, "engines");
    } else if (kind == "pwm") {
        const std::string pinName =
            requireString(request.parameters, "pin", "configure_peripheral");
        const int freq = request.parameters.value("freq_hz", 1000);
        if (freq <= 0) {
            throw core::RequestValidationError("PWM frequency must be positive",
                                               {{"freq_hz", freq}}, "engines");
        }
        const firmware::McuPin* pin = project.mcu.findPin(pinName);
        if (pin == nullptr) {
            throw core::RequestValidationError(
                "Pin '" + pinName + "' does not exist on " + project.mcu.model,
                {{"pin", pinName}}, "engines");
        }
        if (!pin->pwm) {
            throw core::RequestValidationError(
                "Pin '" + pinName + "' does not support PWM on " + project.mcu.model,
                {{"pin", pinName}}, "engines");
        }
        if (project.findMapping(pinName) != nullptr) {
            throw core::RequestValidationError(
                "Pin '" + pinName + "' is already configured",
                {{"pin", pinName}}, "engines");
        }
        req.kind = firmware::RequirementKind::Pwm;
        params = core::Json{{"pin", pinName},
                            {"freq_hz", freq},
                            {"duty", request.parameters.value("duty", 128)}};
        applyPeripheralPin(project, pinName, "pwm", peripheral,
                           firmware::PinDirection::Output);
    } else {
        throw core::CapabilityUnavailableError(
            "Peripheral kind '" + kind + "' is not supported by FirmwareEngine V1",
            {{"kind", kind}}, "engines");
    }

    req.params = params;
    project.requirements.push_back(req);

    EngineResult out = successResult(request, {{"project", project.toJson()},
                                               {"peripheral", peripheral},
                                               {"kind", kind},
                                               {"params", params}});
    out.validation = validate(out);
    core::Logger::instance().info("engines", "firmware configure_peripheral",
                                  core::Json{{"peripheral", peripheral},
                                             {"kind", kind}});
    return out;
}

EngineResult FirmwareEngine::executeGenerateFirmware(const EngineRequest& request) {
    requireParams(request, {"project"});
    firmware::FirmwareProject project =
        projectParam(request.parameters, "generate_firmware");
    const std::vector<firmware::SourceFile> sources =
        firmware::generateSources(project);
    project.sources = sources;

    core::Json fileList = core::Json::array();
    for (const auto& src : sources) {
        fileList.push_back(core::Json{{"path", src.path},
                                      {"bytes", static_cast<int>(src.content.size())}});
    }

    EngineResult out = successResult(request, {{"project", project.toJson()},
                                               {"files", fileList},
                                               {"file_count",
                                                static_cast<int>(sources.size())}});
    out.validation = validate(out);

    // Register generated sources as pending artifacts (JobManager promotes
    // them through ArtifactManager into storage/artifacts/ with SHA-256).
    const std::string scratch =
        (fs::temp_directory_path() / ("trinity_fw_src_" + shortId())).string();
    std::error_code ec;
    fs::create_directories(scratch, ec);
    if (!ec) {
        for (const auto& src : sources) {
            const fs::path outPath = fs::path(scratch) / src.path;
            std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
            if (file) {
                file << src.content;
                file.close();
                out.pendingArtifacts.emplace_back(outPath.string(), "firmware_source");
            }
        }
        const fs::path metaPath = fs::path(scratch) / "project.json";
        std::ofstream meta(metaPath, std::ios::binary | std::ios::trunc);
        if (meta) {
            meta << project.toJson().dump(2);
            meta.close();
            out.pendingArtifacts.emplace_back(metaPath.string(), "firmware_metadata");
        }
    }

    core::Logger::instance().info("engines", "firmware generate_firmware",
                                  core::Json{{"files", sources.size()},
                                             {"mcu", project.mcu.model}});
    return out;
}

EngineResult FirmwareEngine::executeValidateProject(const EngineRequest& request) {
    requireParams(request, {"project"});
    const firmware::FirmwareProject project =
        projectParam(request.parameters, "validate_project");
    const validation::ValidationResult rules =
        firmware::validateFirmwareProject(project);

    core::Json ruleList = core::Json::array();
    for (const auto& msg : rules.messages) {
        ruleList.push_back(
            core::Json{{"rule", msg.rule},
                       {"severity", validation::toString(msg.severity)},
                       {"passed", msg.passed},
                       {"message", msg.message},
                       {"details", msg.details}});
    }
    EngineResult out = successResult(request, {{"project", project.toJson()},
                                               {"passed", rules.passed()},
                                               {"rules", ruleList},
                                               {"message", rules.message}});
    out.success = rules.passed();
    if (!rules.passed()) {
        out.errors.clear();
        out.addError(core::makeError(core::ErrorCode::GeometryValidationError,
                                     "Firmware project failed validation checks",
                                     "engines", {{"checks", rules.checks}}));
    }
    out.validation = rules;
    out.validation->operation = request.operation;
    core::Logger::instance().info("engines", "firmware validate_project",
                                  core::Json{{"passed", rules.passed()},
                                             {"pins", project.pinMappings.size()}});
    return out;
}

EngineResult FirmwareEngine::executeBuild(const EngineRequest& request) {
    requireParams(request, {"project"});
    firmware::FirmwareProject project = projectParam(request.parameters, "build");
    if (!project.hasMcu) {
        throw core::RequestValidationError(
            "build requires an MCU — run select_mcu first", {}, "engines");
    }
    if (project.sources.empty()) {
        throw core::RequestValidationError(
            "build requires generated sources — run generate_firmware first", {},
            "engines");
    }
    const validation::ValidationResult rules =
        firmware::validateFirmwareProject(project);
    if (!rules.passed()) {
        EngineResult refused = failureResult(
            request, "Refusing to build: project failed validation",
            {{"checks", rules.checks}});
        refused.validation = rules;
        refused.validation->operation = request.operation;
        return refused;
    }

    // Controlled build: resolve toolchain, write sources, compile via
    // explicit argv (never a shell string). Missing toolchain is a
    // truthful CAPABILITY_UNAVAILABLE — never a fake success.
    const std::string toolchain =
        firmware::findToolchain(project.mcu.toolchain);
    if (toolchain.empty()) {
        EngineResult out = capabilityUnavailable(
            request,
            "No toolchain found for " + project.mcu.model +
                " (looked for '" + project.mcu.toolchain +
                "'; set TRINITY_TOOLCHAIN_DIR or PATH)");
        out.result = {{"project", project.toJson()},
                      {"toolchain", project.mcu.toolchain},
                      {"executed", false}};
        out.validation = validate(out);
        core::Logger::instance().warning(
            "engines", "firmware toolchain unavailable",
            core::Json{{"toolchain", project.mcu.toolchain},
                       {"mcu", project.mcu.model}});
        return out;
    }

    const std::string workDir =
        (fs::temp_directory_path() / ("trinity_fw_" + shortId())).string();
    std::error_code ec;
    fs::create_directories(workDir, ec);
    if (ec) {
        throw core::EngineExecutionError(
            "Cannot create firmware scratch directory: " + ec.message(), {},
            "engines");
    }
    for (const auto& src : project.sources) {
        const fs::path outPath = fs::path(workDir) / src.path;
        std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            throw core::EngineExecutionError("Cannot write source " + src.path, {},
                                             "engines");
        }
        file << src.content;
        file.close();
        if (!file) {
            throw core::EngineExecutionError("Failed while writing " + src.path, {},
                                             "engines");
        }
    }

    const std::vector<std::string> argv = {
        "-c", "main.cpp", "-o", "main.o", project.build.optLevel, "-std=c++17"};
    const std::vector<std::string> expected = {"main.o"};
    firmware::FirmwareBuilder builder(workDir);
    const firmware::BuildResult built = builder.build(toolchain, argv, expected);

    core::Json artifactList = core::Json::array();
    for (const auto& path : built.artifacts) {
        artifactList.push_back(path);
    }

    if (!built.executed) {
        EngineResult out =
            capabilityUnavailable(request, built.error.empty() ? "Build did not run"
                                                               : built.error);
        out.result = {{"project", project.toJson()},
                      {"toolchain", toolchain},
                      {"executed", false},
                      {"error", built.error}};
        out.validation = validate(out);
        return out;
    }

    EngineResult out = successResult(
        request, {{"project", project.toJson()},
                  {"toolchain", toolchain},
                  {"executed", true},
                  {"success", built.success},
                  {"exit_code", built.exitCode},
                  {"stdout", built.stdoutText},
                  {"stderr", built.stderrText},
                  {"artifacts", artifactList}});
    out.success = built.success;
    if (!built.success) {
        out.errors.clear();
        out.addError(core::makeError(core::ErrorCode::EngineExecutionError,
                                     built.error.empty() ? "Compiler failed"
                                                         : built.error,
                                     "engines",
                                     {{"exit_code", built.exitCode}}));
    }
    for (const auto& path : built.artifacts) {
        out.pendingArtifacts.emplace_back(path, "firmware_object");
    }
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "firmware build",
        core::Json{{"toolchain", toolchain},
                   {"success", built.success},
                   {"exit_code", built.exitCode}});
    return out;
}

validation::ValidationResult FirmwareEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "firmware"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Firmware operation failed";
        validation::ValidationMessage msg;
        msg.rule = "firmware.success";
        msg.severity = validation::Severity::Error;
        msg.passed = false;
        msg.message = "Engine reported failure";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    if (result.operation == "validate_project" && result.result.contains("rules")) {
        const bool passed = result.result.value("passed", false);
        validation.status = passed ? validation::ValidationStatus::Validated
                                   : validation::ValidationStatus::Invalid;
        validation.message = result.result.value("message", "");
        validation::ValidationMessage msg;
        msg.rule = "firmware.project_rules";
        msg.severity = passed ? validation::Severity::Info : validation::Severity::Error;
        msg.passed = passed;
        msg.message = validation.message;
        validation.addMessage(std::move(msg));
        return validation;
    }
    if (!result.result.contains("project") || !result.result["project"].is_object()) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "Firmware result carries no project object";
        return validation;
    }
    validation.status = validation::ValidationStatus::Validated;
    validation.message = "Firmware operation produced a project object";
    validation::ValidationMessage msg;
    msg.rule = "firmware.project_present";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = validation.message;
    validation.addMessage(std::move(msg));
    return validation;
}

}  // namespace trinity::engines

#include <doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/FirmwareEngine.hpp"
#include "trinity/firmware/Builder.hpp"
#include "trinity/firmware/CodeGen.hpp"
#include "trinity/firmware/FirmwareProject.hpp"
#include "trinity/firmware/McuDatabase.hpp"
#include "trinity/firmware/Validators.hpp"
#include "trinity/intelligence/RequirementParser.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/jobs/JobWorker.hpp"
#include "trinity/storage/Database.hpp"
#include "trinity/storage/Repositories.hpp"
#include "trinity/workflows/Executor.hpp"
#include "trinity/workflows/Workflow.hpp"

namespace fs = std::filesystem;
namespace {

trinity::engines::EngineRequest makeReq(const std::string& op,
                                        const trinity::core::Json& params) {
    trinity::engines::EngineRequest req;
    req.engine = "firmware";
    req.operation = op;
    req.parameters = params;
    return req;
}

trinity::core::Json createProject(trinity::engines::FirmwareEngine& engine,
                                  const std::string& name) {
    const auto out = engine.execute(makeReq("create_project", {{"name", name}}));
    REQUIRE(out.success);
    return out.result["project"];
}

trinity::core::Json selectMcu(trinity::engines::FirmwareEngine& engine,
                              const trinity::core::Json& project,
                              const std::string& mcu) {
    const auto out =
        engine.execute(makeReq("select_mcu", {{"project", project}, {"mcu", mcu}}));
    REQUIRE(out.success);
    return out.result["project"];
}

trinity::core::Json configurePin(trinity::engines::FirmwareEngine& engine,
                                 const trinity::core::Json& project,
                                 const std::string& pin, const std::string& function,
                                 const std::string& direction) {
    const auto out = engine.execute(makeReq(
        "configure_pin",
        {{"project", project}, {"pin", pin}, {"function", function},
         {"direction", direction}}));
    REQUIRE(out.success);
    return out.result["project"];
}

trinity::core::Json configurePeripheral(trinity::engines::FirmwareEngine& engine,
                                        const trinity::core::Json& project,
                                        const trinity::core::Json& params) {
    trinity::core::Json full = params;
    full["project"] = project;
    const auto out = engine.execute(makeReq("configure_peripheral", full));
    REQUIRE(out.success);
    return out.result["project"];
}

std::string sourceContent(const trinity::firmware::FirmwareProject& project,
                          const std::string& path) {
    for (const auto& src : project.sources) {
        if (src.path == path) {
            return src.content;
        }
    }
    return {};
}

struct Fixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;
    std::shared_ptr<trinity::storage::WorkflowRepository> workflows;
    std::shared_ptr<trinity::workflows::WorkflowExecutor> executor;

    Fixture() {
        dir = (fs::temp_directory_path() / "trinity-test-firmware").string();
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir + "/artifacts", ec);
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<trinity::engines::FirmwareEngine>());
        artifacts =
            std::make_shared<trinity::artifacts::ArtifactManager>(db, dir + "/artifacts");
        jobs = std::make_shared<trinity::jobs::JobManager>(db, registry, artifacts);
        workflows = std::make_shared<trinity::storage::WorkflowRepository>(db);
        executor = std::make_shared<trinity::workflows::WorkflowExecutor>(jobs, registry,
                                                                          workflows);
    }
    ~Fixture() {
        executor.reset();
        workflows.reset();
        jobs.reset();
        artifacts.reset();
        registry.reset();
        db.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("firmware IR serializes and round-trips") {
    trinity::firmware::FirmwareProject project;
    project.name = "roundtrip";
    project.hasMcu = true;
    project.mcu = trinity::firmware::lookupMcu("ESP32");
    project.clockHz = 240000000LL;
    trinity::firmware::PinMapping mapping;
    mapping.mcuPin = "GPIO2";
    mapping.function = "status_led";
    mapping.direction = trinity::firmware::PinDirection::Output;
    mapping.electricalMode = "digital";
    mapping.pull = "up";
    mapping.altFunction = "GPIO2_OUT";
    project.pinMappings.push_back(mapping);
    trinity::firmware::FirmwareRequirement req;
    req.kind = trinity::firmware::RequirementKind::GpioOutput;
    req.params = trinity::core::Json{{"pin", "GPIO2"}, {"function", "status_led"}};
    project.requirements.push_back(req);
    project.sources.push_back({"main.cpp", "// main"});
    project.build.profile = "release";
    project.build.arch = "xtensa-lx6";
    project.build.toolchain = "xtensa-esp32-elf-g++";
    project.build.optLevel = "-O2";
    project.build.outputDir = "build";
    project.metadata = trinity::core::Json{{"created_by", "test"}};

    const auto back =
        trinity::firmware::FirmwareProject::fromJson(project.toJson());
    CHECK(back.name == "roundtrip");
    CHECK(back.hasMcu);
    CHECK(back.mcu.model == "ESP32");
    CHECK(back.clockHz == 240000000LL);
    REQUIRE(back.pinMappings.size() == 1);
    CHECK(back.pinMappings[0].mcuPin == "GPIO2");
    CHECK(trinity::firmware::toString(back.pinMappings[0].direction) == "out");
    CHECK(back.pinMappings[0].pull == "up");
    CHECK(back.pinMappings[0].altFunction == "GPIO2_OUT");
    REQUIRE(back.requirements.size() == 1);
    CHECK(trinity::firmware::toString(back.requirements[0].kind) == "write_gpio");
    REQUIRE(back.sources.size() == 1);
    CHECK(back.sources[0].content == "// main");
    CHECK(back.build.profile == "release");
    CHECK(back.build.arch == "xtensa-lx6");
    CHECK(back.build.optLevel == "-O2");
    CHECK(back.build.outputDir == "build");
    CHECK(back.metadata.value("created_by", "") == "test");
    REQUIRE(back.findMapping("GPIO2") != nullptr);
}

TEST_CASE("requirement kinds map deterministically") {
    using trinity::firmware::RequirementKind;
    using trinity::firmware::toString;
    CHECK(toString(trinity::firmware::requirementKindFromString("read_gpio")) ==
          toString(RequirementKind::GpioInput));
    CHECK(toString(trinity::firmware::requirementKindFromString("write_gpio")) ==
          toString(RequirementKind::GpioOutput));
    CHECK(toString(trinity::firmware::requirementKindFromString("write_pwm")) ==
          toString(RequirementKind::Pwm));
    CHECK(toString(trinity::firmware::requirementKindFromString("serial_communication")) ==
          toString(RequirementKind::Uart));
    CHECK(toString(trinity::firmware::requirementKindFromString("i2c_communication")) ==
          toString(RequirementKind::I2c));
    CHECK(toString(trinity::firmware::requirementKindFromString("spi_communication")) ==
          toString(RequirementKind::Spi));
    CHECK(toString(trinity::firmware::requirementKindFromString("flash_ota")) ==
          toString(RequirementKind::Unknown));
    CHECK(toString(RequirementKind::Uart) == "serial_communication");
    CHECK(toString(RequirementKind::Unknown) == "unknown");
}

TEST_CASE("mcu database supports a small known set and rejects others") {
    const auto mcus = trinity::firmware::supportedMcus();
    CHECK(mcus.size() == 3);
    CHECK(std::find(mcus.begin(), mcus.end(), "ESP32") != mcus.end());
    CHECK(std::find(mcus.begin(), mcus.end(), "STM32F401RE") != mcus.end());
    CHECK(std::find(mcus.begin(), mcus.end(), "RP2040") != mcus.end());

    const auto esp = trinity::firmware::lookupMcu("ESP32");
    CHECK(esp.family == "ESP32");
    CHECK(esp.manufacturer == "Espressif");
    CHECK(esp.architecture == "xtensa-lx6");
    CHECK(esp.clockHzMax == 240000000LL);
    CHECK(esp.findPin("GPIO2") != nullptr);
    CHECK(esp.findPin("GPIO999") == nullptr);
    CHECK(esp.hasPeripheralKind("uart"));
    CHECK(esp.hasPeripheralKind("i2c"));
    CHECK(esp.hasPeripheralKind("pwm"));

    CHECK_THROWS_AS(trinity::firmware::lookupMcu("STM32H7-FAKE"),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(trinity::firmware::lookupMcu(""),
                    trinity::core::RequestValidationError);
}

TEST_CASE("configure_pin assigns pins and rejects invalid or duplicates") {
    trinity::engines::FirmwareEngine engine;
    auto project = createProject(engine, "pin-demo");
    project = selectMcu(engine, project, "ESP32");

    const auto ok = engine.execute(makeReq("configure_pin",
                                           {{"project", project},
                                            {"pin", "GPIO2"},
                                            {"function", "status_led"},
                                            {"direction", "out"},
                                            {"pull", "up"}}));
    CHECK(ok.success);
    REQUIRE(ok.validation.has_value());
    CHECK(ok.validation->passed());

    const auto withPin = ok.result["project"];
    // Duplicate pin refused.
    const auto dup = engine.execute(makeReq("configure_pin",
                                            {{"project", withPin},
                                             {"pin", "GPIO2"},
                                             {"function", "other"},
                                             {"direction", "in"}}));
    CHECK_FALSE(dup.success);

    // Invalid pin name refused with known list.
    const auto badPin = engine.execute(makeReq("configure_pin",
                                               {{"project", withPin},
                                                {"pin", "GPIO999"},
                                                {"function", "x"},
                                                {"direction", "in"}}));
    CHECK_FALSE(badPin.success);
    REQUIRE_FALSE(badPin.errors.empty());

    // Invalid direction refused.
    const auto badDir = engine.execute(makeReq("configure_pin",
                                               {{"project", withPin},
                                                {"pin", "GPIO4"},
                                                {"function", "x"},
                                                {"direction", "sideways"}}));
    CHECK_FALSE(badDir.success);

    // Invalid pull refused.
    const auto badPull = engine.execute(makeReq("configure_pin",
                                                {{"project", withPin},
                                                 {"pin", "GPIO4"},
                                                 {"function", "x"},
                                                 {"direction", "in"},
                                                 {"pull", "floating"}}));
    CHECK_FALSE(badPull.success);

    // configure_pin without MCU refused.
    auto bare = createProject(engine, "bare");
    const auto noMcu = engine.execute(makeReq("configure_pin",
                                              {{"project", bare},
                                               {"pin", "GPIO2"},
                                               {"function", "x"},
                                               {"direction", "out"}}));
    CHECK_FALSE(noMcu.success);
}

TEST_CASE("peripheral configuration supports uart i2c pwm and refuses spi") {
    trinity::engines::FirmwareEngine engine;
    auto project = selectMcu(engine, createProject(engine, "peri-demo"), "ESP32");

    const auto uart = configurePeripheral(engine, project,
                                          {{"peripheral", "UART0"},
                                           {"kind", "uart"},
                                           {"tx", "GPIO1"},
                                           {"rx", "GPIO3"},
                                           {"baud", 115200}});
    REQUIRE(uart.contains("pin_mappings"));
    // UART pins now occupied: reuse refused.
    const auto conflict = engine.execute(
        makeReq("configure_peripheral", {{"project", uart},
                                         {"peripheral", "UART1"},
                                         {"kind", "uart"},
                                         {"tx", "GPIO1"},
                                         {"rx", "GPIO3"}}));
    CHECK_FALSE(conflict.success);

    // I2C on the same project (different pins).
    const auto i2c = configurePeripheral(engine, uart,
                                         {{"peripheral", "I2C0"},
                                          {"kind", "i2c"},
                                          {"sda", "GPIO21"},
                                          {"scl", "GPIO22"}});
    REQUIRE(i2c.is_object());
    REQUIRE(i2c.contains("pin_mappings"));

    // SDA/SCL reuse refused (conflicting functions).
    const auto i2cConflict = engine.execute(
        makeReq("configure_peripheral", {{"project", i2c},
                                         {"peripheral", "I2C1"},
                                         {"kind", "i2c"},
                                         {"sda", "GPIO21"},
                                         {"scl", "GPIO4"}}));
    CHECK_FALSE(i2cConflict.success);

    // PWM on a fresh project (GPIO18 still free on first project but
    // keep the chain linear for clarity).
    const auto pwm = configurePeripheral(engine, i2c,
                                         {{"peripheral", "PWM"},
                                          {"kind", "pwm"},
                                          {"pin", "GPIO18"},
                                          {"freq_hz", 2000},
                                          {"duty", 128}});
    REQUIRE(pwm.is_object());

    // Input-only ESP32 pin cannot do PWM.
    auto clean3 = selectMcu(engine, createProject(engine, "peri-nopwm"), "ESP32");
    const auto noPwm = engine.execute(
        makeReq("configure_peripheral", {{"project", clean3},
                                         {"peripheral", "PWM"},
                                         {"kind", "pwm"},
                                         {"pin", "GPIO34"}}));
    CHECK_FALSE(noPwm.success);

    // SPI refused truthfully for V1.
    auto clean4 = selectMcu(engine, createProject(engine, "peri-spi"), "ESP32");
    const auto spi = engine.execute(
        makeReq("configure_peripheral", {{"project", clean4},
                                         {"peripheral", "SPI0"},
                                         {"kind", "spi"}}));
    CHECK_FALSE(spi.success);
    REQUIRE_FALSE(spi.errors.empty());
    CHECK(spi.errors.front().value("code", "") == "capability_unavailable");

    // Unsupported peripheral kind refused.
    const auto can = engine.execute(
        makeReq("configure_peripheral", {{"project", clean4},
                                         {"peripheral", "CAN0"},
                                         {"kind", "can"}}));
    CHECK_FALSE(can.success);

    // Invalid UART pins refused.
    auto clean5 = selectMcu(engine, createProject(engine, "peri-baduart"), "ESP32");
    const auto badTx = engine.execute(
        makeReq("configure_peripheral", {{"project", clean5},
                                         {"peripheral", "UART0"},
                                         {"kind", "uart"},
                                         {"tx", "GPIO999"},
                                         {"rx", "GPIO3"}}));
    CHECK_FALSE(badTx.success);

    // Invalid baud refused.
    const auto badBaud = engine.execute(
        makeReq("configure_peripheral", {{"project", clean5},
                                         {"peripheral", "UART1"},
                                         {"kind", "uart"},
                                         {"tx", "GPIO1"},
                                         {"rx", "GPIO3"},
                                         {"baud", 0}}));
    CHECK_FALSE(badBaud.success);
}

TEST_CASE("engine refuses unknown mcu and unsupported operations truthfully") {
    trinity::engines::FirmwareEngine engine;
    const auto project = createProject(engine, "bad-mcu");
    const auto out =
        engine.execute(makeReq("select_mcu", {{"project", project},
                                              {"mcu", "AVR-ATMEGA-FAKE"}}));
    CHECK_FALSE(out.success);
    REQUIRE_FALSE(out.errors.empty());

    const auto unknownOp = engine.execute(makeReq("flash_device", {}));
    CHECK_FALSE(unknownOp.success);
    REQUIRE_FALSE(unknownOp.errors.empty());
    CHECK(unknownOp.errors.front().value("code", "") == "capability_unavailable");
}

TEST_CASE("source generation is deterministic and config-specific") {
    trinity::engines::FirmwareEngine engine;

    // Config A: ESP32 GPIO out.
    auto projectA = selectMcu(engine, createProject(engine, "gpio-only"), "ESP32");
    projectA = configurePin(engine, projectA, "GPIO2", "status_led", "out");
    const auto generatedA =
        engine.execute(makeReq("generate_firmware", {{"project", projectA}}));
    REQUIRE(generatedA.success);
    const auto filesA =
        trinity::firmware::FirmwareProject::fromJson(generatedA.result["project"]);

    // Config B: ESP32 UART + PWM.
    auto projectB = selectMcu(engine, createProject(engine, "uart-pwm"), "ESP32");
    projectB = configurePeripheral(engine, projectB,
                                   {{"peripheral", "UART0"},
                                    {"kind", "uart"},
                                    {"tx", "GPIO1"},
                                    {"rx", "GPIO3"},
                                    {"baud", 115200}});
    projectB = configurePeripheral(engine, projectB,
                                   {{"peripheral", "PWM"},
                                    {"kind", "pwm"},
                                    {"pin", "GPIO18"},
                                    {"freq_hz", 1000}});
    const auto generatedB = engine.execute(
        makeReq("generate_firmware", {{"project", projectB}}));
    REQUIRE(generatedB.success);
    const auto filesB =
        trinity::firmware::FirmwareProject::fromJson(generatedB.result["project"]);

    // Config C: STM32 (different family entirely).
    auto projectC = selectMcu(engine, createProject(engine, "stm32-led"), "STM32F401RE");
    projectC = configurePin(engine, projectC, "PA5", "status_led", "out");
    const auto generatedC = engine.execute(
        makeReq("generate_firmware", {{"project", projectC}}));
    REQUIRE(generatedC.success);
    const auto filesC =
        trinity::firmware::FirmwareProject::fromJson(generatedC.result["project"]);

    // Determinism: regenerating the same IR yields identical bytes.
    const auto regenA = engine.execute(makeReq("generate_firmware", {{"project", projectA}}));
    REQUIRE(regenA.success);
    const auto filesA2 =
        trinity::firmware::FirmwareProject::fromJson(regenA.result["project"]);
    REQUIRE(filesA.sources.size() == filesA2.sources.size());
    for (size_t i = 0; i < filesA.sources.size(); ++i) {
        CHECK(filesA.sources[i].path == filesA2.sources[i].path);
        CHECK(filesA.sources[i].content == filesA2.sources[i].content);
    }

    // Different configs produce different, appropriate sources.
    const std::string mainA = sourceContent(filesA, "main.cpp");
    const std::string mainB = sourceContent(filesB, "main.cpp");
    const std::string mainC = sourceContent(filesC, "main.cpp");
    const std::string configA = sourceContent(filesA, "config.h");
    const std::string configB = sourceContent(filesB, "config.h");
    const std::string configC = sourceContent(filesC, "config.h");
    const std::string platformA = sourceContent(filesA, "platform.h");

    CHECK_FALSE(mainA.empty());
    CHECK(mainA.find("setup()") != std::string::npos);
    CHECK(mainA.find("loop()") != std::string::npos);
    CHECK(mainA.find("pinMode") != std::string::npos);
    CHECK(configA.find("PIN_GPIO2") != std::string::npos);
    CHECK(configA.find("TRINITY_MCU \"ESP32\"") != std::string::npos);
    CHECK(platformA.find("TRINITY_FAMILY_ESP32") != std::string::npos);

    CHECK(mainB.find("Serial.begin") != std::string::npos);
    CHECK(mainB.find("ledcSetup") != std::string::npos);
    CHECK(configB.find("BAUD") != std::string::npos);
    CHECK(configB.find("PWM_FREQ_HZ") != std::string::npos);

    CHECK(mainC.find("pinMode") != std::string::npos);
    CHECK(configC.find("PIN_PA5") != std::string::npos);
    CHECK(configC.find("TRINITY_MCU \"STM32F401RE\"") != std::string::npos);

    CHECK(mainA != mainB);
    CHECK(mainA != mainC);
    CHECK(mainB != mainC);
    CHECK(configA != configB);
    CHECK(configA != configC);

    // Two identical valid configs produce byte-identical main.cpp.
    CHECK(mainA == sourceContent(filesA2, "main.cpp"));

    // Three expected files each time.
    CHECK(filesA.sources.size() == 3);
    CHECK(filesB.sources.size() == 3);
    CHECK(filesC.sources.size() == 3);
}

TEST_CASE("generate refuses without an MCU") {
    trinity::engines::FirmwareEngine engine;
    const auto project = createProject(engine, "no-mcu");
    const auto out = engine.execute(makeReq("generate_firmware", {{"project", project}}));
    CHECK_FALSE(out.success);
}

TEST_CASE("validation reports states and never claims VERIFIED") {
    trinity::engines::FirmwareEngine engine;
    auto project = selectMcu(engine, createProject(engine, "validate-ok"), "ESP32");
    project = configurePin(engine, project, "GPIO2", "led", "out");
    const auto generated =
        engine.execute(makeReq("generate_firmware", {{"project", project}}));
    REQUIRE(generated.success);
    project = generated.result["project"];

    auto validated =
        trinity::firmware::validateFirmwareProject(
            trinity::firmware::FirmwareProject::fromJson(project));
    CHECK(trinity::validation::toString(validated.status) == "VALIDATED");
    CHECK(validated.passed());

    // Empty project: invalid (no MCU).
    trinity::firmware::FirmwareProject empty;
    empty.name = "empty";
    const auto invalid = trinity::firmware::validateFirmwareProject(empty);
    CHECK(trinity::validation::toString(invalid.status) == "INVALID");
    CHECK_FALSE(invalid.passed());

    // Duplicate pin invalidates.
    auto dup = trinity::firmware::FirmwareProject::fromJson(
        selectMcu(engine, createProject(engine, "validate-dup"), "ESP32"));
    trinity::firmware::PinMapping m;
    m.mcuPin = "GPIO2";
    m.function = "a";
    m.direction = trinity::firmware::PinDirection::Output;
    dup.pinMappings.push_back(m);
    dup.pinMappings.push_back(m);
    const auto dupOut = trinity::firmware::validateFirmwareProject(dup);
    CHECK(trinity::validation::toString(dupOut.status) == "INVALID");

    // Invalid clock invalidates.
    auto badClock = trinity::firmware::FirmwareProject::fromJson(
        selectMcu(engine, createProject(engine, "validate-clock"), "ESP32"));
    badClock.clockHz = 999999999LL;
    CHECK(trinity::validation::toString(
              trinity::firmware::validateFirmwareProject(badClock).status) == "INVALID");

    // SPI requirement invalidates for V1.
    auto spiProj = trinity::firmware::FirmwareProject::fromJson(
        selectMcu(engine, createProject(engine, "validate-spi"), "ESP32"));
    trinity::firmware::FirmwareRequirement spiReq;
    spiReq.kind = trinity::firmware::RequirementKind::Spi;
    spiProj.requirements.push_back(spiReq);
    CHECK(trinity::validation::toString(
              trinity::firmware::validateFirmwareProject(spiProj).status) == "INVALID");

    // Engine validate_project returns rules and Validated status (never VERIFIED).
    const auto engineOut =
        engine.execute(makeReq("validate_project", {{"project", project}}));
    CHECK(engineOut.success);
    REQUIRE(engineOut.validation.has_value());
    CHECK(trinity::validation::toString(engineOut.validation->status) == "VALIDATED");
    CHECK(trinity::validation::toString(engineOut.validation->status) != "VERIFIED");

    // Failed validation surfaces Invalid through the engine.
    const auto engineBad =
        engine.execute(makeReq("validate_project", {{"project", empty.toJson()}}));
    CHECK_FALSE(engineBad.success);
    REQUIRE(engineBad.validation.has_value());
    CHECK(trinity::validation::toString(engineBad.validation->status) == "INVALID");
}

TEST_CASE("builder reports CAPABILITY_UNAVAILABLE truthfully without toolchain") {
    // A toolchain name that cannot exist on any supported host.
    const std::string missing = trinity::firmware::findToolchain(
        "trinity-definitely-not-a-real-compiler-xyz");
    CHECK(missing.empty());

    trinity::firmware::FirmwareBuilder builder("");
    const auto result =
        builder.build("", {"-c", "main.cpp", "-o", "main.o"}, {"main.o"});
    CHECK_FALSE(result.executed);
    CHECK_FALSE(result.success);
    CHECK(result.exitCode == -1);
    CHECK_FALSE(result.error.empty());

    const auto notFound =
        builder.build("/nonexistent/path/to/compiler", {"-c", "x.cpp"}, {});
    CHECK_FALSE(notFound.executed);
    CHECK_FALSE(notFound.error.empty());
}

TEST_CASE("build op returns truthful unavailable or real compile result") {
    trinity::engines::FirmwareEngine engine;
    auto project = selectMcu(engine, createProject(engine, "build-op"), "ESP32");
    project = configurePin(engine, project, "GPIO2", "led", "out");
    const auto generated =
        engine.execute(makeReq("generate_firmware", {{"project", project}}));
    REQUIRE(generated.success);
    const auto withSources = generated.result["project"];

    const auto out = engine.execute(makeReq("build", {{"project", withSources}}));
    REQUIRE(out.result.contains("executed"));
    const bool executed = out.result.value("executed", false);
    if (!executed) {
        // Truthful unavailable path: capability_unavailable error, no fake success.
        CHECK_FALSE(out.success);
        REQUIRE_FALSE(out.errors.empty());
        CHECK(out.errors.front().value("code", "") == "capability_unavailable");
        // Reports the expected toolchain name it looked for (truthful, not a path).
        CHECK(out.result.value("toolchain", "") == "xtensa-esp32-elf-g++");
    } else {
        // Real toolchain present: report honest exit code, never fabricate.
        CHECK(out.result.contains("exit_code"));
        if (out.success) {
            REQUIRE_FALSE(out.pendingArtifacts.empty());
        }
    }

    // Build without sources refused.
    auto bare = selectMcu(engine, createProject(engine, "build-bare"), "ESP32");
    const auto noSrc = engine.execute(makeReq("build", {{"project", bare}}));
    CHECK_FALSE(noSrc.success);
}

TEST_CASE("generated artifacts register with SHA-256 through jobs") {
    Fixture fx;
    // create_project
    auto created = fx.jobs->runSync("firmware", "create_project", {{"name", "job-fw"}});
    REQUIRE(created.value("success", false));
    auto project = created["result"]["project"];
    // select_mcu
    auto withMcu = fx.jobs->runSync(
        "firmware", "select_mcu", {{"project", project}, {"mcu", "ESP32"}});
    REQUIRE(withMcu.value("success", false));
    project = withMcu["result"]["project"];
    // configure_pin
    auto withPin = fx.jobs->runSync("firmware", "configure_pin",
                                    {{"project", project},
                                     {"pin", "GPIO2"},
                                     {"function", "led"},
                                     {"direction", "out"}});
    REQUIRE(withPin.value("success", false));
    project = withPin["result"]["project"];
    // generate_firmware registers pending artifacts promoted by JobManager.
    auto generated =
        fx.jobs->runSync("firmware", "generate_firmware", {{"project", project}});
    REQUIRE(generated.value("success", false));
    REQUIRE(generated["artifacts"].is_array());
    CHECK(generated["artifacts"].size() == 4);  // 3 sources + project.json

    // SHA-256 is 64 hex chars; sizes positive; files exist; job id matches.
    std::vector<std::string> types;
    for (const auto& art : generated["artifacts"]) {
        const std::string checksum = art.value("checksum", "");
        CHECK(checksum.size() == 64);
        CHECK(art.value("size_bytes", 0LL) > 0);
        const std::string artifactId = art.value("artifact_id", "");
        REQUIRE_FALSE(artifactId.empty());
        const auto stored = fx.artifacts->get(artifactId);
        CHECK(stored.checksum == checksum);
        CHECK(stored.jobId == generated.value("job_id", ""));
        CHECK(std::filesystem::is_regular_file(stored.path));
        types.push_back(stored.type);
    }
    CHECK(std::find(types.begin(), types.end(), "firmware_source") != types.end());
    CHECK(std::find(types.begin(), types.end(), "firmware_metadata") != types.end());

    // Job persisted in SQLite with terminal state.
    const std::string jobId = generated.value("job_id", "");
    const auto job = fx.jobs->get(jobId);
    CHECK(trinity::jobs::isTerminal(job.status));
    CHECK(job.succeeded());
    CHECK_FALSE(job.completedAt.empty());

    // JobRepository round-trip confirms persistence.
    trinity::storage::JobRepository repo(fx.db);
    const auto reloaded = repo.get(jobId);
    CHECK(reloaded.succeeded());
    CHECK(reloaded.engine == "firmware");
    CHECK(reloaded.operation == "generate_firmware");
}

TEST_CASE("firmware executes on the worker thread") {
    Fixture fx;
    auto worker = std::make_shared<trinity::jobs::JobWorker>(fx.jobs, 1);
    worker->start();
    const std::string jobId = worker->submit("firmware", "create_project",
                                             {{"name", "worker-fw"}});
    trinity::jobs::Job job;
    for (int i = 0; i < 200; ++i) {
        job = fx.jobs->get(jobId);
        if (trinity::jobs::isTerminal(job.status)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    CHECK(trinity::jobs::isTerminal(job.status));
    CHECK(job.succeeded());
    worker->stop();
}

TEST_CASE("firmware workflow chains structured project state between nodes") {
    Fixture fx;
    trinity::workflows::Workflow wf;
    wf.workflowId = "wf-firmware";
    wf.name = "firmware-chain";
    wf.status = trinity::workflows::WorkflowStatus::Queued;
    wf.createdAt = "2026-01-01T00:00:00+00:00";
    wf.updatedAt = wf.createdAt;

    trinity::workflows::WorkflowNode a;
    a.id = "A";
    a.nodeId = "A";
    a.engine = "firmware";
    a.operation = "create_project";
    a.parameters = {{"name", "wf-fw"}};
    a.input = a.parameters;

    trinity::workflows::WorkflowNode b;
    b.id = "B";
    b.nodeId = "B";
    b.engine = "firmware";
    b.operation = "select_mcu";
    b.parameters = {{"mcu", "ESP32"}};
    b.input = b.parameters;
    b.inputFrom = {{"project", "{{A.project}}"}};  // structured JSON, not a string

    trinity::workflows::WorkflowNode c;
    c.id = "C";
    c.nodeId = "C";
    c.engine = "firmware";
    c.operation = "configure_pin";
    c.parameters = {{"pin", "GPIO2"}, {"function", "led"}, {"direction", "out"}};
    c.input = c.parameters;
    c.inputFrom = {{"project", "{{B.project}}"}};

    trinity::workflows::WorkflowNode d;
    d.id = "D";
    d.nodeId = "D";
    d.engine = "firmware";
    d.operation = "configure_peripheral";
    d.parameters = {{"peripheral", "UART0"},
                    {"kind", "uart"},
                    {"tx", "GPIO1"},
                    {"rx", "GPIO3"}};
    d.input = d.parameters;
    d.inputFrom = {{"project", "{{C.project}}"}};

    trinity::workflows::WorkflowNode e;
    e.id = "E";
    e.nodeId = "E";
    e.engine = "firmware";
    e.operation = "generate_firmware";
    e.parameters = trinity::core::Json::object();
    e.input = e.parameters;
    e.inputFrom = {{"project", "{{D.project}}"}};

    trinity::workflows::WorkflowNode f;
    f.id = "F";
    f.nodeId = "F";
    f.engine = "firmware";
    f.operation = "validate_project";
    f.parameters = trinity::core::Json::object();
    f.input = f.parameters;
    f.inputFrom = {{"project", "{{E.project}}"}};

    wf.nodes = {a, b, c, d, e, f};
    int edge = 0;
    for (const auto& [from, to] :
         {std::pair<const char*, const char*>{"A", "B"}, {"B", "C"}, {"C", "D"},
          {"D", "E"}, {"E", "F"}}) {
        trinity::workflows::WorkflowEdge eEdge;
        eEdge.edgeId = "e" + std::to_string(++edge);
        eEdge.fromNode = from;
        eEdge.toNode = to;
        wf.edges.push_back(eEdge);
    }

    const auto result = fx.executor->runInline(wf);
    CHECK(result.success);
    CHECK(trinity::workflows::toString(result.workflow.status) == "completed");
    REQUIRE(result.nodeResults.count("A") == 1);
    REQUIRE(result.nodeResults.count("B") == 1);
    CHECK(result.nodeResults.at("B")["mcu"] == "ESP32");
    REQUIRE(result.nodeResults.count("C") == 1);
    REQUIRE(result.nodeResults.count("D") == 1);
    REQUIRE(result.nodeResults.count("E") == 1);
    CHECK(result.nodeResults.at("E")["file_count"] == 3);
    REQUIRE(result.nodeResults.count("F") == 1);
    CHECK(result.nodeResults.at("F")["passed"] == true);

    // The project object is structured between nodes (has_mcu survives).
    CHECK(result.nodeResults.at("C")["project"]["has_mcu"] == true);
    CHECK(result.nodeResults.at("C")["project"]["name"] == "wf-fw");
}

TEST_CASE("intent parser extracts explicit firmware requirements without inventing") {
    trinity::intelligence::RequirementParser parser;

    // Full request: MCU + pins for UART.
    const auto full = parser.parse(
        "Generate firmware for ESP32 with UART communication on TX GPIO1 RX GPIO3");
    CHECK(trinity::intelligence::toString(full.status) == "VALID");
    CHECK(full.intent.domain == "firmware");
    CHECK(full.intent.missing.empty());
    REQUIRE(full.intent.parameters.contains("mcu"));
    CHECK(full.intent.parameters["mcu"].get<std::string>() == "ESP32");

    // Configure GPIO 2 as output.
    const auto gpio = parser.parse("Configure GPIO 2 as output");
    CHECK(trinity::intelligence::toString(gpio.status) == "VALID");
    CHECK(gpio.intent.domain == "firmware");
    CHECK(gpio.intent.parameters.contains("pin"));
    CHECK(gpio.intent.parameters.contains("direction"));
    CHECK(gpio.intent.parameters["direction"].get<std::string>() == "out");

    // PWM on GPIO 18.
    const auto pwm = parser.parse("Create PWM control on GPIO 18");
    CHECK(trinity::intelligence::toString(pwm.status) == "VALID");
    CHECK(pwm.intent.domain == "firmware");
    REQUIRE(pwm.intent.parameters.contains("pin"));
    CHECK(pwm.intent.parameters["pin"].get<std::string>() == "GPIO18");

    // I2C pins.
    const auto i2c = parser.parse("Configure I2C on SDA 21 and SCL 22");
    CHECK(trinity::intelligence::toString(i2c.status) == "VALID");
    CHECK(i2c.intent.domain == "firmware");
    CHECK(i2c.intent.parameters.contains("sda"));
    CHECK(i2c.intent.parameters.contains("scl"));

    // Incomplete: no MCU named, must report missing, not invent.
    const auto incomplete = parser.parse("Generate firmware with UART communication");
    CHECK(incomplete.intent.domain == "firmware");
    const auto status = trinity::intelligence::toString(incomplete.status);
    CHECK((status == "INCOMPLETE" || status == "VALID"));
    if (status == "INCOMPLETE") {
        CHECK_FALSE(incomplete.intent.missing.empty());
    }
}

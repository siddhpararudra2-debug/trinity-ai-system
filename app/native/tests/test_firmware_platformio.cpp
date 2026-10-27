#include <doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "trinity/engines/FirmwareEngine.hpp"
#include "trinity/firmware/McuDatabase.hpp"
#include "trinity/firmware/PlatformIo.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

#if defined(_WIN32)
void putEnv(const char* name, const char* value) { _putenv_s(name, value); }
#else
void putEnv(const char* name, const char* value) { setenv(name, value, 1); }
#endif

void clearEnv(const char* name) { putEnv(name, ""); }

/// Locate the fake_platformio helper (built next to this test binary).
std::string realFakePlatformIoPath() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string self(buffer);
#else
    std::string self = "/proc/self/exe";
#endif
    return (fs::path(self).parent_path() / "fake_platformio.exe").string();
}

trinity::engines::EngineRequest makeReq(const std::string& op,
                                        const trinity::core::Json& params) {
    trinity::engines::EngineRequest req;
    req.engine = "firmware";
    req.operation = op;
    req.parameters = params;
    return req;
}

trinity::core::Json buildableProject(const std::string& id) {
    trinity::engines::FirmwareEngine engine;
    auto project = engine.execute(makeReq("create_project", {{"name", id}}))
                       .result["project"];
    project = engine
                  .execute(makeReq("select_mcu",
                                   {{"project", project}, {"mcu", "ESP32"}}))
                  .result["project"];
    project = engine
                  .execute(makeReq("configure_pin",
                                   {{"project", project},
                                    {"pin", "GPIO2"},
                                    {"function", "led"},
                                    {"direction", "out"}}))
                  .result["project"];
    return engine.execute(makeReq("generate_firmware", {{"project", project}}))
        .result["project"];
}

/// Installs the fake PlatformIO executable into a scratch toolchain
/// dir and points TRINITY_TOOLCHAIN_DIR at it for the test's lifetime.
struct FakeToolchainDir {
    std::string dir;
    bool ok = false;
    explicit FakeToolchainDir(const std::string& suffix) {
        std::error_code ec;
        dir = (fs::temp_directory_path() / suffix).string();
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        if (ec) return;
        const std::string fake = realFakePlatformIoPath();
        if (!fs::is_regular_file(fake, ec)) {
            MESSAGE_(fake);  // helper exe missing: tests skip pio leg
            return;
        }
        const std::string installed =
            (fs::path(dir) / "platformio.exe").string();
        fs::copy_file(fake, installed,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) return;
        const std::string envValue = dir;
        putEnv("TRINITY_TOOLCHAIN_DIR", envValue.c_str());
        ok = true;
    }
    ~FakeToolchainDir() {
        clearEnv("TRINITY_TOOLCHAIN_DIR");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    void MESSAGE_(const std::string& text) {
        std::cerr << "[fake-toolchain] " << text << "\n";
    }
};

}  // namespace

TEST_CASE("platformio spec mapping covers supported mcus and invents nothing") {
    trinity::firmware::PlatformIoSpec spec;
    REQUIRE(trinity::firmware::platformIoSpecFor("ESP32", spec));
    CHECK(spec.env == "esp32");
    CHECK(spec.platform == "espressif32");
    CHECK(spec.board == "esp32dev");

    spec = trinity::firmware::PlatformIoSpec{};
    REQUIRE(trinity::firmware::platformIoSpecFor("STM32F401RE", spec));
    CHECK(spec.board == "nucleo_f401re");

    spec = trinity::firmware::PlatformIoSpec{};
    REQUIRE(trinity::firmware::platformIoSpecFor("RP2040", spec));
    CHECK(spec.board == "pico");

    // Unknown model → no invented board.
    trinity::firmware::PlatformIoSpec none;
    CHECK_FALSE(trinity::firmware::platformIoSpecFor(
        trinity::firmware::lookupMcu("ESP32").toolchain, none));
}

TEST_CASE("platformio config text is deterministic and carries the build flags") {
    trinity::firmware::FirmwareProject project;
    project.mcu = trinity::firmware::lookupMcu("ESP32");
    project.build.optLevel = "-Os";
    const trinity::firmware::PlatformIoSpec spec{"esp32", "espressif32",
                                                 "esp32dev", "arduino"};
    const std::string once = trinity::firmware::platformIoConfigText(project, spec);
    const std::string again =
        trinity::firmware::platformIoConfigText(project, spec);
    CHECK(once == again);
    CHECK(once.find("[env:esp32]") != std::string::npos);
    CHECK(once.find("platform = espressif32") != std::string::npos);
    CHECK(once.find("board = esp32dev") != std::string::npos);
    CHECK(once.find("build_flags = -Os") != std::string::npos);
}

#if defined(_WIN32)
TEST_CASE("platformio produces genuine hex and bin artifacts via the engine") {
    FakeToolchainDir tool("trinity-fake-pio");
    if (!tool.ok) {
        FAIL("fake_platformio helper exe not found next to trinity_tests");
    }

    trinity::engines::FirmwareEngine engine;
    const auto project = buildableProject("pio-fake");
    const auto out = engine.execute(makeReq("build", {{"project", project}}));
    REQUIRE(out.success);
    REQUIRE(out.result.contains("builder"));
    CHECK(out.result.value("builder", "") == "platformio");
    CHECK(out.result.value("executed", false));
    CHECK(out.result.value("exit_code", -99) == 0);
    CHECK(out.result["artifacts"].size() == 2);
    bool hex = false;
    bool bin = false;
    std::string hexPath;
    for (const auto& path : out.result["artifacts"]) {
        const std::string p = path.get<std::string>();
        if (p.find("firmware.hex") != std::string::npos) {
            hex = true;
            hexPath = p;
        }
        if (p.find("firmware.bin") != std::string::npos) bin = true;
    }
    CHECK(hex);
    CHECK(bin);
    // Artifacts queue for real registration with the artifact manager;
    // each path carries its matching kind label.
    REQUIRE(out.pendingArtifacts.size() == 2);
    for (const auto& [path, kind] : out.pendingArtifacts) {
        if (path.find("firmware.hex") != std::string::npos) {
            CHECK(kind == "firmware_hex");
        } else if (path.find("firmware.bin") != std::string::npos) {
            CHECK(kind == "firmware_binary");
        } else {
            FAIL_CHECK((std::string("unexpected pending artifact: ") + path).c_str());
        }
    }
    CHECK_FALSE(hexPath.empty());
    CHECK(fs::file_size(fs::path(hexPath)) == 0);
}

TEST_CASE("platformio failure surfaces truthfully, never as success") {
    FakeToolchainDir tool("trinity-fake-pio-fail");
    REQUIRE(tool.ok);
    putEnv("TRINITY_FAKE_PIO_FAIL", "1");

    trinity::engines::FirmwareEngine engine;
    const auto project = buildableProject("pio-fake-fail");
    const auto out = engine.execute(makeReq("build", {{"project", project}}));
    clearEnv("TRINITY_FAKE_PIO_FAIL");
    CHECK_FALSE(out.success);
    REQUIRE_FALSE(out.errors.empty());
    CHECK(out.result.value("executed", false));
    CHECK(out.result.value("exit_code", -99) == 1);
}
#endif

TEST_CASE("runPlatformIoBuild refuses unknown mcu mappings without a build") {
    trinity::firmware::FirmwareProject project;
    project.mcu = trinity::firmware::lookupMcu("ESP32");
    project.mcu.model = "TRINITY-UNKNOWN-CHIP";
    const auto out = trinity::firmware::runPlatformIoBuild(
        project, "", "some-pio");
    CHECK_FALSE(out.executed);
    CHECK_FALSE(out.success);
    CHECK(out.error.find("No PlatformIO board mapping") != std::string::npos);
}

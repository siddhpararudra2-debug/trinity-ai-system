// Stand-in `platformio.exe` for tests: stands on the CreateProcess
// seam exactly like the real PlatformIO CLI does — invoked with
// explicit argv, working directory set to the project, stdout/stderr
// captured. Emits real files under .pio\build\<env>\ so callers can
// verify artifact collection.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    // Test switch: nonzero value makes the fake exit 1 (compile error path).
    if (const char* fail = std::getenv("TRINITY_FAKE_PIO_FAIL");
        fail != nullptr && *fail != '\0' && *fail != '0') {
        std::fprintf(stderr, "[fake] compile error near main.cpp:1\n");
        return 1;
    }
    std::error_code ec;
    const std::filesystem::path buildDir =
        std::filesystem::path(".pio") / "build" / "esp32";
    std::filesystem::create_directories(buildDir, ec);
    if (ec) {
        std::fprintf(stderr, "fake platformio: cannot create build dir\n");
        return 2;
    }
    { std::ofstream hex(buildDir / "firmware.hex", std::ios::trunc); }
    { std::ofstream bin(buildDir / "firmware.bin", std::ios::trunc);
      bin << "fake-esp32-image\n"; }
    std::printf("fake pio run: processed platformio.ini\n");
    return 0;
}

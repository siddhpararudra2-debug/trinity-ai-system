// Trinity — platform + storage tests.
//
// The platform environment is injected, so the whole data layout can be
// exercised without touching the real process environment.
#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "../core/FileSystem.hpp"
#include "../core/Uuid.hpp"
#include "../platform/Platform.hpp"
#include "../storage/Storage.hpp"

using namespace trinity;

namespace {

class FakeEnvironment : public platform::IEnvironment {
public:
    std::map<std::string, std::string> values;
    std::string home;

    std::optional<std::string> get(const std::string& name) const override {
        const auto it = values.find(name);
        if (it == values.end()) return std::nullopt;
        return it->second;
    }

    std::string home_directory() const override { return home; }
};

std::string fresh_temp_dir(const std::string& label) {
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        ("trinity_" + label + "_" + core::new_uuid().substr(0, 8));
    std::error_code ec;
    core::FileSystem::ensure_directory(base.string(), ec);
    return base.string();
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

TEST_CASE("StorageLayout uses explicit overrides verbatim") {
    FakeEnvironment environment;
    environment.values["LOCALAPPDATA"] = "ignored/local";
    environment.home = "ignored/home";

    const storage::StorageLayout layout = storage::StorageLayout::resolve_with_overrides(
        environment, "/custom/data", "/custom/projects", std::string());
    CHECK(layout.data_dir == "/custom/data");
    CHECK(layout.projects_dir.find("custom/projects") != std::string::npos);
    CHECK(layout.database_file.find("/custom/data") != std::string::npos);
    CHECK(layout.artifacts_dir.find("artifacts") != std::string::npos);
    CHECK(layout.log_file.find("trinity.log") != std::string::npos);
}

TEST_CASE("StorageLayout resolves platform defaults from the environment") {
    FakeEnvironment environment;
    environment.values["LOCALAPPDATA"] = "C:/Users/tester/AppData/Local";
    environment.values["USERPROFILE"] = "C:/Users/tester";
    environment.home = "C:/Users/tester";

    const storage::StorageLayout layout = storage::StorageLayout::resolve(environment);
    CHECK(ends_with(layout.data_dir, "Trinity"));
    CHECK(layout.data_dir.find("AppData") != std::string::npos);
    CHECK(ends_with(layout.projects_dir, "Trinity Projects"));

    // TRINITY_DATA_DIR wins over the platform convention.
    environment.values["TRINITY_DATA_DIR"] = "D:/portable/trinity";
    const storage::StorageLayout portable = storage::StorageLayout::resolve(environment);
    CHECK(portable.data_dir == "D:/portable/trinity");
}

TEST_CASE("StorageLayout creates every directory it names") {
    const std::string root = fresh_temp_dir("layout");
    FakeEnvironment environment;
    const storage::StorageLayout layout = storage::StorageLayout::resolve_with_overrides(
        environment, root, root + "/projects", std::string());

    std::error_code ec;
    REQUIRE(layout.ensure_directories(ec));
    CHECK(!ec);
    CHECK(std::filesystem::exists(layout.artifacts_dir));
    CHECK(std::filesystem::exists(layout.logs_dir));
    CHECK(std::filesystem::exists(layout.temp_dir));
    CHECK(std::filesystem::exists(layout.projects_dir));

    core::FileSystem::remove_all(root, ec);
}

TEST_CASE("LocalFileStorage refuses to escape its root") {
    const std::string root = fresh_temp_dir("storage_escape");
    storage::LocalFileStorage storage(root);

    CHECK(storage.absolute("../escape.txt").empty());
    CHECK(storage.absolute("nested/../../escape.txt").empty());
    CHECK_FALSE(storage.exists("../trinity.db"));

    std::error_code ec;
    CHECK_FALSE(storage.write_text_atomic("../escape.txt", "nope", ec));
    CHECK(ec);

    CHECK_FALSE(storage.absolute("inside/ok.txt").empty());

    std::error_code cleanup;
    core::FileSystem::remove_all(root, cleanup);
}

TEST_CASE("LocalFileStorage round trips text, listings and sizes") {
    const std::string root = fresh_temp_dir("storage_roundtrip");
    storage::LocalFileStorage storage(root);

    std::error_code ec;
    REQUIRE(storage.write_text_atomic("notes/first.txt", "hello trinity", ec));
    CHECK(!ec);

    auto contents = storage.read_text("notes/first.txt");
    REQUIRE(contents.has_value());
    CHECK(*contents == "hello trinity");

    const std::vector<std::string> files = storage.list_files("notes");
    REQUIRE(files.size() == 1);
    CHECK(files.at(0) == "first.txt");
    CHECK(storage.size("notes/first.txt") == 13);
    CHECK(storage.exists("notes"));

    REQUIRE(storage.ensure_directory("nested/child", ec));
    const std::vector<std::string> directories = storage.list_directories("nested");
    REQUIRE(directories.size() == 1);
    CHECK(directories.at(0) == "child");

    CHECK(storage.remove_all("notes", ec));
    CHECK_FALSE(storage.exists("notes/first.txt"));

    std::error_code cleanup;
    core::FileSystem::remove_all(root, cleanup);
}

TEST_CASE("PlatformInfo describes the running platform") {
    FakeEnvironment environment;
    environment.values["LOCALAPPDATA"] = "C:/Users/tester/AppData/Local";
    environment.home = "C:/Users/tester";

    const platform::PlatformInfo info = platform::describe_platform(environment);
    CHECK_FALSE(info.os.empty());
    CHECK((info.pointer_bits == 32 || info.pointer_bits == 64));
    CHECK(info.cpu_count >= 1);
    CHECK(ends_with(info.config_dir, "Trinity"));

    const core::Json document = info.to_json();
    CHECK(document.find("os") != nullptr);
    CHECK(document.find("cpu_count") != nullptr);
    CHECK(platform::hardware_concurrency() >= 1);
}

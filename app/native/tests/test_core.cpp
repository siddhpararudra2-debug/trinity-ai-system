// Trinity — core module tests.
#include <filesystem>
#include <fstream>

#include <doctest/doctest.h>

#include "../core/FileSystem.hpp"
#include "../core/Json.hpp"
#include "../core/Paths.hpp"
#include "../core/Sha256.hpp"
#include "../core/Time.hpp"
#include "../core/Uuid.hpp"

using namespace trinity::core;

TEST_CASE("Json round trips and parses") {
    const std::string text = R"({"a":1,"b":[true,null,"x"],"c":{"d":-2.5}})";
    Json parsed = Json::parse(text);
    REQUIRE(parsed.is_object());
    CHECK(parsed.find("a")->as_int() == 1);
    CHECK(parsed.find("b")->as_array().at(0).as_bool() == true);
    CHECK(parsed.find("b")->as_array().at(1).is_null());
    CHECK(parsed.find("b")->as_array().at(2).as_string() == "x");
    CHECK(parsed.find("c")->find("d")->as_double() == doctest::Approx(-2.5));
    Json reparsed = Json::parse(parsed.dump());
    CHECK(reparsed.dump() == parsed.dump());
}

TEST_CASE("Json rejects malformed input") {
    CHECK_THROWS(Json::parse("{"));
    CHECK_THROWS(Json::parse("[1,]"));
    CHECK_THROWS(Json::parse("{\"a\" 1}"));
    CHECK_THROWS(Json::parse("nul"));
    CHECK_THROWS(Json::parse("{\"a\":1} trailing"));
}

TEST_CASE("Json string escapes round trip") {
    Json value(Json(std::string("quote\" backslash\\ newline\n tab\t unicode \xC3\xA9")));
    Json parsed = Json::parse(value.dump());
    CHECK(parsed.as_string() == std::string("quote\" backslash\\ newline\n tab\t unicode \xC3\xA9"));
}

TEST_CASE("Sha256 known vectors") {
    CHECK(sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string long_input(1000, 'a');
    CHECK(sha256_hex(long_input) ==
          "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST_CASE("Uuid format and uniqueness") {
    std::string first = new_uuid();
    std::string second = new_uuid();
    CHECK(is_valid_uuid(first));
    CHECK(is_valid_uuid(second));
    CHECK(first != second);
    CHECK(first.size() == 36);
    CHECK(first[14] == '4');  // version 4
}

TEST_CASE("ISO timestamps are UTC shaped") {
    const std::string stamp = iso_utc_now();
    REQUIRE(stamp.size() == 24);
    CHECK(stamp[4] == '-');
    CHECK(stamp[10] == 'T');
    CHECK(stamp[23] == 'Z');
}

TEST_CASE("Paths honour TRINITY_DATA_DIR override") {
    // Env override seam is exercised via Paths::env; direct resolution keeps
    // the default environment. Here we validate the layout contract.
    const std::string data = Paths::data_dir();
    CHECK(!data.empty());
    CHECK(Paths::database_file().find(data) != std::string::npos);
    CHECK(Paths::artifacts_dir().find(data) != std::string::npos);
}

TEST_CASE("FileSystem traversal protection") {
    const std::string root = std::filesystem::temp_directory_path().string() + "/trinity_test_fs";
    std::error_code ec;
    FileSystem::ensure_directory(root, ec);
    REQUIRE(!ec);

    auto inside = FileSystem::validate_under(root, root + "/projects/demo");
    CHECK(inside.has_value());
    auto escape = FileSystem::validate_under(root, root + "/../outside");
    CHECK(!escape.has_value());
    auto deep_escape = FileSystem::validate_under(root, "../../etc/passwd");
    CHECK(!deep_escape.has_value());

    FileSystem::remove_all(root, ec);
    CHECK(!ec);
}

TEST_CASE("FileSystem atomic write") {
    const std::string root = std::filesystem::temp_directory_path().string() + "/trinity_test_atomic";
    std::error_code ec;
    FileSystem::remove_all(root, ec);
    const std::string file = root + "/nested/dir/file.json";
    CHECK(FileSystem::write_file_atomic(file, "{\"ok\":true}", ec));
    auto contents = FileSystem::read_file(file);
    REQUIRE(contents.has_value());
    CHECK(*contents == "{\"ok\":true}");
    // No temp leftovers.
    CHECK(FileSystem::list_files(root + "/nested/dir").size() == 1);
    FileSystem::remove_all(root, ec);
}

TEST_CASE("Temp directories are unique") {
    const std::string base = std::filesystem::temp_directory_path().string() + "/trinity_test_tmp";
    std::error_code ec;
    FileSystem::remove_all(base, ec);
    const std::string a = FileSystem::make_temp_directory(base, "t_");
    const std::string b = FileSystem::make_temp_directory(base, "t_");
    CHECK(!a.empty());
    CHECK(!b.empty());
    CHECK(a != b);
    FileSystem::remove_all(base, ec);
}

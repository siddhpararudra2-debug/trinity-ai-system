#include <doctest.h>

#include <filesystem>

#include "trinity/fs/Filesystem.hpp"

TEST_CASE("filesystem read/write/metadata/dirs") {
    trinity::fs::Filesystem fs;
    const auto root =
        (std::filesystem::temp_directory_path() / "trinity-test-fs").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    CHECK(fs.createDirs(root + "/a/b").isOk());
    CHECK(fs.exists(root + "/a/b"));
    CHECK(fs.isDirectory(root + "/a/b"));
    CHECK_FALSE(fs.isFile(root + "/a/b"));

    const std::string file = root + "/a/b/data.txt";
    CHECK(fs.writeFile(file, "hello").isOk());
    CHECK(fs.isFile(file));
    const auto read = fs.readFile(file);
    REQUIRE(read.isOk());
    CHECK(read.value() == "hello");

    const auto meta = fs.metadata(file);
    CHECK(meta.exists);
    CHECK(meta.isFile);
    CHECK(meta.sizeBytes == 5);

    const auto listed = fs.listDir(root + "/a/b");
    REQUIRE(listed.isOk());
    CHECK(listed.value().size() == 1);

    CHECK(fs.copyFile(file, root + "/a/copy.txt").isOk());
    CHECK(fs.exists(root + "/a/copy.txt"));
    CHECK(fs.remove(file).isOk());
    CHECK_FALSE(fs.exists(file));
    CHECK(fs.removeAll(root).isOk());
}

TEST_CASE("filesystem safeJoin rejects traversal and absolute paths") {
    trinity::fs::Filesystem fs;
    const auto ok = fs.safeJoin("/tmp/root", "sub/dir.txt");
    CHECK(ok.isOk());
    CHECK_FALSE(fs.safeJoin("/tmp/root", "../escape.txt").isOk());
    CHECK_FALSE(fs.safeJoin("/tmp/root", "/absolute.txt").isOk());
    const auto nested = fs.safeJoin("/tmp/root", "a/b.txt");
    REQUIRE(nested.isOk());
    CHECK(nested.value().find("a") != std::string::npos);
}

TEST_CASE("filesystem read of missing file fails with structured error") {
    trinity::fs::Filesystem fs;
    const auto missing = fs.readFile("/definitely/not/here-trinity.txt");
    CHECK_FALSE(missing.isOk());
    CHECK_FALSE(missing.error().message.empty());
}

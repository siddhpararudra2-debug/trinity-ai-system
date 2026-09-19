#include "trinity/artifacts/Artifact.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "trinity/core/Error.hpp"

#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>

// Fallback if the SDK in use does not expose the algorithm-name macro.
#ifndef BCRYPT_SHA256_ALGORITHM_NAME
#define BCRYPT_SHA256_ALGORITHM_NAME L"SHA256"
#endif
#endif

#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::artifacts {
namespace fs = std::filesystem;

namespace {

// SQL string escaping for the internal query helper: single quotes double.
std::string sqlQuote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out += '\'';
    for (char c : value) {
        if (c == '\'') {
            out += '\'';
        }
        out += c;
    }
    out += '\'';
    return out;
}

std::string sha256File(const std::string& path) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM_NAME, nullptr, 0) != 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
                          sizeof(hashLength), &resultLength, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptGetProperty failed");
    }
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptCreateHash failed");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Cannot open file for checksum: " + path);
    }
    std::vector<unsigned char> buffer(1 << 20);
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0 &&
            BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) != 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCryptHashData failed");
        }
    }
    std::vector<unsigned char> digest(hashLength);
    if (BCryptFinishHash(hash, digest.data(), hashLength, 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptFinishHash failed");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream out;
    out << std::hex;
    for (unsigned char byte : digest) {
        out.width(2);
        out.fill('0');
        out << static_cast<int>(byte);
    }
    return out.str();
#else
    throw std::runtime_error("SHA-256 checksum is only implemented on Windows: " + path);
#endif
}

}  // namespace

core::Json Artifact::toJson() const {
    return core::Json{{"artifact_id", artifactId},
                      {"job_id", jobId},
                      {"type", type},
                      {"path", path},
                      {"size_bytes", sizeBytes},
                      {"checksum", checksum},
                      {"created_at", createdAt}};
}

ArtifactManager::ArtifactManager(std::shared_ptr<storage::Database> db,
                                 std::string artifactsDir)
    : db_(std::move(db)), artifactsDir_(std::move(artifactsDir)) {}

Artifact ArtifactManager::storeFile(const std::string& srcPath,
                                    const std::string& artifactType,
                                    const std::string& jobId) {
    const fs::path src(srcPath);
    if (!fs::is_regular_file(src)) {
        throw core::ArtifactNotFoundError("Source file does not exist: " + srcPath);
    }
    const std::string artifactId = core::newUuid();
    const fs::path destDir = fs::path(artifactsDir_) / artifactId;
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        throw std::runtime_error("Cannot create artifact directory: " + ec.message());
    }
    const fs::path dest = destDir / src.filename();
    fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        throw std::runtime_error("Cannot store artifact file: " + ec.message());
    }

    Artifact artifact;
    artifact.artifactId = artifactId;
    artifact.jobId = jobId;
    artifact.type = artifactType;
    artifact.path = dest.string();
    artifact.sizeBytes = static_cast<long long>(fs::file_size(dest, ec));
    artifact.checksum = sha256File(dest.string());
    artifact.createdAt = core::utcNowIso();

    db_->exec("INSERT INTO artifacts (artifact_id, job_id, type, path, size_bytes, "
              "checksum, created_at) VALUES (" +
              sqlQuote(artifact.artifactId) + ", " + sqlQuote(artifact.jobId) + ", " +
              sqlQuote(artifact.type) + ", " + sqlQuote(artifact.path) + ", " +
              std::to_string(artifact.sizeBytes) + ", " + sqlQuote(artifact.checksum) +
              ", " + sqlQuote(artifact.createdAt) + ");");
    return artifact;
}

Artifact ArtifactManager::get(const std::string& artifactId) const {
    const auto rows =
        db_->query("SELECT artifact_id, job_id, type, path, size_bytes, checksum, "
                   "created_at FROM artifacts WHERE artifact_id = " +
                   sqlQuote(artifactId) + ";");
    if (rows.empty() || rows[0].size() < 7) {
        throw core::ArtifactNotFoundError("No artifact with id '" + artifactId + "'");
    }
    Artifact artifact;
    artifact.artifactId = rows[0][0];
    artifact.jobId = rows[0][1];
    artifact.type = rows[0][2];
    artifact.path = rows[0][3];
    artifact.sizeBytes = std::stoll(rows[0][4].empty() ? "0" : rows[0][4]);
    artifact.checksum = rows[0][5];
    artifact.createdAt = rows[0][6];
    return artifact;
}

}  // namespace trinity::artifacts

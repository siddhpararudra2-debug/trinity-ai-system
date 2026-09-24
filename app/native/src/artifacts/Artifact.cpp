#include "trinity/artifacts/Artifact.hpp"

#include <filesystem>
#include <stdexcept>

#include "trinity/core/Error.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/artifacts/Checksum.hpp"

namespace trinity::artifacts {
namespace fs = std::filesystem;

core::Json Artifact::toJson() const {
    return core::Json{{"artifact_id", artifactId},
                      {"job_id", jobId},
                      {"workflow_id", workflowId},
                      {"type", type},
                      {"path", path},
                      {"size_bytes", sizeBytes},
                      {"checksum", checksum},
                      {"created_at", createdAt}};
}

Artifact Artifact::fromJson(const core::Json& json) {
    Artifact artifact;
    artifact.artifactId = json.value("artifact_id", "");
    artifact.jobId = json.value("job_id", "");
    artifact.workflowId = json.value("workflow_id", "");
    artifact.type = json.value("type", "");
    artifact.path = json.value("path", "");
    artifact.sizeBytes = json.value("size_bytes", 0LL);
    artifact.checksum = json.value("checksum", "");
    artifact.createdAt = json.value("created_at", "");
    return artifact;
}

std::string toString(ArtifactType type) {
    switch (type) {
        case ArtifactType::Mesh:
            return "mesh";
        case ArtifactType::Report:
            return "report";
        case ArtifactType::Image:
            return "image";
        case ArtifactType::Data:
            return "data";
        case ArtifactType::Binary:
            return "binary";
        case ArtifactType::Unknown:
        default:
            return "unknown";
    }
}

ArtifactType artifactTypeFromString(const std::string& type) {
    if (type == "mesh") return ArtifactType::Mesh;
    if (type == "report") return ArtifactType::Report;
    if (type == "image") return ArtifactType::Image;
    if (type == "data") return ArtifactType::Data;
    if (type == "binary") return ArtifactType::Binary;
    return ArtifactType::Unknown;
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

    db_->execute(
        "INSERT INTO artifacts (artifact_id, job_id, type, path, size_bytes, "
        "checksum, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
        {artifact.artifactId, artifact.jobId, artifact.type, artifact.path,
         static_cast<std::int64_t>(artifact.sizeBytes), artifact.checksum,
         artifact.createdAt});
    return artifact;
}

Artifact ArtifactManager::get(const std::string& artifactId) const {
    const auto rows = db_->queryParams(
        "SELECT artifact_id, job_id, type, path, size_bytes, checksum, "
        "created_at FROM artifacts WHERE artifact_id = ?;",
        {artifactId});
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

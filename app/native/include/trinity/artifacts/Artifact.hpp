#pragma once

// Artifact model and manager. Mirrors src/artifacts/manager.py: large
// files live on the filesystem; only metadata goes into SQLite. This
// manager is the only writer into storage/artifacts/.

#include <memory>
#include <string>

#include "../core/Json.hpp"
#include "../storage/Database.hpp"

namespace trinity::artifacts {

struct Artifact {
    std::string artifactId;
    std::string jobId;
    std::string type;
    std::string path;
    long long sizeBytes = 0;
    std::string checksum;
    std::string createdAt;

    core::Json toJson() const;
};

class ArtifactManager {
public:
    ArtifactManager(std::shared_ptr<storage::Database> db, std::string artifactsDir);

    // Copy srcPath into managed storage, checksum it (SHA-256), record
    // metadata, and return the stored artifact.
    Artifact storeFile(const std::string& srcPath, const std::string& artifactType,
                       const std::string& jobId);

    Artifact get(const std::string& artifactId) const;

private:
    std::shared_ptr<storage::Database> db_;
    std::string artifactsDir_;
};

}  // namespace trinity::artifacts

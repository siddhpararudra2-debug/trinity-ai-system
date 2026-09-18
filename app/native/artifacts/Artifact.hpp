// Trinity — Artifacts are first-class objects with full metadata and
// validation state. The ArtifactStore is the ONLY writer to the artifacts
// tree (single-writer rule carried over from the V1 Python backend).
//
//   GENERATED  ->  VALIDATED  ->  VERIFIED
//
// Never claim VERIFIED just because a file exists; verification requires
// engine-specific checks recorded in the validations table.
#pragma once

#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "../db/Database.hpp"

namespace trinity::artifacts {

enum class ValidationState { Generated, Validated, Verified, Failed };

const char* validation_state_string(ValidationState state);
core::Result<ValidationState> validation_state_from_string(const std::string& text);

// Known artifact types (brief §Artifact System). Unknown types are rejected
// on store() — the list is data, not hardcode.
struct ArtifactType {
    const char* type;
    const char* extension;
};
extern const ArtifactType kKnownTypes[];
extern const int kKnownTypeCount;
bool is_known_type(const std::string& type);
std::string extension_for_type(const std::string& type);

struct Artifact {
    std::string artifact_id;
    std::string project_id;
    std::string job_id;
    std::string type;
    std::string filename;
    std::string path;
    std::uint64_t size_bytes = 0;
    std::string hash_sha256;
    std::string engine;
    std::string engine_version;
    ValidationState validation_state = ValidationState::Generated;
    std::string created_at;

    core::Json to_json() const;
};

// Canonical artifact-store interface (brief §Core interfaces: IArtifactStore).
// The single-writer contract lives here: nothing else may insert artifact rows
// or move files into the artifacts tree.
class IArtifactStore {
public:
    virtual ~IArtifactStore() = default;

    virtual core::Result<Artifact> store_file(const std::string& source_path,
                                              const std::string& type,
                                              const std::string& project_id = "",
                                              const std::string& job_id = "",
                                              const std::string& engine = "",
                                              const std::string& engine_version = "") = 0;

    virtual core::Result<Artifact> get(const std::string& artifact_id) const = 0;
    virtual core::Result<std::vector<Artifact>> list_for_project(
        const std::string& project_id) const = 0;
    virtual core::Result<std::vector<Artifact>> list_for_job(const std::string& job_id) const = 0;
    virtual core::Status set_validation_state(const std::string& artifact_id,
                                              ValidationState state) = 0;
    virtual core::Result<bool> verify_integrity(const std::string& artifact_id) const = 0;
    virtual core::Status remove(const std::string& artifact_id) = 0;
};

class ArtifactStore : public IArtifactStore {
public:
    explicit ArtifactStore(db::Database& db, std::string artifacts_root);

    // Moves a produced file into permanent storage under
    //   <artifacts_root>/<artifact_id>/<filename>
    // computes the sha-256, and inserts the metadata row. Validation state
    // starts at GENERATED.
    core::Result<Artifact> store_file(const std::string& source_path, const std::string& type,
                                      const std::string& project_id = "",
                                      const std::string& job_id = "",
                                      const std::string& engine = "",
                                      const std::string& engine_version = "") override;

    core::Result<Artifact> get(const std::string& artifact_id) const override;
    core::Result<std::vector<Artifact>> list_for_project(const std::string& project_id) const override;
    core::Result<std::vector<Artifact>> list_for_job(const std::string& job_id) const override;

    // Transition the validation state. Forward-only with one exception:
    // VERIFIED -> FAILED requires an explicit re-validation record (handled by
    // the ValidationEngine, not here).
    core::Status set_validation_state(const std::string& artifact_id, ValidationState state) override;

    // Re-verify integrity: recompute the sha-256 and compare with metadata.
    core::Result<bool> verify_integrity(const std::string& artifact_id) const override;

    core::Status remove(const std::string& artifact_id) override;

private:
    core::Result<Artifact> row_to_artifact(const core::JsonObject& row) const;

    db::Database* db_;
    std::string artifacts_root_;
};

}  // namespace trinity::artifacts

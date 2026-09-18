// Trinity — validation lifecycle enforcement (brief §Validation).
//
//   GENERATED -> VALIDATED -> VERIFIED      (and any -> FAILED on evidence)
//
// Transitions are guarded: an artifact may never claim VERIFIED without
// engine-specific evidence recorded in the validations table. This is the
// core honesty invariant of the Trinity verification model.
#pragma once

#include <string>
#include <vector>

#include "../artifacts/Artifact.hpp"
#include "../core/Json.hpp"
#include "../db/Database.hpp"
#include "../engines/Engine.hpp"

namespace trinity::validation {

struct ValidationRecord {
    std::string validation_id;
    std::string artifact_id;
    std::string job_id;
    std::string engine;
    std::string status;      // GENERATED | VALIDATED | VERIFIED | FAILED
    core::Json checks = core::Json::object();
    std::string created_at;
};

// Canonical validator interface (brief §Core interfaces: IValidator).
class IValidator {
public:
    virtual ~IValidator() = default;

    virtual core::Result<artifacts::ValidationState> apply_checks(
        const std::string& artifact_id, const std::string& engine, const core::Json& checks,
        bool checks_passed, bool verified) = 0;
    virtual core::Status record(const std::string& artifact_id, const std::string& job_id,
                               const std::string& engine, const std::string& status,
                               const core::Json& checks) = 0;
    virtual core::Result<std::vector<ValidationRecord>> history_for_artifact(
        const std::string& artifact_id) const = 0;
};

class ValidationEngine : public IValidator {
public:
    ValidationEngine(db::Database& db, artifacts::ArtifactStore& artifacts);

    // Applies engine-specific checks to an artifact and records the evidence.
    // allowed transitions: GENERATED -> VALIDATED (checks pass)
    //                      VALIDATED -> VERIFIED (checks pass, engine says verified)
    //                      any -> FAILED (checks fail)
    // Returns the resulting state; fails with request_validation_error when
    // the transition itself is illegal (e.g. GENERATED -> VERIFIED directly).
    core::Result<artifacts::ValidationState> apply_checks(
        const std::string& artifact_id, const std::string& engine,
        const core::Json& checks, bool checks_passed, bool verified) override;

    // Records a raw validation row (used by job completion paths).
    core::Status record(const std::string& artifact_id, const std::string& job_id,
                        const std::string& engine, const std::string& status,
                        const core::Json& checks) override;

    core::Result<std::vector<ValidationRecord>> history_for_artifact(
        const std::string& artifact_id) const override;

private:
    db::Database* db_;
    artifacts::ArtifactStore* artifacts_;
};

}  // namespace trinity::validation

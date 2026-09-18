#include "ValidationEngine.hpp"

#include "../core/Time.hpp"
#include "../core/Uuid.hpp"

namespace trinity::validation {
namespace {

bool transition_allowed(artifacts::ValidationState from, artifacts::ValidationState to) {
    using VS = artifacts::ValidationState;
    if (from == to) return false;
    if (to == VS::Failed) return true;                 // any -> FAILED with evidence
    if (from == VS::Generated && to == VS::Validated) return true;
    if (from == VS::Validated && to == VS::Verified) return true;
    return false;
}

}  // namespace

ValidationEngine::ValidationEngine(db::Database& db, artifacts::ArtifactStore& artifacts)
    : db_(&db), artifacts_(&artifacts) {}

core::Result<artifacts::ValidationState> ValidationEngine::apply_checks(
    const std::string& artifact_id, const std::string& engine, const core::Json& checks,
    bool checks_passed, bool verified) {
    auto artifact = artifacts_->get(artifact_id);
    if (artifact.is_error()) return core::Result<artifacts::ValidationState>::fail(artifact.take_error());

    artifacts::ValidationState target = checks_passed
                                            ? (verified ? artifacts::ValidationState::Verified
                                                        : artifacts::ValidationState::Validated)
                                            : artifacts::ValidationState::Failed;

    if (!transition_allowed(artifact.value().validation_state, target)) {
        core::Json details = core::Json::object();
        details["from"] = artifacts::validation_state_string(artifact.value().validation_state);
        details["to"] = artifacts::validation_state_string(target);
        return core::Result<artifacts::ValidationState>::fail(core::Error(
            core::ErrorCode::RequestValidationError,
            "illegal validation transition: verification requires evidence at every stage",
            details));
    }

    if (core::Status recorded = record(artifact_id, artifact.value().job_id, engine,
                                       artifacts::validation_state_string(target), checks);
        !recorded.is_ok()) {
        return core::Result<artifacts::ValidationState>::fail(recorded.take_error());
    }
    if (core::Status updated = artifacts_->set_validation_state(artifact_id, target);
        !updated.is_ok()) {
        return core::Result<artifacts::ValidationState>::fail(updated.take_error());
    }
    return core::Result<artifacts::ValidationState>::ok(target);
}

core::Status ValidationEngine::record(const std::string& artifact_id, const std::string& job_id,
                                      const std::string& engine, const std::string& status,
                                      const core::Json& checks) {
    const std::string validation_id = core::new_uuid();
    return db_->run(
        "INSERT INTO validations (validation_id, artifact_id, job_id, engine, status, checks, "
        "created_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
        {core::Json(validation_id), core::Json(artifact_id), core::Json(job_id),
         core::Json(engine), core::Json(status), core::Json(checks.dump()),
         core::Json(core::iso_utc_now())});
}

core::Result<std::vector<ValidationRecord>> ValidationEngine::history_for_artifact(
    const std::string& artifact_id) const {
    auto rows = db_->query(
        "SELECT validation_id, artifact_id, job_id, engine, status, checks, created_at "
        "FROM validations WHERE artifact_id = ? ORDER BY created_at;",
        {core::Json(artifact_id)});
    if (rows.is_error()) {
        return core::Result<std::vector<ValidationRecord>>::fail(rows.take_error());
    }
    std::vector<ValidationRecord> out;
    out.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        ValidationRecord record;
        record.validation_id = row[0].as_string();
        record.artifact_id = row[1].as_string();
        record.job_id = row[2].as_string();
        record.engine = row[3].as_string();
        record.status = row[4].as_string();
        try {
            record.checks = core::Json::parse(row[5].as_string());
        } catch (...) {
            record.checks = core::Json::object();
        }
        record.created_at = row[6].as_string();
        out.push_back(std::move(record));
    }
    return core::Result<std::vector<ValidationRecord>>::ok(std::move(out));
}

}  // namespace trinity::validation

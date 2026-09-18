#include "Artifact.hpp"

#include <cstdio>
#include <filesystem>

#include "../core/FileSystem.hpp"
#include "../core/Sha256.hpp"
#include "../core/Time.hpp"
#include "../core/Uuid.hpp"

namespace trinity::artifacts {
namespace {

core::Error invalid(const std::string& message) {
    return core::Error(core::ErrorCode::RequestValidationError, message);
}

}  // namespace

const char* validation_state_string(ValidationState state) {
    switch (state) {
        case ValidationState::Generated: return "GENERATED";
        case ValidationState::Validated: return "VALIDATED";
        case ValidationState::Verified: return "VERIFIED";
        case ValidationState::Failed: return "FAILED";
    }
    return "GENERATED";
}

core::Result<ValidationState> validation_state_from_string(const std::string& text) {
    if (text == "GENERATED") return core::Result<ValidationState>::ok(ValidationState::Generated);
    if (text == "VALIDATED") return core::Result<ValidationState>::ok(ValidationState::Validated);
    if (text == "VERIFIED") return core::Result<ValidationState>::ok(ValidationState::Verified);
    if (text == "FAILED") return core::Result<ValidationState>::ok(ValidationState::Failed);
    return core::Result<ValidationState>::fail(invalid("unknown validation state '" + text + "'"));
}

const ArtifactType kKnownTypes[] = {
    {"step", ".step"},   {"stl", ".stl"},     {"3mf", ".3mf"},   {"glb", ".glb"},
    {"obj", ".obj"},     {"json", ".json"},   {"csv", ".csv"},   {"txt", ".txt"},
    {"pdf", ".pdf"},     {"png", ".png"},     {"source", ".src"}, {"report", ".rpt"},
    {"simulation", ".sim"}, {"pcb", ".pcb"},  {"firmware", ".fw"},
};
const int kKnownTypeCount =
    static_cast<int>(sizeof(kKnownTypes) / sizeof(kKnownTypes[0]));

bool is_known_type(const std::string& type) {
    for (int i = 0; i < kKnownTypeCount; ++i) {
        if (type == kKnownTypes[i].type) return true;
    }
    return false;
}

std::string extension_for_type(const std::string& type) {
    for (int i = 0; i < kKnownTypeCount; ++i) {
        if (type == kKnownTypes[i].type) return kKnownTypes[i].extension;
    }
    return ".bin";
}

core::Json Artifact::to_json() const {
    core::Json out = core::Json::object();
    out["artifact_id"] = artifact_id;
    out["project_id"] = project_id;
    out["job_id"] = job_id;
    out["type"] = type;
    out["filename"] = filename;
    out["path"] = path;
    out["size_bytes"] = static_cast<double>(size_bytes);
    out["hash_sha256"] = hash_sha256;
    out["engine"] = engine;
    out["engine_version"] = engine_version;
    out["validation_state"] = validation_state_string(validation_state);
    out["created_at"] = created_at;
    return out;
}

ArtifactStore::ArtifactStore(db::Database& db, std::string artifacts_root)
    : db_(&db), artifacts_root_(std::move(artifacts_root)) {}

core::Result<Artifact> ArtifactStore::store_file(const std::string& source_path,
                                                 const std::string& type,
                                                 const std::string& project_id,
                                                 const std::string& job_id,
                                                 const std::string& engine,
                                                 const std::string& engine_version) {
    if (!is_known_type(type)) {
        return core::Result<Artifact>::fail(invalid("unknown artifact type '" + type + "'"));
    }
    std::error_code ec;
    const std::filesystem::path source = core::FileSystem::normalise(source_path);
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        return core::Result<Artifact>::fail(
            invalid("source file does not exist: " + source_path));
    }

    // Read in chunks for the hash (artifacts can be large).
    core::Sha256 hash;
    std::FILE* file = nullptr;
#if defined(_WIN32)
    if (fopen_s(&file, source.string().c_str(), "rb") != 0) file = nullptr;
#else
    file = std::fopen(source.string().c_str(), "rb");
#endif
    if (file == nullptr) {
        return core::Result<Artifact>::fail(
            core::Error(core::ErrorCode::PathValidationError, "cannot open " + source_path));
    }
    char buffer[65536];
    std::size_t read = 0;
    std::uint64_t total = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        hash.update(buffer, read);
        total += read;
    }
    std::fclose(file);
    const std::string digest = hash.finish_hex();

    // Content-addressed directory: <root>/<artifact_id>/
    const std::string artifact_id = core::new_uuid();
    std::string filename = source.filename().generic_string();
    if (filename.empty()) filename = "artifact" + extension_for_type(type);

    const std::string dest_dir = artifacts_root_ + "/" + artifact_id;
    if (!core::FileSystem::ensure_directory(dest_dir, ec)) {
        return core::Result<Artifact>::fail(
            core::Error(core::ErrorCode::PathValidationError, "cannot create artifact dir"));
    }
    const std::string dest_path = dest_dir + "/" + filename;
    std::filesystem::copy_file(source, dest_path,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return core::Result<Artifact>::fail(core::Error(
            core::ErrorCode::PathValidationError, "artifact copy failed: " + ec.message()));
    }

    Artifact artifact;
    artifact.artifact_id = artifact_id;
    artifact.project_id = project_id;
    artifact.job_id = job_id;
    artifact.type = type;
    artifact.filename = filename;
    artifact.path = core::FileSystem::normalise(dest_path).generic_string();
    artifact.size_bytes = total;
    artifact.hash_sha256 = digest;
    artifact.engine = engine;
    artifact.engine_version = engine_version;
    artifact.validation_state = ValidationState::Generated;
    artifact.created_at = core::iso_utc_now();

    if (auto run = db_->run(
            "INSERT INTO artifacts (artifact_id, project_id, job_id, type, filename, path, "
            "size_bytes, hash_sha256, engine, engine_version, validation_state, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            {core::Json(artifact.artifact_id), core::Json(artifact.project_id),
             core::Json(artifact.job_id), core::Json(artifact.type),
             core::Json(artifact.filename), core::Json(artifact.path),
             core::Json(static_cast<std::int64_t>(artifact.size_bytes)),
             core::Json(artifact.hash_sha256), core::Json(artifact.engine),
             core::Json(artifact.engine_version),
             core::Json(validation_state_string(artifact.validation_state)),
             core::Json(artifact.created_at)});
        run.is_error()) {
        return core::Result<Artifact>::fail(run.take_error());
    }
    return core::Result<Artifact>::ok(std::move(artifact));
}

core::Result<Artifact> ArtifactStore::row_to_artifact(const core::JsonObject& row) const {
    Artifact artifact;
    const auto get = [&row](const char* key) -> core::Json {
        const auto it = row.find(key);
        return it != row.end() ? it->second : core::Json(nullptr);
    };
    artifact.artifact_id = get("artifact_id").as_string();
    artifact.project_id = get("project_id").as_string();
    artifact.job_id = get("job_id").as_string();
    artifact.type = get("type").as_string();
    artifact.filename = get("filename").as_string();
    artifact.path = get("path").as_string();
    artifact.size_bytes = static_cast<std::uint64_t>(get("size_bytes").as_int());
    artifact.hash_sha256 = get("hash_sha256").as_string();
    artifact.engine = get("engine").as_string();
    artifact.engine_version = get("engine_version").as_string();
    artifact.created_at = get("created_at").as_string();
    auto state = validation_state_from_string(get("validation_state").as_string());
    artifact.validation_state =
        state.is_ok() ? state.value() : ValidationState::Generated;

    if (artifact.artifact_id.empty()) {
        return core::Result<Artifact>::fail(invalid("artifact row malformed"));
    }
    return core::Result<Artifact>::ok(std::move(artifact));
}

core::Result<Artifact> ArtifactStore::get(const std::string& artifact_id) const {
    auto row = db_->query_one("SELECT * FROM artifacts WHERE artifact_id = ?;",
                              {core::Json(artifact_id)});
    if (row.is_error()) return core::Result<Artifact>::fail(row.take_error());
    if (!row.value().has_value()) {
        return core::Result<Artifact>::fail(core::Error(
            core::ErrorCode::ArtifactNotFoundError,
            "no artifact with id '" + artifact_id + "'"));
    }
    return row_to_artifact(*row.value());
}

core::Result<std::vector<Artifact>> ArtifactStore::list_for_project(
    const std::string& project_id) const {
    auto rows = db_->query(
        "SELECT * FROM artifacts WHERE project_id = ? ORDER BY created_at;",
        {core::Json(project_id)});
    if (rows.is_error()) return core::Result<std::vector<Artifact>>::fail(rows.take_error());
    std::vector<Artifact> out;
    // Column order: artifact_id, project_id, job_id, type, filename, path,
    // size_bytes, hash_sha256, engine, engine_version, validation_state, created_at.
    const char* columns[] = {"artifact_id", "project_id", "job_id",       "type",
                             "filename",    "path",       "size_bytes",   "hash_sha256",
                             "engine",      "engine_version", "validation_state", "created_at"};
    for (const auto& r : rows.value()) {
        core::JsonObject obj;
        for (int i = 0; i < 12; ++i) obj[columns[i]] = r[static_cast<std::size_t>(i)];
        auto artifact = row_to_artifact(obj);
        if (artifact.is_ok()) out.push_back(artifact.take_value());
    }
    return core::Result<std::vector<Artifact>>::ok(std::move(out));
}

core::Result<std::vector<Artifact>> ArtifactStore::list_for_job(const std::string& job_id) const {
    auto rows = db_->query("SELECT * FROM artifacts WHERE job_id = ? ORDER BY created_at;",
                           {core::Json(job_id)});
    if (rows.is_error()) return core::Result<std::vector<Artifact>>::fail(rows.take_error());
    std::vector<Artifact> out;
    const char* columns[] = {"artifact_id", "project_id", "job_id",       "type",
                             "filename",    "path",       "size_bytes",   "hash_sha256",
                             "engine",      "engine_version", "validation_state", "created_at"};
    for (const auto& r : rows.value()) {
        core::JsonObject obj;
        for (int i = 0; i < 12; ++i) obj[columns[i]] = r[static_cast<std::size_t>(i)];
        auto artifact = row_to_artifact(obj);
        if (artifact.is_ok()) out.push_back(artifact.take_value());
    }
    return core::Result<std::vector<Artifact>>::ok(std::move(out));
}

core::Status ArtifactStore::set_validation_state(const std::string& artifact_id,
                                                 ValidationState state) {
    if (auto run = db_->run(
            "UPDATE artifacts SET validation_state = ? WHERE artifact_id = ?;",
            {core::Json(validation_state_string(state)), core::Json(artifact_id)});
        run.is_error()) {
        return core::Status::fail(run.take_error());
    }
    return core::Status::ok();
}

core::Result<bool> ArtifactStore::verify_integrity(const std::string& artifact_id) const {
    auto artifact = get(artifact_id);
    if (artifact.is_error()) return core::Result<bool>::fail(artifact.take_error());
    auto contents = core::FileSystem::read_file(artifact.value().path);
    if (!contents.has_value()) {
        return core::Result<bool>::ok(false);
    }
    return core::Result<bool>::ok(core::sha256_hex(*contents) == artifact.value().hash_sha256);
}

core::Status ArtifactStore::remove(const std::string& artifact_id) {
    auto artifact = get(artifact_id);
    if (artifact.is_error()) return core::Status::fail(artifact.take_error());
    std::error_code ec;
    core::FileSystem::remove_all(artifact.value().path, ec);  // file itself
    core::FileSystem::remove_all(
        core::FileSystem::normalise(artifact.value().path).parent_path().generic_string(), ec);
    if (auto run = db_->run("DELETE FROM artifacts WHERE artifact_id = ?;",
                            {core::Json(artifact_id)});
        run.is_error()) {
        return core::Status::fail(run.take_error());
    }
    return core::Status::ok();
}

}  // namespace trinity::artifacts

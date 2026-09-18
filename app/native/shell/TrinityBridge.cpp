#include "TrinityBridge.hpp"

#include <QTimer>

#include "../core/Paths.hpp"

namespace trinity::shell {

TrinityBridge::TrinityBridge(std::unique_ptr<db::Database> db, QObject* parent)
    : QObject(parent), db_(std::move(db)) {
    settings_ = std::make_unique<settings::SettingsStore>(*db_);
    settings_->seed_defaults();

    std::string root = core::Paths::projects_root();
    if (auto configured = settings_->workspace_root();
        configured.is_ok() && !configured.value().empty()) {
        root = configured.value();
    }

    artifacts_ = std::make_unique<artifacts::ArtifactStore>(*db_, core::Paths::artifacts_dir());
    jobs_ = std::make_unique<jobs::JobSystem>(*db_, *artifacts_);
    projects_ = std::make_unique<projects::ProjectStore>(*db_, root);
    validation_ = std::make_unique<validation::ValidationEngine>(*db_, *artifacts_);
    executor_ = std::make_unique<commands::ToolExecutor>(*jobs_);

    engines::bootstrap_builtin_engines();

    // Stream job progress to QML.
    jobs_->set_callbacks(
        [this](const std::string& job_id, double progress) {
            auto record = jobs_->get(job_id);
            if (record.is_ok()) {
                emit job_updated(QString::fromStdString(job_id), progress,
                                 QString::fromStdString(
                                     jobs::job_state_string(record.value().status)));
            }
        },
        [this](const std::string& job_id, const std::string& line) {
            emit job_log(QString::fromStdString(job_id), QString::fromStdString(line));
        });
}

TrinityBridge::~TrinityBridge() = default;

QString TrinityBridge::data_dir() const { return QString::fromStdString(core::Paths::data_dir()); }

void TrinityBridge::set_active_project_id(const QString& project_id) {
    if (active_project_ != project_id) {
        active_project_ = project_id;
        emit active_project_changed();
    }
}

QVariantMap TrinityBridge::create_project(const QString& name, const QString& description) {
    auto created = projects_->create(name.toStdString(), description.toStdString());
    if (created.is_error()) {
        emit error_raised(QString::fromStdString(created.error().code_string()),
                          QString::fromStdString(created.error().message()));
        return {};
    }
    set_active_project_id(QString::fromStdString(created.value().project_id));
    QVariantMap out;
    out["project_id"] = QString::fromStdString(created.value().project_id);
    out["name"] = QString::fromStdString(created.value().name);
    out["workspace"] = QString::fromStdString(created.value().workspace);
    return out;
}

QVariantList TrinityBridge::list_projects(bool include_archived) {
    QVariantList out;
    auto projects = projects_->list(include_archived);
    for (const auto& project : projects.value_or({})) {
        QVariantMap entry;
        entry["project_id"] = QString::fromStdString(project.project_id);
        entry["name"] = QString::fromStdString(project.name);
        entry["description"] = QString::fromStdString(project.description);
        entry["archived"] = project.archived;
        out.append(entry);
    }
    return out;
}

bool TrinityBridge::open_project(const QString& project_id) {
    auto fetched = projects_->get(project_id.toStdString());
    if (fetched.is_error()) return false;
    set_active_project_id(project_id);
    return true;
}

bool TrinityBridge::archive_project(const QString& project_id, bool archived) {
    return projects_->archive(project_id.toStdString(), archived).is_ok();
}

QVariantList TrinityBridge::list_engines() const {
    QVariantList out;
    for (const auto& descriptor : engines::EngineRegistry::instance().list()) {
        QVariantMap entry;
        entry["id"] = QString::fromStdString(descriptor.id);
        entry["name"] = QString::fromStdString(descriptor.name);
        entry["version"] = QString::fromStdString(descriptor.version);
        QStringList capabilities;
        for (const auto& capability : descriptor.capabilities) {
            capabilities << QString::fromStdString(capability);
        }
        entry["capabilities"] = capabilities;
        entry["health"] = QString::fromStdString(engines::engine_health_string(descriptor.health));
        entry["health_detail"] = QString::fromStdString(descriptor.health_detail);
        out.append(entry);
    }
    return out;
}

QVariantMap TrinityBridge::run_command(const QString& text) {
    QVariantMap out;
    auto outcome = executor_->run_text(text.toStdString(), active_project_.toStdString());
    if (outcome.is_error()) {
        out["ok"] = false;
        out["error"] = QString::fromStdString(outcome.error().message());
        emit error_raised(QString::fromStdString(outcome.error().code_string()),
                          QString::fromStdString(outcome.error().message()));
        return out;
    }
    jobs_->wait_for_idle();
    auto record = jobs_->get(outcome.value().job_id);
    out["ok"] = true;
    out["job_id"] = QString::fromStdString(outcome.value().job_id);
    if (record.is_ok()) {
        out["status"] = QString::fromStdString(jobs::job_state_string(record.value().status));
        // Persist pending artifacts through the single writer.
        QVariantList stored;
        if (const core::Json* pending = record.value().output.find("pending_artifacts")) {
            for (const core::Json& entry : pending->as_array()) {
                auto artifact = artifacts_->store_file(
                    entry.find("path")->as_string(), entry.find("type")->as_string(),
                    active_project_.toStdString(), record.value().job_id,
                    record.value().engine, "1.0");
                if (artifact.is_ok()) {
                    QVariantMap ref;
                    ref["artifact_id"] =
                        QString::fromStdString(artifact.value().artifact_id);
                    ref["type"] = QString::fromStdString(artifact.value().type);
                    ref["hash"] = QString::fromStdString(artifact.value().hash_sha256);
                    stored.append(ref);
                }
            }
        }
        out["artifacts"] = stored;
    }
    emit command_result(out);
    return out;
}

QVariantList TrinityBridge::recent_jobs(int limit) const {
    QVariantList out;
    for (const auto& job : jobs_->list_recent(static_cast<std::size_t>(limit)).value_or({})) {
        QVariantMap entry;
        entry["job_id"] = QString::fromStdString(job.job_id);
        entry["engine"] = QString::fromStdString(job.engine);
        entry["operation"] = QString::fromStdString(job.operation);
        entry["status"] = QString::fromStdString(jobs::job_state_string(job.status));
        entry["progress"] = job.progress;
        out.append(entry);
    }
    return out;
}

bool TrinityBridge::pause_job(const QString& job_id) {
    return jobs_->pause(job_id.toStdString()).is_ok();
}

bool TrinityBridge::resume_job(const QString& job_id) {
    return jobs_->resume(job_id.toStdString()).is_ok();
}

bool TrinityBridge::cancel_job(const QString& job_id) {
    return jobs_->cancel(job_id.toStdString()).is_ok();
}

QVariantList TrinityBridge::project_artifacts() const {
    QVariantList out;
    for (const auto& artifact :
         artifacts_->list_for_project(active_project_.toStdString()).value_or({})) {
        QVariantMap entry;
        entry["artifact_id"] = QString::fromStdString(artifact.artifact_id);
        entry["type"] = QString::fromStdString(artifact.type);
        entry["filename"] = QString::fromStdString(artifact.filename);
        entry["state"] =
            QString::fromStdString(artifacts::validation_state_string(artifact.validation_state));
        entry["hash"] = QString::fromStdString(artifact.hash_sha256);
        out.append(entry);
    }
    return out;
}

QVariantMap TrinityBridge::validate_artifact(const QString& artifact_id) {
    QVariantMap out;
    auto state = validation_->apply_checks(artifact_id.toStdString(), "cad",
                                           core::Json::object(), true, false);
    out["ok"] = state.is_ok();
    out["state"] = state.is_ok()
                       ? QString::fromStdString(artifacts::validation_state_string(state.value()))
                       : QString::fromStdString(state.error().message());
    return out;
}

QVariantMap TrinityBridge::verify_artifact(const QString& artifact_id) {
    QVariantMap out;
    auto state = validation_->apply_checks(artifact_id.toStdString(), "cad",
                                           core::Json::object(), true, true);
    out["ok"] = state.is_ok();
    out["state"] = state.is_ok()
                       ? QString::fromStdString(artifacts::validation_state_string(state.value()))
                       : QString::fromStdString(state.error().message());
    return out;
}

QVariantMap TrinityBridge::all_settings() const {
    // Categories -> entries; conversion from core::Json to QVariantMap.
    QVariantMap out;
    const core::Json grouped = settings_->to_json();
    for (const auto& [category, entries] : grouped.as_object()) {
        QVariantList list;
        for (const core::Json& entry : entries.as_array()) {
            QVariantMap item;
            item["key"] = QString::fromStdString(entry.find("key")->as_string());
            item["label"] = QString::fromStdString(entry.find("label")->as_string());
            item["value"] = QString::fromStdString(entry.find("value")->as_string());
            item["default"] = QString::fromStdString(entry.find("default")->as_string());
            list.append(item);
        }
        out[QString::fromStdString(category)] = list;
    }
    return out;
}

bool TrinityBridge::set_setting(const QString& key, const QString& value) {
    const core::Status status = settings_->set(key.toStdString(), value.toStdString());
    if (key == "workspace.root" && status.is_ok()) {
        projects_ = std::make_unique<projects::ProjectStore>(
            *db_,
            value.toStdString().empty() ? core::Paths::projects_root() : value.toStdString());
    }
    return status.is_ok();
}

std::string TrinityBridge::projects_root() const {
    auto configured = settings_->workspace_root();
    return configured.is_ok() && !configured.value().empty() ? configured.value()
                                                             : core::Paths::projects_root();
}

}  // namespace trinity::shell

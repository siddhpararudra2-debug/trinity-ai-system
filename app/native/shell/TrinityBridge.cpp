#include "TrinityBridge.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <filesystem>

#include "../core/Paths.hpp"
#include "controllers/LayoutController.hpp"
#include "controllers/SettingsController.hpp"
#include "controllers/ViewportController.hpp"
#include "controllers/WorkspaceController.hpp"
#include "models/ArtifactModel.hpp"
#include "models/CommandModel.hpp"
#include "models/EngineModel.hpp"
#include "models/JobModel.hpp"
#include "models/LogModel.hpp"
#include "models/ProjectModel.hpp"

namespace trinity::shell {

TrinityBridge::TrinityBridge(std::unique_ptr<db::Database> db, QObject* parent)
    : QObject(parent), db_(std::move(db)) {
    // Event bus owned here (standalone shell). When embedded in Application, caller can inject, but shell owns one.
    eventsOwned_ = std::make_unique<core::EventBus>();
    events_ = eventsOwned_.get();

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
    commandRegistry_ = std::make_unique<commands::CommandRegistry>(events_);

    // Register built-ins
    {
        auto cmds = commands::builtin_commands(*projects_, *executor_, *validation_);
        for (auto& c : cmds) commandRegistry_->register_command(c);
    }

    engines::bootstrap_builtin_engines();

    // Wire jobs to event bus + bridge signals (queued to Qt thread)
    jobs_->set_event_bus(events_);
    wireJobCallbacks();

    // --- Models / Controllers ---
    projectModel_ = std::make_unique<ProjectModel>(projects_.get(), this);
    projectModel_->refresh(true);

    jobModel_ = std::make_unique<JobModel>(jobs_.get(), events_, this);
    artifactModel_ = std::make_unique<ArtifactModel>(artifacts_.get(), events_, this);
    engineModel_ = std::make_unique<EngineModel>(this);
    logModel_ = std::make_unique<LogModel>(events_, this);
    commandModel_ = std::make_unique<CommandModel>(commandRegistry_.get(), this);
    commandFilterModel_ = std::make_unique<CommandFilterModel>(this);
    commandFilterModel_->setSourceModel(commandModel_.get());

    workspace_ = std::make_unique<WorkspaceController>(this);
    {
        QString layoutFile = QString::fromStdString(core::Paths::data_dir()) + "/layout.json";
        // Prefer QStandardPaths writable
        QString alt = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Trinity/layout.json";
        if (!QDir(QString::fromStdString(core::Paths::data_dir())).exists()) layoutFile = alt;
        layout_ = std::make_unique<LayoutController>(layoutFile, this);
        connect(layout_.get(), &LayoutController::layoutChanged, this, [this]() { layout_->save(); });
    }
    viewport_ = std::make_unique<ViewportController>(events_, artifacts_.get(), this);
    settingsCtrl_ = std::make_unique<SettingsController>(settings_.get(), this);

    // Keep artifact model filtered by active project
    connect(this, &TrinityBridge::active_project_changed, this, [this]() {
        projectModel_->setActiveProjectId(active_project_);
        artifactModel_->setActiveProjectId(active_project_);
    });
    // When a new project is created via model, also update active id
    connect(projectModel_.get(), &ProjectModel::projectCreated, this, [this](const QString& pid) {
        set_active_project_id(pid);
    });
}

TrinityBridge::~TrinityBridge() = default;

void TrinityBridge::wireJobCallbacks() {
    jobs_->set_callbacks(
        [this](const std::string& job_id, double progress) {
            QString qid = QString::fromStdString(job_id);
            QMetaObject::invokeMethod(this, [this, qid, progress]() {
                // lookup status for completeness
                auto rec = jobs_->get(qid.toStdString());
                QString status = rec.is_ok() ? QString::fromStdString(jobs::job_state_string(rec.value().status)) : QString("RUNNING");
                emit job_updated(qid, progress, status);
            }, Qt::QueuedConnection);
        },
        [this](const std::string& job_id, const std::string& line) {
            QString qid = QString::fromStdString(job_id);
            QString qline = QString::fromStdString(line);
            QMetaObject::invokeMethod(this, [this, qid, qline]() { emit job_log(qid, qline); },
                                      Qt::QueuedConnection);
        });
    jobs_->set_state_callback([this](const jobs::JobRecord& rec) {
        QString qid = QString::fromStdString(rec.job_id);
        double prog = rec.progress;
        QString status = QString::fromStdString(jobs::job_state_string(rec.status));
        QMetaObject::invokeMethod(this, [this, qid, prog, status]() { emit job_updated(qid, prog, status); },
                                  Qt::QueuedConnection);
    });
}

QString TrinityBridge::data_dir() const { return QString::fromStdString(core::Paths::data_dir()); }

void TrinityBridge::set_active_project_id(const QString& project_id) {
    if (active_project_ != project_id) {
        active_project_ = project_id;
        emit active_project_changed();
    }
}

// ---- projects ----
QVariantMap TrinityBridge::create_project(const QString& name, const QString& description) {
    // delegate to model so list stays coherent
    return projectModel_->createProject(name, description);
}
QVariantList TrinityBridge::list_projects(bool include_archived) {
    QVariantList out;
    projectModel_->refresh(include_archived);
    for (int i = 0; i < projectModel_->rowCount(); ++i) {
        QVariantMap m = projectModel_->get(i);
        QVariantMap e;
        e["project_id"] = m["projectId"];
        e["name"] = m["name"];
        e["description"] = m["description"];
        e["archived"] = m["archived"];
        out.append(e);
    }
    return out;
}
bool TrinityBridge::open_project(const QString& project_id) {
    return projectModel_->openProject(project_id);
}
bool TrinityBridge::archive_project(const QString& project_id, bool archived) {
    return projectModel_->archiveProject(project_id, archived);
}
bool TrinityBridge::rename_project(const QString& projectId, const QString& newName) {
    return projectModel_->renameProject(projectId, newName);
}

// ---- engines/commands ----
QVariantList TrinityBridge::list_engines() const {
    QVariantList out;
    for (int i = 0; i < engineModel_->rowCount(); ++i) {
        QVariantMap m = engineModel_->get(i);
        QVariantMap e;
        e["id"] = m["engineId"];
        e["name"] = m["name"];
        e["version"] = m["version"];
        e["capabilities"] = m["capabilities"];
        e["health"] = m["health"];
        e["health_detail"] = m["healthDetail"];
        out.append(e);
    }
    return out;
}

QVariantMap TrinityBridge::run_command_async(const QString& text) {
    QVariantMap out;
    auto outcome = executor_->run_text(text.toStdString(), active_project_.toStdString());
    if (outcome.is_error()) {
        out["ok"] = false;
        out["error"] = QString::fromStdString(outcome.error().message());
        out["code"] = QString::fromStdString(outcome.error().code_string());
        emit error_raised(out["code"].toString(), out["error"].toString());
        return out;
    }
    out["ok"] = true;
    out["job_id"] = QString::fromStdString(outcome.value().job_id);
    out["explanation"] = QString::fromStdString(outcome.value().explanation);
    // Do NOT block — let JobModel/EventBus drive progress.
    // Still attempt to store pending artifacts asynchronously when job finishes
    // For immediacy, schedule a deferred artifact ingest via QTimer polling job completion
    QString jid = out["job_id"].toString();
    QTimer::singleShot(120, this, [this, jid]() {
        auto rec = jobs_->get(jid.toStdString());
        if (!rec.is_ok()) return;
        // If job is still queued/running, re-arm
        if (rec.value().status == jobs::JobState::Queued || rec.value().status == jobs::JobState::Running) {
            QTimer::singleShot(200, this, [this, jid]() {
                // use jobModel refresh to sync
                jobModel_->refresh(100);
            });
        }
        // Pending artifacts are already handled inside JobSystem workers; artifactModel will auto-refresh via event
        jobModel_->refresh(100);
    });
    emit command_result(out);
    return out;
}

QVariantMap TrinityBridge::run_command(const QString& text) {
    // Compat: some QML calls expect blocking until job terminal for validation.
    // We call async then wait briefly, but never block UI thread longer than needed.
    // If called from QML (UI thread), prefer async; detect and avoid deadlock.
    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        // On UI thread: do async and return pending; caller should listen to job_updated
        auto out = run_command_async(text);
        // Optionally wait with event loop pumping for legacy callers that check job completion immediately
        // but cap at 3s to avoid freezing workstation
        QString jid = out["job_id"].toString();
        if (!jid.isEmpty() && out["ok"].toBool()) {
            // Non-blocking wait simulation: we return immediately with status QUEUED
            out["status"] = "QUEUED";
        }
        return out;
    }
    // Off UI thread: safe to wait
    auto outcome = executor_->run_text(text.toStdString(), active_project_.toStdString());
    QVariantMap out;
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
        QVariantList stored;
        if (const core::Json* pending = record.value().output.find("pending_artifacts")) {
            for (const core::Json& entry : pending->as_array()) {
                auto* pathJson = entry.find("path");
                auto* typeJson = entry.find("type");
                if (!pathJson || !typeJson) continue;
                auto artifact = artifacts_->store_file(
                    pathJson->as_string(), typeJson->as_string(),
                    active_project_.toStdString(), record.value().job_id,
                    record.value().engine, "1.0");
                if (artifact.is_ok()) {
                    QVariantMap ref;
                    ref["artifact_id"] = QString::fromStdString(artifact.value().artifact_id);
                    ref["type"] = QString::fromStdString(artifact.value().type);
                    ref["hash"] = QString::fromStdString(artifact.value().hash_sha256);
                    stored.append(ref);
                }
            }
        }
        out["artifacts"] = stored;
    }
    emit command_result(out);
    // Refresh models
    QMetaObject::invokeMethod(this, [this]() { jobModel_->refresh(100); artifactModel_->refresh(); }, Qt::QueuedConnection);
    return out;
}

// ---- jobs ----
QVariantList TrinityBridge::recent_jobs(int limit) const {
    QVariantList out;
    if (!jobModel_) return out;
    const_cast<JobModel*>(jobModel_.get())->refresh(limit);
    for (int i = 0; i < jobModel_->rowCount(); ++i) {
        out.append(jobModel_->get(i));
    }
    return out;
}
bool TrinityBridge::pause_job(const QString& job_id) { return jobModel_ ? jobModel_->pauseJob(job_id) : false; }
bool TrinityBridge::resume_job(const QString& job_id) { return jobModel_ ? jobModel_->resumeJob(job_id) : false; }
bool TrinityBridge::cancel_job(const QString& job_id) { return jobModel_ ? jobModel_->cancelJob(job_id) : false; }
QVariantMap TrinityBridge::get_job(const QString& jobId) const {
    return jobModel_ ? jobModel_->getJob(jobId) : QVariantMap{};
}

// ---- artifacts ----
QVariantList TrinityBridge::project_artifacts() const {
    QVariantList out;
    if (!artifactModel_) return out;
    const_cast<ArtifactModel*>(artifactModel_.get())->refresh();
    for (int i = 0; i < artifactModel_->rowCount(); ++i) {
        QVariantMap m = artifactModel_->get(i);
        QVariantMap e;
        e["artifact_id"] = m["artifactId"];
        e["type"] = m["type"];
        e["filename"] = m["filename"];
        e["state"] = m["validationState"];
        e["hash"] = m["hash"];
        out.append(e);
    }
    return out;
}
QVariantMap TrinityBridge::validate_artifact(const QString& artifact_id) {
    QVariantMap out;
    auto state = validation_->apply_checks(artifact_id.toStdString(), "cad", core::Json::object(), true, false);
    out["ok"] = state.is_ok();
    out["state"] = state.is_ok() ? QString::fromStdString(artifacts::validation_state_string(state.value()))
                                 : QString::fromStdString(state.error().message());
    if (state.is_ok()) artifactModel_->refresh();
    return out;
}
QVariantMap TrinityBridge::verify_artifact(const QString& artifact_id) {
    QVariantMap out;
    auto state = validation_->apply_checks(artifact_id.toStdString(), "cad", core::Json::object(), true, true);
    out["ok"] = state.is_ok();
    out["state"] = state.is_ok() ? QString::fromStdString(artifacts::validation_state_string(state.value()))
                                 : QString::fromStdString(state.error().message());
    if (state.is_ok()) artifactModel_->refresh();
    return out;
}
bool TrinityBridge::verify_integrity(const QString& artifactId) {
    return artifactModel_ ? artifactModel_->verifyIntegrity(artifactId) : false;
}
bool TrinityBridge::remove_artifact(const QString& artifactId) {
    return artifactModel_ ? artifactModel_->removeArtifact(artifactId) : false;
}
QVariantMap TrinityBridge::import_artifact(const QString& fileUrl, const QString& type) {
    QVariantMap out; out["ok"]=false;
    if (active_project_.isEmpty()) { emit error_raised("request_validation_error","No active project — create or open one first"); return out; }
    QString local = fileUrl;
    if (local.startsWith("file:///")) local = QUrl(fileUrl).toLocalFile();
    else if (local.startsWith("file:")) local = QUrl(fileUrl).toLocalFile();
    if (local.isEmpty() || !QFile::exists(local)) { emit error_raised("request_validation_error","File not found: "+fileUrl); return out; }
    std::string stdPath = local.toStdString();
    std::string stdType = type.toStdString();
    if (stdType.empty()) {
        // infer from extension
        auto pos = stdPath.rfind('.');
        stdType = (pos!=std::string::npos) ? stdPath.substr(pos+1) : "stl";
        for (auto& c: stdType) c = (char)::tolower(c);
        if (!artifacts::is_known_type(stdType)) stdType = "stl";
    }
    auto res = artifacts_->store_file(stdPath, stdType, active_project_.toStdString(), "", "import", "1.0");
    if (res.is_error()) {
        emit error_raised(QString::fromStdString(res.error().code_string()), QString::fromStdString(res.error().message()));
        return out;
    }
    artifactModel_->refresh();
    out["ok"]=true;
    out["artifact_id"]=QString::fromStdString(res.value().artifact_id);
    out["type"]=QString::fromStdString(res.value().type);
    out["path"]=QString::fromStdString(res.value().path);
    return out;
}
QVariantList TrinityBridge::list_project_files(const QString& subfolder) const {
    QVariantList out;
    if (active_project_.isEmpty() || !projects_) return out;
    auto proj = projects_->get(active_project_.toStdString());
    if (proj.is_error()) return out;
    std::string ws = proj.value().workspace;
    std::string dir = ws;
    if (!subfolder.isEmpty()) dir += "/" + subfolder.toStdString();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return out;
    for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (e.is_regular_file()) {
            QVariantMap m;
            m["name"]=QString::fromStdString(e.path().filename().string());
            m["path"]=QString::fromStdString(e.path().string());
            m["size"]= (qlonglong)e.file_size(ec);
            out.append(m);
        }
    }
    return out;
}

// ---- settings ----
QVariantMap TrinityBridge::all_settings() const {
    return settingsCtrl_ ? settingsCtrl_->allSettings() : QVariantMap{};
}
bool TrinityBridge::set_setting(const QString& key, const QString& value) {
    return settingsCtrl_ ? settingsCtrl_->set(key, value) : false;
}
QString TrinityBridge::get_setting(const QString& key) const {
    return settingsCtrl_ ? settingsCtrl_->get(key) : QString();
}
QVariantList TrinityBridge::validation_history(const QString& artifactId) const {
    QVariantList out;
    if (!validation_ || artifactId.isEmpty()) return out;
    auto res = validation_->history_for_artifact(artifactId.toStdString());
    if (res.is_error()) return out;
    for (const auto& rec : res.value()) {
        QVariantMap m;
        m["validationId"] = QString::fromStdString(rec.validation_id);
        m["artifactId"] = QString::fromStdString(rec.artifact_id);
        m["jobId"] = QString::fromStdString(rec.job_id);
        m["engine"] = QString::fromStdString(rec.engine);
        m["status"] = QString::fromStdString(rec.status);
        m["checks"] = QString::fromStdString(rec.checks.dump());
        m["createdAt"] = QString::fromStdString(rec.created_at);
        out.append(m);
    }
    return out;
}
QVariantMap TrinityBridge::app_status() const {
    QVariantMap out;
    // Build from engineModel + jobModel + layout + status
    out["dataDir"] = data_dir();
    out["activeProject"] = active_project_;
    out["engines"] = engineModel_ ? engineModel_->rowCount() : 0;
    out["jobs"] = jobModel_ ? jobModel_->rowCount() : 0;
    out["artifacts"] = artifactModel_ ? artifactModel_->rowCount() : 0;
    return out;
}

std::string TrinityBridge::projects_root() const {
    auto configured = settings_->workspace_root();
    return configured.is_ok() && !configured.value().empty() ? configured.value() : core::Paths::projects_root();
}

}  // namespace trinity::shell

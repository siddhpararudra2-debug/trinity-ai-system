// Trinity — QML bridge. Wraps the core systems (projects, jobs, engines,
// commands, artifacts) into Qt-friendly signals/slots. UI logic stays in QML;
// all business logic stays in trinity_core (no UI/business mixing).
#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "../artifacts/Artifact.hpp"
#include "../commands/Commands.hpp"
#include "../core/Json.hpp"
#include "../db/Database.hpp"
#include "../engines/Engine.hpp"
#include "../jobs/JobSystem.hpp"
#include "../projects/Project.hpp"
#include "../settings/Settings.hpp"
#include "../validation/ValidationEngine.hpp"

namespace trinity::shell {

class TrinityBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dataDir READ data_dir CONSTANT)
    Q_PROPERTY(QString activeProjectId READ active_project_id WRITE set_active_project_id
                   NOTIFY active_project_changed)

public:
    TrinityBridge(std::unique_ptr<db::Database> db, QObject* parent);
    ~TrinityBridge() override;

    QString data_dir() const;
    QString active_project_id() const { return active_project_; }
    void set_active_project_id(const QString& project_id);

    // C++ side access for integration points.
    jobs::JobSystem& jobs() { return *jobs_; }

public slots:
    // ---- project lifecycle -------------------------------------------------
    QVariantMap create_project(const QString& name, const QString& description);
    QVariantList list_projects(bool include_archived);
    bool open_project(const QString& project_id);
    bool archive_project(const QString& project_id, bool archived);

    // ---- engines + commands ------------------------------------------------
    QVariantList list_engines() const;
    QVariantMap run_command(const QString& text);

    // ---- jobs ----------------------------------------------------------------
    QVariantList recent_jobs(int limit) const;
    bool pause_job(const QString& job_id);
    bool resume_job(const QString& job_id);
    bool cancel_job(const QString& job_id);

    // ---- artifacts -----------------------------------------------------------
    QVariantList project_artifacts() const;
    QVariantMap validate_artifact(const QString& artifact_id);
    QVariantMap verify_artifact(const QString& artifact_id);

    // ---- settings ------------------------------------------------------------
    QVariantMap all_settings() const;
    bool set_setting(const QString& key, const QString& value);

signals:
    void active_project_changed();
    void job_updated(const QString& job_id, double progress, const QString& status);
    void job_log(const QString& job_id, const QString& line);
    void command_result(const QVariantMap& result);
    void error_raised(const QString& code, const QString& message);

private:
    std::string projects_root() const;

    std::unique_ptr<db::Database> db_;
    std::unique_ptr<settings::SettingsStore> settings_;
    std::unique_ptr<artifacts::ArtifactStore> artifacts_;
    std::unique_ptr<jobs::JobSystem> jobs_;
    std::unique_ptr<projects::ProjectStore> projects_;
    std::unique_ptr<validation::ValidationEngine> validation_;
    std::unique_ptr<commands::ToolExecutor> executor_;
    QString active_project_;
};

}  // namespace trinity::shell

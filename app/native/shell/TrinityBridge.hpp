// Trinity — QML bridge (engineering workstation).
// Facade over trinity_core: exposes QAbstractListModels and Controllers to QML.
// QML contains zero business logic; all state mutations go through this layer
// and its owned models/controllers.
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

// Forward declare models/controllers to keep header lightweight
namespace trinity::shell {
class ProjectModel;
class JobModel;
class ArtifactModel;
class EngineModel;
class LogModel;
class CommandModel;
class CommandFilterModel;
class WorkspaceController;
class LayoutController;
class ViewportController;
class SettingsController;
}

namespace trinity::shell {

class TrinityBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dataDir READ data_dir CONSTANT)
    Q_PROPERTY(QString activeProjectId READ active_project_id WRITE set_active_project_id NOTIFY active_project_changed)
    Q_PROPERTY(ProjectModel* projectModel READ projectModel CONSTANT)
    Q_PROPERTY(JobModel* jobModel READ jobModel CONSTANT)
    Q_PROPERTY(ArtifactModel* artifactModel READ artifactModel CONSTANT)
    Q_PROPERTY(EngineModel* engineModel READ engineModel CONSTANT)
    Q_PROPERTY(LogModel* logModel READ logModel CONSTANT)
    Q_PROPERTY(CommandModel* commandModel READ commandModel CONSTANT)
    Q_PROPERTY(CommandFilterModel* commandFilterModel READ commandFilterModel CONSTANT)
    Q_PROPERTY(WorkspaceController* workspace READ workspace CONSTANT)
    Q_PROPERTY(LayoutController* layout READ layout CONSTANT)
    Q_PROPERTY(ViewportController* viewport READ viewport CONSTANT)
    Q_PROPERTY(SettingsController* settingsCtrl READ settingsCtrl CONSTANT)

public:
    TrinityBridge(std::unique_ptr<db::Database> db, QObject* parent = nullptr);
    ~TrinityBridge() override;

    QString data_dir() const;
    QString active_project_id() const { return active_project_; }
    void set_active_project_id(const QString& project_id);

    // Models / controllers for QML binding
    ProjectModel* projectModel() const { return projectModel_.get(); }
    JobModel* jobModel() const { return jobModel_.get(); }
    ArtifactModel* artifactModel() const { return artifactModel_.get(); }
    EngineModel* engineModel() const { return engineModel_.get(); }
    LogModel* logModel() const { return logModel_.get(); }
    CommandModel* commandModel() const { return commandModel_.get(); }
    CommandFilterModel* commandFilterModel() const { return commandFilterModel_.get(); }
    WorkspaceController* workspace() const { return workspace_.get(); }
    LayoutController* layout() const { return layout_.get(); }
    ViewportController* viewport() const { return viewport_.get(); }
    SettingsController* settingsCtrl() const { return settingsCtrl_.get(); }

    // C++ access for tests / integration
    jobs::JobSystem& jobs() { return *jobs_; }
    core::EventBus& events() { return *events_; }

public slots:
    // ---- project lifecycle (compat + model-delegating) -----------------------
    QVariantMap create_project(const QString& name, const QString& description);
    QVariantList list_projects(bool include_archived);
    bool open_project(const QString& project_id);
    bool archive_project(const QString& project_id, bool archived);
    Q_INVOKABLE bool rename_project(const QString& projectId, const QString& newName);

    // ---- engines + commands ------------------------------------------------
    QVariantList list_engines() const;
    QVariantMap run_command(const QString& text); // async-submit + immediate return (compat: still waits for legacy callers)
    Q_INVOKABLE QVariantMap run_command_async(const QString& text);

    // ---- jobs ---------------------------------------------------------------
    QVariantList recent_jobs(int limit) const;
    bool pause_job(const QString& job_id);
    bool resume_job(const QString& job_id);
    bool cancel_job(const QString& job_id);
    Q_INVOKABLE QVariantMap get_job(const QString& jobId) const;

    // ---- artifacts -----------------------------------------------------------
    QVariantList project_artifacts() const;
    QVariantMap validate_artifact(const QString& artifact_id);
    QVariantMap verify_artifact(const QString& artifact_id);
    Q_INVOKABLE bool verify_integrity(const QString& artifactId);
    Q_INVOKABLE bool remove_artifact(const QString& artifactId);
    Q_INVOKABLE QVariantMap import_artifact(const QString& fileUrl, const QString& type = "stl");
    Q_INVOKABLE QVariantList list_project_files(const QString& subfolder = "") const;

    // ---- settings ------------------------------------------------------------
    QVariantMap all_settings() const;
    bool set_setting(const QString& key, const QString& value);
    Q_INVOKABLE QString get_setting(const QString& key) const;

    // ---- validation history --------------------------------------------------
    Q_INVOKABLE QVariantList validation_history(const QString& artifactId) const;

    // ---- app status ----------------------------------------------------------
    Q_INVOKABLE QVariantMap app_status() const;

signals:
    void active_project_changed();
    void job_updated(const QString& job_id, double progress, const QString& status);
    void job_log(const QString& job_id, const QString& line);
    void command_result(const QVariantMap& result);
    void error_raised(const QString& code, const QString& message);

private:
    std::string projects_root() const;
    void wireJobCallbacks();

    std::unique_ptr<db::Database> db_;
    std::unique_ptr<core::EventBus> eventsOwned_; // owned when not injected
    core::EventBus* events_ = nullptr;
    std::unique_ptr<settings::SettingsStore> settings_;
    std::unique_ptr<artifacts::ArtifactStore> artifacts_;
    std::unique_ptr<jobs::JobSystem> jobs_;
    std::unique_ptr<projects::ProjectStore> projects_;
    std::unique_ptr<validation::ValidationEngine> validation_;
    std::unique_ptr<commands::ToolExecutor> executor_;
    std::unique_ptr<commands::CommandRegistry> commandRegistry_;

    // QML models/controllers
    std::unique_ptr<ProjectModel> projectModel_;
    std::unique_ptr<JobModel> jobModel_;
    std::unique_ptr<ArtifactModel> artifactModel_;
    std::unique_ptr<EngineModel> engineModel_;
    std::unique_ptr<LogModel> logModel_;
    std::unique_ptr<CommandModel> commandModel_;
    std::unique_ptr<CommandFilterModel> commandFilterModel_;
    std::unique_ptr<WorkspaceController> workspace_;
    std::unique_ptr<LayoutController> layout_;
    std::unique_ptr<ViewportController> viewport_;
    std::unique_ptr<SettingsController> settingsCtrl_;

    QString active_project_;
};

}  // namespace trinity::shell

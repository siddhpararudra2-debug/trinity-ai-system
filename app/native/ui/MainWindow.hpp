#pragma once

// Trinity desktop window (QWidget). Shows product status plus the
// currently registered engines from the Engine Registry, clearly
// distinguishing implemented capabilities from scaffolded ones.
// Also hosts the requirement-understanding panel: a user request is
// parsed into a structured Intent, validated, and routed to an engine
// (lookup only — this UI never executes an engine).
//
// Viewer integration: center 3D viewport (ViewportWidget) driven by
// ViewerController/ViewerState; right inspector (ViewerPanel) for
// parameters/validation/artifacts; bottom jobs/logs. Layout:
//
// ┌──────────────────────────────────────────────┐
// │ Trinity                                      │
// ├──────────────┬───────────────────┬───────────┤
// │ Navigation   │                   │ Inspector │
// │              │     3D VIEW       │           │
// │ Projects     │                   │ Parameters│
// │ Jobs         │                   │ Validation│
// │ Engines      │                   │ Artifacts │
// ├──────────────┴───────────────────┴───────────┤
// │ Jobs / Logs / Status                         │
// └──────────────────────────────────────────────┘

#include <QMainWindow>
#include <QLineEdit>
#include <QTextEdit>

#include <string>
#include <vector>

namespace trinity::engines {
class EngineRegistry;
}
namespace trinity::jobs {
class JobManager;
}
namespace trinity::workflows {
class WorkflowExecutor;
}
namespace trinity::intelligence {
class RequestPipeline;
}
namespace trinity::storage {
class ArtifactRepository;
}
namespace trinity::artifacts {
class ArtifactManager;
}

class QTableWidget;
class QTimer;
class QComboBox;

namespace trinity::ui {

class JobTableWidget;
class WorkflowTableWidget;
class ViewportWidget;
class ViewerPanel;
class ViewerController;

struct EngineEntry {
    std::string name;
    std::string version;
    std::vector<std::string> capabilities;
    std::string lastResult;
    bool implemented = false;
};

struct InitSummary {
    std::string version = "0.1.0";
    std::string dbPath;
    std::size_t engineCount = 0;
    std::vector<EngineEntry> engines;
    std::string modelProvider = "none";
    bool modelAvailable = false;  // true once another dev plugs a real LLM in
    bool coreOk = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const InitSummary& summary, QWidget* parent = nullptr);
    explicit MainWindow(const InitSummary& summary, engines::EngineRegistry* registry,
                        QWidget* parent = nullptr);
    explicit MainWindow(const InitSummary& summary, engines::EngineRegistry* registry,
                        jobs::JobManager* jobs, workflows::WorkflowExecutor* executor,
                        intelligence::RequestPipeline* pipeline, QWidget* parent = nullptr);

    void setViewerServices(storage::ArtifactRepository* artifactRepo,
                           artifacts::ArtifactManager* artifacts);

private slots:
    void handleParse();
    void handleExecute();
    void handleMathSubmit();
    void handleDemoWorkflow();
    void refreshJobs();
    void refreshMathResult();
    void refreshWorkflows();
    void refreshArtifacts();
    void refreshLogs();
    void onViewerStateChanged();
    void onViewerModelReady(const QString& jobId);
    void onViewerLoadError(const QString& message);
    void onArtifactSelected(const std::string& artifactId);
    void onViewportMeasurement(double ax, double ay, double az, double bx, double by,
                               double bz, bool complete);
    void onViewportCameraChanged();

private:
    void buildUi();
    void refreshAll();

    InitSummary summary_;
    engines::EngineRegistry* registry_ = nullptr;
    jobs::JobManager* jobs_ = nullptr;
    workflows::WorkflowExecutor* executor_ = nullptr;
    intelligence::RequestPipeline* pipeline_ = nullptr;
    storage::ArtifactRepository* artifactRepo_ = nullptr;
    artifacts::ArtifactManager* artifactsMgr_ = nullptr;

    QLineEdit* input_ = nullptr;
    QTextEdit* output_ = nullptr;
    QLineEdit* executeInput_ = nullptr;
    QTextEdit* executeOutput_ = nullptr;
    QComboBox* mathOp_ = nullptr;
    QLineEdit* mathExpr_ = nullptr;
    QLineEdit* mathParams_ = nullptr;
    QTextEdit* mathOutput_ = nullptr;
    std::string lastMathJobId_;
    QTableWidget* jobsTable_ = nullptr;
    QTableWidget* workflowsTable_ = nullptr;
    QTextEdit* logView_ = nullptr;
    QTimer* refreshTimer_ = nullptr;

    ViewportWidget* viewport_ = nullptr;
    ViewerPanel* viewerPanel_ = nullptr;
    ViewerController* viewerController_ = nullptr;
    std::string lastShownArtifact_;
    std::string lastShownJob_;
};

}  // namespace trinity::ui

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

#include "trinity/core/Json.hpp"

namespace trinity::engines {
class EngineRegistry;
}
namespace trinity::jobs {
class JobManager;
class JobWorker;
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
class TimeSeriesWidget;

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
    void handlePcbCreate();
    void handlePcbAddComponent();
    void handlePcbAddNet();
    void handlePcbPlace();
    void handlePcbValidate();
    void handlePcbExport();
    void handleFwCreate();
    void handleFwSelectMcu();
    void handleFwConfigurePin();
    void handleFwConfigurePeripheral();
    void handleFwGenerate();
    void handleFwValidate();
    void handleFwBuild();
    void handleSimRun();
    void handleResearchIndex();
    void handleResearchSearch();
    void handleResearchSummarize();
    void handleRoboticsFk();
    void handleRoboticsPlan();
    void handleRoboticsExport();
    void handleDemoWorkflow();
    void refreshJobs();
    void refreshMathResult();
    void refreshPcbResult();
    void refreshFwResult();
    void refreshSimResult();
    void refreshResearchResult();
    void refreshRoboticsResult();
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
    // PCB workspace state: the live design JSON chains between ops
    // (create -> add -> place -> validate -> export) and is the §12
    // visualization interface (outline/placements/footprints/layers).
    QLineEdit* pcbWidth_ = nullptr;
    QLineEdit* pcbHeight_ = nullptr;
    QLineEdit* pcbThick_ = nullptr;
    QLineEdit* pcbRef_ = nullptr;
    QLineEdit* pcbValue_ = nullptr;
    QLineEdit* pcbFootprint_ = nullptr;
    QLineEdit* pcbNetName_ = nullptr;
    QLineEdit* pcbNetPins_ = nullptr;
    QLineEdit* pcbPlaceRef_ = nullptr;
    QLineEdit* pcbPlaceX_ = nullptr;
    QLineEdit* pcbPlaceY_ = nullptr;
    QLineEdit* pcbPlaceRot_ = nullptr;
    QTextEdit* pcbOutput_ = nullptr;
    core::Json lastPcbDesign_ = core::Json::object();
    bool hasPcbDesign_ = false;
    std::string lastPcbJobId_;
    // Firmware workspace state: live project JSON chains between ops
    // (create -> mcu -> pins/peripherals -> generate -> validate -> build),
    // same structured-state pattern as the PCB design.
    QLineEdit* fwName_ = nullptr;
    QComboBox* fwMcu_ = nullptr;
    QLineEdit* fwPin_ = nullptr;
    QLineEdit* fwFunc_ = nullptr;
    QComboBox* fwDir_ = nullptr;
    QComboBox* fwKind_ = nullptr;
    QLineEdit* fwPinA_ = nullptr;
    QLineEdit* fwPinB_ = nullptr;
    QLineEdit* fwParam_ = nullptr;
    QComboBox* fwProfile_ = nullptr;
    QTextEdit* fwOutput_ = nullptr;
    core::Json lastFwProject_ = core::Json::object();
    bool hasFwProject_ = false;
    std::string lastFwJobId_;
    // Simulation workspace: structured sim jobs + three time-series charts.
    QComboBox* simModel_ = nullptr;
    QLineEdit* simDuration_ = nullptr;
    QLineEdit* simVelocity_ = nullptr;
    QLineEdit* simAccel_ = nullptr;
    QLineEdit* simAngle_ = nullptr;
    QLineEdit* simMass_ = nullptr;
    QLineEdit* simForce_ = nullptr;
    QTextEdit* simOutput_ = nullptr;
    TimeSeriesWidget* simPosChart_ = nullptr;
    TimeSeriesWidget* simVelChart_ = nullptr;
    TimeSeriesWidget* simAccChart_ = nullptr;
    std::string lastSimJobId_;
    // Research workspace: deterministic local index (index/search/summarize);
    // empty inputs are reported inline without submitting a job.
    QLineEdit* researchTitle_ = nullptr;
    QTextEdit* researchText_ = nullptr;
    QLineEdit* researchQuery_ = nullptr;
    QTextEdit* researchOutput_ = nullptr;
    std::string lastResearchJobId_;

    // Robotics workspace: DH forward kinematics, joint trajectory, URDF
    // export; empty/malformed inputs are reported inline without submitting.
    QLineEdit* robotName_ = nullptr;
    QTextEdit* dhChain_ = nullptr;
    QLineEdit* jointAngles_ = nullptr;
    QLineEdit* startJoint_ = nullptr;
    QLineEdit* goalJoint_ = nullptr;
    QLineEdit* duration_ = nullptr;
    QTextEdit* roboticsOutput_ = nullptr;
    std::string lastRoboticsJobId_;
    jobs::JobWorker* worker_ = nullptr;

public:
    void setWorkerService(jobs::JobWorker* worker) { worker_ = worker; }
    // §12: board outline + placements + footprints + layers for the
    // future 2D PCB viewer. No renderer lives here.
    core::Json pcbDesignForViewer() const { return lastPcbDesign_; }
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

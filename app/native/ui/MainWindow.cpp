#include "MainWindow.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <thread>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/intelligence/IntentRouter.hpp"
#include "trinity/intelligence/IntentValidator.hpp"
#include "trinity/intelligence/RequirementParser.hpp"
#include "trinity/intelligence/RequestPipeline.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Repositories.hpp"
#include "trinity/workflows/Executor.hpp"
#include "viewer/ViewerController.hpp"
#include "viewer/ViewerPanel.hpp"
#include "viewer/ViewportWidget.hpp"

namespace trinity::ui {

MainWindow::MainWindow(const InitSummary& summary, QWidget* parent)
    : MainWindow(summary, nullptr, parent) {}

MainWindow::MainWindow(const InitSummary& summary, engines::EngineRegistry* registry,
                       QWidget* parent)
    : MainWindow(summary, registry, nullptr, nullptr, nullptr, parent) {}

MainWindow::MainWindow(const InitSummary& summary, engines::EngineRegistry* registry,
                       jobs::JobManager* jobs, workflows::WorkflowExecutor* executor,
                       intelligence::RequestPipeline* pipeline, QWidget* parent)
    : QMainWindow(parent),
      summary_(summary),
      registry_(registry),
      jobs_(jobs),
      executor_(executor),
      pipeline_(pipeline) {
    buildUi();
    refreshAll();
}

void MainWindow::setViewerServices(storage::ArtifactRepository* artifactRepo,
                                   artifacts::ArtifactManager* artifacts) {
    artifactRepo_ = artifactRepo;
    artifactsMgr_ = artifacts;
    if (viewerController_ != nullptr) {
        viewerController_->setServices(jobs_, artifactRepo_);
    }
    refreshArtifacts();
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("Trinity"));
    resize(1400, 900);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Compact header (was a full vertical stack; keep info, save space).
    auto* title = new QLabel(QStringLiteral("Trinity — Native C++ engineering workspace"), central);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
    layout->addWidget(title);

    const QString modelText =
        QString::fromStdString(summary_.modelProvider) +
        (summary_.modelAvailable ? QString() : QStringLiteral(" (no model — LLM slot open)"));
    const QString status =
        QStringLiteral("Status: running  •  Version %1  •  Engines %2  •  Model %3  •  DB %4  •  %5")
            .arg(QString::fromStdString(summary_.version))
            .arg(static_cast<qulonglong>(summary_.engineCount))
            .arg(modelText)
            .arg(QString::fromStdString(summary_.dbPath))
            .arg(summary_.coreOk ? QStringLiteral("core OK")
                                 : QStringLiteral("Core FAILED — see logs"));
    auto* statusLabel = new QLabel(status, central);
    statusLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #444;"));
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    // Viewer controller (owns ViewerState; services may be attached later).
    viewerController_ = new ViewerController(this);
    viewerController_->setServices(jobs_, artifactRepo_);
    connect(viewerController_, &ViewerController::stateChanged, this,
            &MainWindow::onViewerStateChanged);
    connect(viewerController_, &ViewerController::modelReady, this,
            &MainWindow::onViewerModelReady);
    connect(viewerController_, &ViewerController::loadError, this,
            &MainWindow::onViewerLoadError);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, central);
    layout->addWidget(mainSplitter, 1);

    // ---- Left: Navigation / Projects / Jobs / Engines (scrollable so the
    // 3D view and bottom Jobs/Logs always keep their space) ----
    auto* leftScroll = new QScrollArea(mainSplitter);
    leftScroll->setWidgetResizable(true);
    leftScroll->setMinimumWidth(300);
    auto* leftPane = new QWidget(leftScroll);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    leftLayout->setSpacing(6);

    auto* enginesTitle = new QLabel(QStringLiteral("Engines (from Registry)"), leftPane);
    enginesTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(enginesTitle);

    if (summary_.engines.empty()) {
        leftLayout->addWidget(new QLabel(QStringLiteral("No engines registered"), leftPane));
    } else {
        for (const auto& engine : summary_.engines) {
            const QString line =
                QStringLiteral("%1  •  %2  •  %3")
                    .arg(QString::fromStdString(engine.name).toUpper())
                    .arg(QString::fromStdString(engine.version))
                    .arg(engine.implemented ? QStringLiteral("implemented")
                                            : QStringLiteral("scaffolded/unavailable"));
            auto* row = new QLabel(line, leftPane);
            row->setStyleSheet(engine.implemented
                                   ? QStringLiteral("font-size: 12px; color: #1a7f37;")
                                   : QStringLiteral("font-size: 12px; color: #888;"));
            row->setWordWrap(true);
            leftLayout->addWidget(row);
        }
    }

    auto* reqTitle = new QLabel(
        QStringLiteral("Requirement Understanding (no execution)"), leftPane);
    reqTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    reqTitle->setWordWrap(true);
    leftLayout->addWidget(reqTitle);

    input_ = new QLineEdit(leftPane);
    input_->setPlaceholderText(
        QStringLiteral("e.g. Create a 50 mm quadcopter frame with 2 mm thick arms."));
    leftLayout->addWidget(input_);

    auto* parseButton = new QPushButton(QStringLiteral("Parse Requirement"), leftPane);
    leftLayout->addWidget(parseButton);
    connect(parseButton, &QPushButton::clicked, this, &MainWindow::handleParse);
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::handleParse);

    output_ = new QTextEdit(leftPane);
    output_->setReadOnly(true);
    output_->setMinimumHeight(110);
    output_->setPlaceholderText(
        QStringLiteral("Parsed intent, validation, routing, parameters, missing, errors…"));
    leftLayout->addWidget(output_);

    auto* execTitle = new QLabel(QStringLiteral("Execute (Jobs + Workflows)"), leftPane);
    execTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(execTitle);

    executeInput_ = new QLineEdit(leftPane);
    executeInput_->setText(QStringLiteral("Create a 50 mm quadcopter frame"));
    executeInput_->setPlaceholderText(QStringLiteral("Create a 50 mm quadcopter frame"));
    leftLayout->addWidget(executeInput_);

    auto* runButton = new QPushButton(QStringLiteral("Submit Request (async)"), leftPane);
    runButton->setEnabled(pipeline_ != nullptr);
    leftLayout->addWidget(runButton);
    connect(runButton, &QPushButton::clicked, this, &MainWindow::handleExecute);
    connect(executeInput_, &QLineEdit::returnPressed, this, &MainWindow::handleExecute);

    auto* demoButton =
        new QPushButton(QStringLiteral("Run Demo Workflow (2-node math)"), leftPane);
    demoButton->setEnabled(executor_ != nullptr);
    leftLayout->addWidget(demoButton);
    connect(demoButton, &QPushButton::clicked, this, &MainWindow::handleDemoWorkflow);

    executeOutput_ = new QTextEdit(leftPane);
    executeOutput_->setReadOnly(true);
    executeOutput_->setMinimumHeight(90);
    executeOutput_->setPlaceholderText(
        QStringLiteral("Execution lifecycle: submit → queued → running → completed/failed…"));
    leftLayout->addWidget(executeOutput_);
    leftLayout->addStretch(1);
    leftScroll->setWidget(leftPane);
    mainSplitter->addWidget(leftScroll);

    // ---- Center: 3D VIEW ----
    auto* centerPane = new QWidget(mainSplitter);
    auto* centerLayout = new QVBoxLayout(centerPane);
    centerLayout->setContentsMargins(4, 4, 4, 4);
    centerLayout->setSpacing(4);
    auto* viewTitle = new QLabel(
        QStringLiteral("3D VIEW  —  Left: orbit • Middle/Right: pan • Wheel: zoom • F: fit • R: reset"),
        centerPane);
    viewTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    centerLayout->addWidget(viewTitle);

    viewport_ = new ViewportWidget(centerPane);
    viewport_->setMinimumSize(360, 300);
    viewport_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    centerLayout->addWidget(viewport_, 1);
    mainSplitter->addWidget(centerPane);

    connect(viewport_, &ViewportWidget::measurementChanged, this,
            &MainWindow::onViewportMeasurement);
    connect(viewport_, &ViewportWidget::cameraChanged, this,
            &MainWindow::onViewportCameraChanged);
    connect(viewport_, &ViewportWidget::renderError, this,
            [this](const QString& msg) {
                if (viewerPanel_ != nullptr) {
                    viewerPanel_->setErrorText(msg);
                }
            });

    // ---- Right: Inspector / Parameters / Validation / Artifacts ----
    viewerPanel_ = new ViewerPanel(mainSplitter);
    viewerPanel_->setMinimumWidth(300);
    mainSplitter->addWidget(viewerPanel_);

    connect(viewerPanel_, &ViewerPanel::fitRequested, viewport_, &ViewportWidget::fitModel);
    connect(viewerPanel_, &ViewerPanel::resetRequested, viewport_,
            &ViewportWidget::resetCamera);
    connect(viewerPanel_, &ViewerPanel::projectionChanged, this, [this](int index) {
        const auto mode = index == 1 ? viewer::ProjectionMode::Orthographic
                                     : viewer::ProjectionMode::Perspective;
        viewport_->setProjectionMode(mode);
        viewerController_->setProjection(mode);
    });
    connect(viewerPanel_, &ViewerPanel::renderModeChanged, this, [this](int index) {
        const auto mode =
            index == 1 ? viewer::RenderMode::Wireframe : viewer::RenderMode::Solid;
        viewport_->setRenderMode(mode);
        viewerController_->setRenderMode(mode);
    });
    connect(viewerPanel_, &ViewerPanel::gridToggled, this, [this](bool on) {
        viewport_->setGridEnabled(on);
        viewerController_->setGrid(on);
    });
    connect(viewerPanel_, &ViewerPanel::axesToggled, this, [this](bool on) {
        viewport_->setAxesEnabled(on);
        viewerController_->setAxes(on);
    });
    connect(viewerPanel_, &ViewerPanel::measureModeToggled, viewport_,
            &ViewportWidget::setMeasureMode);
    connect(viewerPanel_, &ViewerPanel::measureCleared, this, [this]() {
        viewport_->clearMeasurement();
        viewerController_->clearMeasurement();
        onViewerStateChanged();
    });
    connect(viewerPanel_, &ViewerPanel::artifactSelected, this,
            [this](const std::string& id) { onArtifactSelected(id); });
    connect(viewerPanel_, &ViewerPanel::reloadRequested, viewerController_,
            &ViewerController::reloadCurrent);

    mainSplitter->setSizes({340, 640, 340});
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);

    // ---- Bottom: Jobs / Logs / Status ----
    auto* bottomLabel = new QLabel(QStringLiteral("Jobs / Logs / Status"), central);
    bottomLabel->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    layout->addWidget(bottomLabel);

    auto* bottomSplitter = new QSplitter(Qt::Horizontal, central);
    bottomSplitter->setChildrenCollapsible(false);

    jobsTable_ = new QTableWidget(bottomSplitter);
    jobsTable_->setColumnCount(8);
    jobsTable_->setHorizontalHeaderLabels(QStringList()
                                              << "Job ID" << "Engine" << "Operation" << "Status"
                                              << "Created" << "Started" << "Completed" << "Error");
    jobsTable_->horizontalHeader()->setStretchLastSection(true);
    jobsTable_->verticalHeader()->setVisible(false);
    jobsTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    jobsTable_->setMinimumHeight(110);
    jobsTable_->setMinimumWidth(420);

    workflowsTable_ = new QTableWidget(bottomSplitter);
    workflowsTable_->setColumnCount(5);
    workflowsTable_->setHorizontalHeaderLabels(
        QStringList() << "Workflow" << "Nodes" << "Current" << "Status" << "Failures");
    workflowsTable_->horizontalHeader()->setStretchLastSection(true);
    workflowsTable_->verticalHeader()->setVisible(false);
    workflowsTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    workflowsTable_->setMinimumHeight(110);
    workflowsTable_->setMinimumWidth(300);

    logView_ = new QTextEdit(bottomSplitter);
    logView_->setReadOnly(true);
    logView_->setMinimumHeight(110);
    logView_->setPlaceholderText(QStringLiteral("Logs…"));
    bottomSplitter->setSizes({520, 340, 420});
    layout->addWidget(bottomSplitter);

    setCentralWidget(central);

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshJobs);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshWorkflows);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshArtifacts);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshLogs);
    refreshTimer_->start(1000);
}

void MainWindow::refreshAll() {
    refreshJobs();
    refreshWorkflows();
    refreshArtifacts();
    refreshLogs();
    onViewerStateChanged();
}

void MainWindow::handleParse() {
    if (input_ == nullptr || output_ == nullptr) {
        return;
    }
    const std::string request = input_->text().toStdString();

    const trinity::intelligence::RequirementParser parser;
    const trinity::intelligence::ParseResult parsed = parser.parse(request);

    trinity::intelligence::IntentValidator validator;
    trinity::validation::ValidationResult validation;
    if (registry_ != nullptr) {
        validation = validator.validate(parsed.intent, *registry_);
    } else {
        validation = validator.validate(parsed.intent);
    }

    QString report;
    report += QStringLiteral("Original Request:\n") + QString::fromStdString(request) +
              QStringLiteral("\n\n");
    report += QStringLiteral("Parsed Intent:\n") +
              QString::fromStdString(parsed.intent.toJson().dump(2)) + QStringLiteral("\n\n");
    report += QStringLiteral("Validation Status: ") +
              QString::fromStdString(trinity::validation::toString(validation.status)) +
              QStringLiteral(" — ") + QString::fromStdString(validation.message) +
              QStringLiteral("\n");

    if (registry_ != nullptr) {
        const trinity::intelligence::IntentRouter router;
        const trinity::intelligence::RouteResult route =
            router.route(parsed.intent, *registry_);
        report += QStringLiteral("Selected Engine: ") +
                  (route.engine.empty() ? QStringLiteral("(none)")
                                        : QString::fromStdString(route.engine)) +
                  QStringLiteral("  •  Operation: ") +
                  (route.operation.empty() ? QStringLiteral("(none)")
                                           : QString::fromStdString(route.operation)) +
                  QStringLiteral("  •  Status: ") +
                  QString::fromStdString(route.status) + QStringLiteral("\n");
        if (!route.reason.empty()) {
            report += QStringLiteral("Routing Reason: ") +
                      QString::fromStdString(route.reason) + QStringLiteral("\n");
        }
        report += QStringLiteral("Routing Detail:\n") +
                  QString::fromStdString(route.toJson().dump(2)) + QStringLiteral("\n");
    } else {
        report += QStringLiteral("Selected Engine: (registry unavailable)\n");
    }

    report += QStringLiteral("(No engine was executed.)\n");
    output_->setPlainText(report);
}

void MainWindow::handleExecute() {
    if (executeInput_ == nullptr || executeOutput_ == nullptr || pipeline_ == nullptr) {
        return;
    }
    const std::string request = executeInput_->text().toStdString();
    try {
        // Async submit: returns jobId immediately, worker thread executes.
        const auto result = pipeline_->submit(request);
        QString report = QStringLiteral("Request: ") + QString::fromStdString(request) +
                         QStringLiteral("\nJob: ") +
                         QString::fromStdString(result.jobId.empty() ? "(none)" : result.jobId) +
                         QStringLiteral("\nRouted: ") +
                         QString::fromStdString(result.routing.engine + "/" + result.routing.operation) +
                         QStringLiteral(" [") + QString::fromStdString(result.routing.status) +
                         QStringLiteral("]\n");
        if (!result.success && !result.error.is_null()) {
            report += QStringLiteral("Rejected: ") +
                      QString::fromStdString(result.error.dump(2)) + QStringLiteral("\n");
        } else {
            report += QStringLiteral("Lifecycle: QUEUED → RUNNING → COMPLETED/FAILED (polling…)\n");
        }
        executeOutput_->setPlainText(report);
    } catch (const std::exception& exc) {
        executeOutput_->setPlainText(QStringLiteral("Submit failed: ") +
                                     QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handleDemoWorkflow() {
    if (executor_ == nullptr || executeOutput_ == nullptr) {
        return;
    }
    try {
        trinity::workflows::Workflow wf;
        wf.workflowId = trinity::core::newUuid();
        wf.name = "demo-math-2node";
        wf.description = "Node A computes 25*8; Node B consumes A.value via inputFrom.";
        wf.status = trinity::workflows::WorkflowStatus::Queued;
        wf.createdAt = trinity::core::utcNowIso();
        wf.updatedAt = wf.createdAt;

        trinity::workflows::WorkflowNode a;
        a.id = "A";
        a.nodeId = "A";
        a.name = "compute-25x8";
        a.engine = "math";
        a.operation = "evaluate_expression";
        a.parameters = trinity::core::Json{{"expression", "25 * 8"}};
        a.input = a.parameters;

        trinity::workflows::WorkflowNode b;
        b.id = "B";
        b.nodeId = "B";
        b.name = "double-A";
        b.engine = "math";
        b.operation = "evaluate";
        b.parameters = trinity::core::Json{{"expression", "x * 2"}};
        b.input = b.parameters;
        // Explicit structured propagation: B.variables.x <- A.value
        b.inputFrom = trinity::core::Json{{"variables", {{"x", "{{A.value}}"}}}};

        wf.nodes = {a, b};
        trinity::workflows::WorkflowEdge e;
        e.edgeId = trinity::core::newUuid();
        e.fromNode = "A";
        e.toNode = "B";
        wf.edges = {e};

        executor_->save(wf);
        // Off the UI thread: detached worker runs the DAG; UI polls.
        auto* exec = executor_;
        std::thread([exec, wf]() mutable {
            try {
                exec->runInline(wf);
            } catch (...) {
            }
        }).detach();
        executeOutput_->setPlainText(QStringLiteral("Demo workflow submitted: ") +
                                     QString::fromStdString(wf.workflowId) +
                                     QStringLiteral("\nWatch Workflows table for progress."));
    } catch (const std::exception& exc) {
        executeOutput_->setPlainText(QStringLiteral("Workflow submit failed: ") +
                                     QString::fromStdString(exc.what()));
    }
    refreshWorkflows();
}

void MainWindow::refreshJobs() {
    if (jobsTable_ == nullptr || jobs_ == nullptr) {
        return;
    }
    std::vector<trinity::jobs::Job> jobs;
    try {
        jobs = jobs_->listRecent(20);
    } catch (...) {
        return;
    }
    jobsTable_->setRowCount(static_cast<int>(jobs.size()));
    for (int r = 0; r < static_cast<int>(jobs.size()); ++r) {
        const auto& j = jobs[static_cast<size_t>(r)];
        const QString shortId =
            QString::fromStdString(j.jobId.size() > 8 ? j.jobId.substr(0, 8) : j.jobId);
        jobsTable_->setItem(r, 0, new QTableWidgetItem(shortId));
        jobsTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(j.engine)));
        jobsTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(j.operation)));
        jobsTable_->setItem(r, 3,
                            new QTableWidgetItem(QString::fromStdString(toString(j.status))));
        jobsTable_->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(j.createdAt)));
        jobsTable_->setItem(r, 5, new QTableWidgetItem(QString::fromStdString(j.startedAt)));
        jobsTable_->setItem(r, 6, new QTableWidgetItem(QString::fromStdString(j.completedAt)));
        std::string err;
        if (!j.error.is_null()) {
            err = j.error.dump();
            if (err.size() > 120) {
                err = err.substr(0, 120) + "…";
            }
        }
        jobsTable_->setItem(r, 7, new QTableWidgetItem(QString::fromStdString(err)));
    }
    // CAD Engine → Mesh → Validation → ArtifactManager → Viewer (non-blocking:
    // controller loads off the GUI thread; this poll only enqueues).
    if (viewerController_ != nullptr) {
        viewerController_->pollForNewCadJob();
    }
}

void MainWindow::refreshWorkflows() {
    if (workflowsTable_ == nullptr || executor_ == nullptr) {
        return;
    }
    std::vector<trinity::workflows::Workflow> wfs;
    try {
        wfs = executor_->listRecent(10);
    } catch (...) {
        return;
    }
    workflowsTable_->setRowCount(static_cast<int>(wfs.size()));
    for (int r = 0; r < static_cast<int>(wfs.size()); ++r) {
        const auto& w = wfs[static_cast<size_t>(r)];
        workflowsTable_->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(w.name)));
        workflowsTable_->setItem(
            r, 1, new QTableWidgetItem(QString::number(static_cast<qulonglong>(w.nodes.size()))));
        std::string current;
        int failed = 0;
        for (const auto& n : w.nodes) {
            if (n.status == trinity::workflows::NodeStatus::Failed) {
                ++failed;
            }
            if (n.status == trinity::workflows::NodeStatus::Running) {
                current = n.effectiveId();
            }
        }
        if (current.empty()) {
            for (const auto& n : w.nodes) {
                if (n.status == trinity::workflows::NodeStatus::Queued) {
                    current = n.effectiveId();
                    break;
                }
            }
        }
        workflowsTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(current)));
        workflowsTable_->setItem(
            r, 3, new QTableWidgetItem(QString::fromStdString(toString(w.status))));
        workflowsTable_->setItem(r, 4, new QTableWidgetItem(QString::number(failed)));
    }
}

void MainWindow::refreshArtifacts() {
    if (viewerPanel_ == nullptr || jobs_ == nullptr || artifactRepo_ == nullptr) {
        return;
    }
    std::vector<ArtifactRow> rows;
    try {
        const auto jobs = jobs_->listRecent(20);
        for (const auto& job : jobs) {
            std::vector<artifacts::Artifact> list;
            try {
                list = artifactRepo_->listForJob(job.jobId);
            } catch (...) {
                continue;
            }
            std::string state;
            if (job.status == jobs::JobStatus::Completed) {
                state = "VALIDATED";
            } else if (job.status == jobs::JobStatus::Failed ||
                       job.status == jobs::JobStatus::Cancelled) {
                state = "INVALID";
            } else {
                state = "GENERATED";
            }
            for (auto& artifact : list) {
                // Viewer reopen path supports STL + spec JSON only.
                const std::string path = artifact.path;
                const bool viewable =
                    (path.size() >= 4 &&
                     (path.compare(path.size() - 4, 4, ".stl") == 0 ||
                      path.compare(path.size() - 4, 4, ".STL") == 0 ||
                      path.compare(path.size() - 5, 5, ".json") == 0 ||
                      path.compare(path.size() - 5, 5, ".JSON") == 0)) ||
                    artifact.type == "stl" || artifact.type == "json" ||
                    artifact.type == "mesh";
                if (!viewable) {
                    continue;
                }
                rows.push_back(ArtifactRow{artifact, state});
                if (rows.size() >= 50) {
                    break;
                }
            }
            if (rows.size() >= 50) {
                break;
            }
        }
    } catch (...) {
        return;
    }
    viewerPanel_->setArtifacts(rows);
}

void MainWindow::refreshLogs() {
    if (logView_ == nullptr) {
        return;
    }
    try {
        const auto recent = trinity::core::Logger::instance().recent(100);
        QString text;
        text.reserve(4096);
        for (const auto& entry : recent) {
            text += QString::fromStdString(entry.dump());
            text += QChar('\n');
        }
        if (logView_->toPlainText() != text) {
            // Keep the user's scroll position unless they are at the bottom.
            logView_->setPlainText(text);
            logView_->moveCursor(QTextCursor::End);
        }
    } catch (...) {
    }
}

void MainWindow::onViewerStateChanged() {
    if (viewerController_ == nullptr || viewerPanel_ == nullptr || viewport_ == nullptr) {
        return;
    }
    const auto& state = viewerController_->state();
    viewerPanel_->setState(state);
    viewerPanel_->setLoading(state.loading());

    // Upload mesh data only when the displayed model changes — never per frame.
    const std::string shownArtifact = state.hasArtifact() ? state.artifact().artifactId : "";
    const std::string shownJob = viewerController_->currentJobId();
    const bool modelChanged =
        (state.hasMesh() &&
         (shownArtifact != lastShownArtifact_ || shownJob != lastShownJob_)) ||
        (!state.hasMesh() && (!lastShownArtifact_.empty() || !lastShownJob_.empty()));
    if (modelChanged) {
        lastShownArtifact_ = shownArtifact;
        lastShownJob_ = shownJob;
        if (state.hasMesh()) {
            viewport_->setRenderData(state.renderData(), state.boundingBox());
            // Keep ViewerState camera truthful after auto-fit.
            viewerController_->setCamera(viewport_->camera());
            viewerPanel_->setState(viewerController_->state());
        } else {
            viewport_->setRenderData(viewer::RenderData{}, std::nullopt);
        }
    }
}

void MainWindow::onViewerModelReady(const QString& jobId) {
    if (executeOutput_ != nullptr && !jobId.isEmpty()) {
        executeOutput_->append(QStringLiteral("Viewer: mesh ready for job %1").arg(jobId));
    }
    refreshArtifacts();
    onViewerStateChanged();
}

void MainWindow::onViewerLoadError(const QString& message) {
    if (viewerPanel_ != nullptr) {
        viewerPanel_->setErrorText(message);
    }
    if (executeOutput_ != nullptr) {
        executeOutput_->append(QStringLiteral("Viewer error: %1").arg(message));
    }
    onViewerStateChanged();
}

void MainWindow::onArtifactSelected(const std::string& artifactId) {
    if (viewerController_ == nullptr || artifactId.empty()) {
        return;
    }
    viewerController_->openArtifact(artifactId);
}

void MainWindow::onViewportMeasurement(double ax, double ay, double az, double bx,
                                       double by, double bz, bool complete) {
    if (viewerController_ == nullptr) {
        return;
    }
    viewerController_->updateMeasurement(ax, ay, az, bx, by, bz, complete);
}

void MainWindow::onViewportCameraChanged() {
    if (viewerController_ == nullptr || viewport_ == nullptr) {
        return;
    }
    // Sync camera into ViewerState without touching the viewport (no loop).
    viewerController_->setCamera(viewport_->camera());
}

}  // namespace trinity::ui

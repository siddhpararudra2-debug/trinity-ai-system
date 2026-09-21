#include "MainWindow.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <thread>

#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/intelligence/IntentRouter.hpp"
#include "trinity/intelligence/IntentValidator.hpp"
#include "trinity/intelligence/RequirementParser.hpp"
#include "trinity/intelligence/RequestPipeline.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/workflows/Executor.hpp"

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

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("Trinity"));
    resize(900, 1000);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Trinity"), central);
    title->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: 600;"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* subtitle =
        new QLabel(QStringLiteral("Native C++ engineering workspace"), central);
    subtitle->setStyleSheet(QStringLiteral("font-size: 15px; color: #666;"));
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    const QString coreLine =
        summary_.coreOk ? QStringLiteral("Native C++ core initialized successfully")
                        : QStringLiteral("Core initialization FAILED — see logs");
    auto* core = new QLabel(coreLine, central);
    core->setStyleSheet(summary_.coreOk
                            ? QStringLiteral("font-size: 14px; color: #1a7f37;")
                            : QStringLiteral("font-size: 14px; color: #b42318;"));
    core->setAlignment(Qt::AlignCenter);
    core->setWordWrap(true);
    layout->addWidget(core);

    const QString modelText =
        QString::fromStdString(summary_.modelProvider) +
        (summary_.modelAvailable ? QString() : QStringLiteral(" (no model — LLM slot open)"));
    const QString status = QStringLiteral("Status: running  •  Version %1  •  Engines %2  •  Model %3")
                               .arg(QString::fromStdString(summary_.version))
                               .arg(static_cast<qulonglong>(summary_.engineCount))
                               .arg(modelText);
    auto* statusLabel = new QLabel(status, central);
    statusLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #444;"));
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setWordWrap(true);
    layout->addWidget(statusLabel);

    const QString dbLine =
        QStringLiteral("Database: %1").arg(QString::fromStdString(summary_.dbPath));
    auto* dbLabel = new QLabel(dbLine, central);
    dbLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #888;"));
    dbLabel->setAlignment(Qt::AlignCenter);
    dbLabel->setWordWrap(true);
    layout->addWidget(dbLabel);

    auto* enginesTitle = new QLabel(QStringLiteral("Engines (from Registry)"), central);
    enginesTitle->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600;"));
    enginesTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(enginesTitle);

    if (summary_.engines.empty()) {
        auto* none = new QLabel(QStringLiteral("No engines registered"), central);
        none->setAlignment(Qt::AlignCenter);
        layout->addWidget(none);
    } else {
        for (const auto& engine : summary_.engines) {
            const QString line =
                QStringLiteral("%1  •  %2  •  %3")
                    .arg(QString::fromStdString(engine.name).toUpper())
                    .arg(QString::fromStdString(engine.version))
                    .arg(engine.implemented ? QStringLiteral("implemented")
                                            : QStringLiteral("scaffolded/unavailable"));
            auto* row = new QLabel(line, central);
            row->setStyleSheet(engine.implemented
                                   ? QStringLiteral("font-size: 13px; color: #1a7f37;")
                                   : QStringLiteral("font-size: 13px; color: #888;"));
            row->setAlignment(Qt::AlignCenter);
            layout->addWidget(row);
        }
    }

    // Requirement-understanding panel (parse + validate + route only).
    auto* reqTitle = new QLabel(QStringLiteral("Requirement Understanding (no execution)"), central);
    reqTitle->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600;"));
    reqTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(reqTitle);

    input_ = new QLineEdit(central);
    input_->setPlaceholderText(
        QStringLiteral("e.g. Create a 50 mm quadcopter frame with 2 mm thick arms."));
    layout->addWidget(input_);

    auto* parseButton = new QPushButton(QStringLiteral("Parse Requirement"), central);
    layout->addWidget(parseButton);
    connect(parseButton, &QPushButton::clicked, this, &MainWindow::handleParse);
    connect(input_, &QLineEdit::returnPressed, this, &MainWindow::handleParse);

    output_ = new QTextEdit(central);
    output_->setReadOnly(true);
    output_->setMinimumHeight(140);
    output_->setPlaceholderText(
        QStringLiteral("Parsed intent, validation, routing, parameters, missing, errors…"));
    layout->addWidget(output_);

    // Execution panel: full pipeline through JobManager off the UI thread.
    auto* execTitle = new QLabel(QStringLiteral("Execute (Jobs + Workflows)"), central);
    execTitle->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600;"));
    execTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(execTitle);

    executeInput_ = new QLineEdit(central);
    executeInput_->setText(QStringLiteral("Calculate 25 * 8"));
    executeInput_->setPlaceholderText(QStringLiteral("Calculate 25 * 8"));
    layout->addWidget(executeInput_);

    auto* runButton = new QPushButton(QStringLiteral("Submit Request (async)"), central);
    runButton->setEnabled(pipeline_ != nullptr);
    layout->addWidget(runButton);
    connect(runButton, &QPushButton::clicked, this, &MainWindow::handleExecute);
    connect(executeInput_, &QLineEdit::returnPressed, this, &MainWindow::handleExecute);

    auto* demoButton = new QPushButton(QStringLiteral("Run Demo Workflow (2-node math)"), central);
    demoButton->setEnabled(executor_ != nullptr);
    layout->addWidget(demoButton);
    connect(demoButton, &QPushButton::clicked, this, &MainWindow::handleDemoWorkflow);

    executeOutput_ = new QTextEdit(central);
    executeOutput_->setReadOnly(true);
    executeOutput_->setMinimumHeight(120);
    executeOutput_->setPlaceholderText(QStringLiteral("Execution lifecycle: submit → queued → running → completed/failed…"));
    layout->addWidget(executeOutput_);

    auto* jobsTitle = new QLabel(QStringLiteral("Jobs"), central);
    jobsTitle->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600;"));
    jobsTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(jobsTitle);

    jobsTable_ = new QTableWidget(central);
    jobsTable_->setColumnCount(8);
    jobsTable_->setHorizontalHeaderLabels(QStringList()
                                          << "Job ID" << "Engine" << "Operation" << "Status"
                                          << "Created" << "Started" << "Completed" << "Error");
    jobsTable_->horizontalHeader()->setStretchLastSection(true);
    jobsTable_->verticalHeader()->setVisible(false);
    jobsTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    jobsTable_->setMinimumHeight(160);
    layout->addWidget(jobsTable_);

    auto* wfTitle = new QLabel(QStringLiteral("Workflows"), central);
    wfTitle->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 600;"));
    wfTitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(wfTitle);

    workflowsTable_ = new QTableWidget(central);
    workflowsTable_->setColumnCount(5);
    workflowsTable_->setHorizontalHeaderLabels(
        QStringList() << "Workflow" << "Nodes" << "Current" << "Status" << "Failures");
    workflowsTable_->horizontalHeader()->setStretchLastSection(true);
    workflowsTable_->verticalHeader()->setVisible(false);
    workflowsTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    workflowsTable_->setMinimumHeight(120);
    layout->addWidget(workflowsTable_);

    layout->addStretch(1);
    setCentralWidget(central);

    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshJobs);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshWorkflows);
    refreshTimer_->start(1000);
}

void MainWindow::refreshAll() {
    refreshJobs();
    refreshWorkflows();
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

}  // namespace trinity::ui

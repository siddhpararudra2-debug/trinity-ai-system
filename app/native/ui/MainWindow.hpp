#pragma once

// Trinity desktop window (QWidget). Shows product status plus the
// currently registered engines from the Engine Registry, clearly
// distinguishing implemented capabilities from scaffolded ones.
// Also hosts the requirement-understanding panel: a user request is
// parsed into a structured Intent, validated, and routed to an engine
// (lookup only — this UI never executes an engine).

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

class QTableWidget;
class QTimer;

namespace trinity::ui {

class JobTableWidget;
class WorkflowTableWidget;

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

private slots:
    void handleParse();
    void handleExecute();
    void handleDemoWorkflow();
    void refreshJobs();
    void refreshWorkflows();

private:
    void buildUi();
    void refreshAll();

    InitSummary summary_;
    engines::EngineRegistry* registry_ = nullptr;
    jobs::JobManager* jobs_ = nullptr;
    workflows::WorkflowExecutor* executor_ = nullptr;
    intelligence::RequestPipeline* pipeline_ = nullptr;

    QLineEdit* input_ = nullptr;
    QTextEdit* output_ = nullptr;
    QLineEdit* executeInput_ = nullptr;
    QTextEdit* executeOutput_ = nullptr;
    QTableWidget* jobsTable_ = nullptr;
    QTableWidget* workflowsTable_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
};

}  // namespace trinity::ui

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

namespace trinity::ui {

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

private:
    void handleParse();

    InitSummary summary_;
    engines::EngineRegistry* registry_ = nullptr;

    QLineEdit* input_ = nullptr;
    QTextEdit* output_ = nullptr;
};

}  // namespace trinity::ui

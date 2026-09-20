#pragma once

// Trinity desktop window (QWidget). Shows product status plus the
// currently registered engines from the Engine Registry, clearly
// distinguishing implemented capabilities from scaffolded ones.

#include <QMainWindow>

#include <string>
#include <vector>

namespace trinity::ui {

struct EngineEntry {
    std::string name;
    std::string version;
    bool implemented = false;
};

struct InitSummary {
    std::string version = "0.1.0";
    std::string dbPath;
    std::size_t engineCount = 0;
    std::vector<EngineEntry> engines;
    std::string modelProvider = "none";
    bool coreOk = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const InitSummary& summary, QWidget* parent = nullptr);

private:
    InitSummary summary_;
};

}  // namespace trinity::ui

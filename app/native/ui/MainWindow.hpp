#pragma once

// Trinity desktop window (QWidget). Proves the native application
// boots: shows the product name, application status, and that the
// C++ core initialized successfully. Closes cleanly via Qt defaults.

#include <QMainWindow>

#include <string>

namespace trinity::ui {

struct InitSummary {
    std::string version = "0.1.0";
    std::string dbPath;
    std::size_t engineCount = 0;
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

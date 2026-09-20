#include "MainWindow.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace trinity::ui {

MainWindow::MainWindow(const InitSummary& summary, QWidget* parent)
    : QMainWindow(parent), summary_(summary) {
    setWindowTitle(QStringLiteral("Trinity"));
    resize(640, 420);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Trinity"), central);
    title->setStyleSheet(QStringLiteral("font-size: 40px; font-weight: 600;"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto* subtitle = new QLabel(
        QStringLiteral("Native C++ engineering workspace"), central);
    subtitle->setStyleSheet(QStringLiteral("font-size: 15px; color: #666;"));
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    layout->addSpacing(12);

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

    const QString status = QStringLiteral("Status: running  •  Version %1  •  Engines %2  •  Model %3")
                               .arg(QString::fromStdString(summary_.version))
                               .arg(static_cast<qulonglong>(summary_.engineCount))
                               .arg(QString::fromStdString(summary_.modelProvider));
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

    layout->addStretch(1);
    setCentralWidget(central);
}

}  // namespace trinity::ui

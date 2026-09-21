#include "MainWindow.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/intelligence/IntentRouter.hpp"
#include "trinity/intelligence/IntentValidator.hpp"
#include "trinity/intelligence/RequirementParser.hpp"

namespace trinity::ui {

MainWindow::MainWindow(const InitSummary& summary, QWidget* parent)
    : MainWindow(summary, nullptr, parent) {}

MainWindow::MainWindow(const InitSummary& summary, engines::EngineRegistry* registry,
                       QWidget* parent)
    : QMainWindow(parent), summary_(summary), registry_(registry) {
    setWindowTitle(QStringLiteral("Trinity"));
    resize(720, 640);

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

            QString detail;
            if (!engine.capabilities.empty()) {
                QStringList caps;
                for (const auto& cap : engine.capabilities) {
                    caps.push_back(QString::fromStdString(cap));
                }
                detail = QStringLiteral("caps: ") + caps.join(QStringLiteral(", "));
            }
            if (!engine.lastResult.empty()) {
                if (!detail.isEmpty()) {
                    detail += QStringLiteral("  •  ");
                }
                detail += QString::fromStdString(engine.lastResult);
            }
            if (!detail.isEmpty()) {
                auto* sub = new QLabel(detail, central);
                sub->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
                sub->setAlignment(Qt::AlignCenter);
                sub->setWordWrap(true);
                layout->addWidget(sub);
            }
        }
    }

    // Requirement-understanding panel (parse + validate + route only;
    // this UI never executes an engine).
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
    output_->setMinimumHeight(220);
    output_->setPlaceholderText(
        QStringLiteral("Parsed intent, validation, routing, parameters, missing, errors…"));
    layout->addWidget(output_);

    layout->addStretch(1);
    setCentralWidget(central);
}

void MainWindow::handleParse() {
    if (input_ == nullptr || output_ == nullptr) {
        return;
    }
    const std::string request = input_->text().toStdString();

    // Deterministic pipeline: parse -> validate -> route (lookup only).
    // No EngineRegistry::execute, no JobManager::runSync here by design.
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

    report += QStringLiteral("Detected Parameters:\n") +
              QString::fromStdString(parsed.intent.parameters.dump(2)) + QStringLiteral("\n");

    QString missing;
    for (const auto& item : parsed.intent.missing) {
        if (!missing.isEmpty()) {
            missing += QStringLiteral(", ");
        }
        missing += QString::fromStdString(item);
    }
    report += QStringLiteral("Missing Requirements: ") +
              (missing.isEmpty() ? QStringLiteral("(none)") : missing) + QStringLiteral("\n");

    QString errors;
    for (const auto& item : parsed.errors) {
        if (item.rfind("__no_", 0) == 0) {
            continue;  // internal dispatch sentinel, never user-facing
        }
        if (!errors.isEmpty()) {
            errors += QStringLiteral("\n");
        }
        errors += QString::fromStdString(item);
    }
    for (const auto& msg : validation.messages) {
        if (!msg.passed) {
            if (!errors.isEmpty()) {
                errors += QStringLiteral("\n");
            }
            errors += QString::fromStdString("[" + msg.rule + "] " + msg.message);
        }
    }
    report += QStringLiteral("Errors:\n") +
              (errors.isEmpty() ? QStringLiteral("(none)") : errors) + QStringLiteral("\n");
    report += QStringLiteral("(No engine was executed.)\n");

    output_->setPlainText(report);
}

}  // namespace trinity::ui

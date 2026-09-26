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
#include <QComboBox>
#include <QDateTime>
#include <cmath>
#include <thread>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Time.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/engines/FirmwareEngine.hpp"
#include "trinity/jobs/JobWorker.hpp"
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
#include "sim/TimeSeriesWidget.hpp"

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

    // ---- Math (deterministic): structured operation form submitting
    // through the same RequestPipeline -> JobManager -> worker path as
    // the Execute box above (no separate execution system). ----
    auto* mathTitle = new QLabel(QStringLiteral("Math (deterministic)"), leftPane);
    mathTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(mathTitle);

    mathOp_ = new QComboBox(leftPane);
    mathOp_->addItem(QStringLiteral("Evaluate expression"));
    mathOp_->addItem(QStringLiteral("Solve equation"));
    mathOp_->addItem(QStringLiteral("Solve linear (a, b)"));
    mathOp_->addItem(QStringLiteral("Solve quadratic (a, b, c)"));
    mathOp_->addItem(QStringLiteral("Convert units"));
    mathOp_->addItem(QStringLiteral("Engineering formula"));
    leftLayout->addWidget(mathOp_);

    mathExpr_ = new QLineEdit(leftPane);
    mathExpr_->setPlaceholderText(QStringLiteral("Expression (e.g. 2*x + 5, 2*x + 4 = 0)"));
    mathExpr_->setText(QStringLiteral("2 + 3 * 4"));
    leftLayout->addWidget(mathExpr_);

    mathParams_ = new QLineEdit(leftPane);
    mathParams_->setPlaceholderText(QStringLiteral("Variables (e.g. x = 10)"));
    leftLayout->addWidget(mathParams_);

    connect(mathOp_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (mathExpr_ == nullptr || mathParams_ == nullptr) {
                    return;
                }
                // Per-operation hints; the submitter composes the request.
                switch (index) {
                    case 1:
                        mathExpr_->setPlaceholderText(
                            QStringLiteral("Equation (e.g. 2*x + 4 = 0)"));
                        mathParams_->setPlaceholderText(
                            QStringLiteral("Variables, optional (e.g. y = 3)"));
                        break;
                    case 2:
                        mathExpr_->setPlaceholderText(QStringLiteral("(unused)"));
                        mathParams_->setPlaceholderText(
                            QStringLiteral("Coefficients (e.g. a = 2, b = 4)"));
                        break;
                    case 3:
                        mathExpr_->setPlaceholderText(QStringLiteral("(unused)"));
                        mathParams_->setPlaceholderText(
                            QStringLiteral("Coefficients (e.g. a = 1, b = -5, c = 6)"));
                        break;
                    case 4:
                        mathExpr_->setPlaceholderText(
                            QStringLiteral("Value + units (e.g. 10 cm to mm)"));
                        mathParams_->setPlaceholderText(QStringLiteral("(unused)"));
                        break;
                    case 5:
                        mathExpr_->setPlaceholderText(
                            QStringLiteral("Formula name (ohm, power, force)"));
                        mathParams_->setPlaceholderText(
                            QStringLiteral("Inputs (e.g. V = 12, R = 6)"));
                        break;
                    default:
                        mathExpr_->setPlaceholderText(
                            QStringLiteral("Expression (e.g. 2*x + 5, 2*x + 4 = 0)"));
                        mathParams_->setPlaceholderText(
                            QStringLiteral("Variables (e.g. x = 10)"));
                        break;
                }
            });

    auto* mathRunButton =
        new QPushButton(QStringLiteral("Run Math (async)"), leftPane);
    mathRunButton->setEnabled(pipeline_ != nullptr);
    leftLayout->addWidget(mathRunButton);
    connect(mathRunButton, &QPushButton::clicked, this, &MainWindow::handleMathSubmit);
    connect(mathExpr_, &QLineEdit::returnPressed, this, &MainWindow::handleMathSubmit);

    mathOutput_ = new QTextEdit(leftPane);
    mathOutput_->setReadOnly(true);
    mathOutput_->setMinimumHeight(150);
    mathOutput_->setPlaceholderText(QStringLiteral(
        "Math result: expression / variables / operation / result / units / "
        "validation / errors / job id / execution time…"));
    leftLayout->addWidget(mathOutput_);

    // ---- PCB workspace: board/component/net forms submitting structured
    // pcb jobs to the shared worker thread (same JobManager path as the
    // pipeline and workflows). The live design JSON chains between ops.
    auto* pcbTitle = new QLabel(QStringLiteral("PCB (deterministic)"), leftPane);
    pcbTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(pcbTitle);

    auto* boardRow = new QHBoxLayout();
    pcbWidth_ = new QLineEdit(leftPane);
    pcbWidth_->setPlaceholderText(QStringLiteral("W mm (e.g. 50)"));
    pcbWidth_->setText(QStringLiteral("50"));
    pcbHeight_ = new QLineEdit(leftPane);
    pcbHeight_->setPlaceholderText(QStringLiteral("H mm (e.g. 40)"));
    pcbHeight_->setText(QStringLiteral("40"));
    pcbThick_ = new QLineEdit(leftPane);
    pcbThick_->setPlaceholderText(QStringLiteral("Thick (1.6)"));
    boardRow->addWidget(pcbWidth_);
    boardRow->addWidget(pcbHeight_);
    boardRow->addWidget(pcbThick_);
    leftLayout->addLayout(boardRow);
    auto* pcbCreateButton =
        new QPushButton(QStringLiteral("Create Board (async)"), leftPane);
    leftLayout->addWidget(pcbCreateButton);
    connect(pcbCreateButton, &QPushButton::clicked, this, &MainWindow::handlePcbCreate);

    auto* compRow = new QHBoxLayout();
    pcbRef_ = new QLineEdit(leftPane);
    pcbRef_->setPlaceholderText(QStringLiteral("Ref (U1)"));
    pcbValue_ = new QLineEdit(leftPane);
    pcbValue_->setPlaceholderText(QStringLiteral("Value"));
    pcbFootprint_ = new QLineEdit(leftPane);
    pcbFootprint_->setPlaceholderText(QStringLiteral("Footprint"));
    compRow->addWidget(pcbRef_);
    compRow->addWidget(pcbValue_);
    compRow->addWidget(pcbFootprint_);
    leftLayout->addWidget(new QLabel(
        QStringLiteral("Presets: ESP32-WROOM-32, IMU-QFN-24, SOT-223, 0603"), leftPane));
    leftLayout->addLayout(compRow);
    auto* pcbAddCompButton = new QPushButton(QStringLiteral("Add Component"), leftPane);
    leftLayout->addWidget(pcbAddCompButton);
    connect(pcbAddCompButton, &QPushButton::clicked, this,
            &MainWindow::handlePcbAddComponent);

    auto* netRow = new QHBoxLayout();
    pcbNetName_ = new QLineEdit(leftPane);
    pcbNetName_->setPlaceholderText(QStringLiteral("Net (GND)"));
    pcbNetPins_ = new QLineEdit(leftPane);
    pcbNetPins_->setPlaceholderText(QStringLiteral("Pins (U1.19, U2.13)"));
    netRow->addWidget(pcbNetName_);
    netRow->addWidget(pcbNetPins_);
    leftLayout->addLayout(netRow);
    auto* pcbAddNetButton = new QPushButton(QStringLiteral("Add Net"), leftPane);
    leftLayout->addWidget(pcbAddNetButton);
    connect(pcbAddNetButton, &QPushButton::clicked, this, &MainWindow::handlePcbAddNet);

    auto* placeRow = new QHBoxLayout();
    pcbPlaceRef_ = new QLineEdit(leftPane);
    pcbPlaceRef_->setPlaceholderText(QStringLiteral("Ref"));
    pcbPlaceX_ = new QLineEdit(leftPane);
    pcbPlaceX_->setPlaceholderText(QStringLiteral("X mm"));
    pcbPlaceY_ = new QLineEdit(leftPane);
    pcbPlaceY_->setPlaceholderText(QStringLiteral("Y mm"));
    pcbPlaceRot_ = new QLineEdit(leftPane);
    pcbPlaceRot_->setPlaceholderText(QStringLiteral("Rot (0)"));
    placeRow->addWidget(pcbPlaceRef_);
    placeRow->addWidget(pcbPlaceX_);
    placeRow->addWidget(pcbPlaceY_);
    placeRow->addWidget(pcbPlaceRot_);
    leftLayout->addLayout(placeRow);
    auto* pcbPlaceButton = new QPushButton(QStringLiteral("Place Component"), leftPane);
    leftLayout->addWidget(pcbPlaceButton);
    connect(pcbPlaceButton, &QPushButton::clicked, this, &MainWindow::handlePcbPlace);

    auto* validRow = new QHBoxLayout();
    auto* pcbValidateButton = new QPushButton(QStringLiteral("Validate"), leftPane);
    auto* pcbExportButton = new QPushButton(QStringLiteral("Export KiCad"), leftPane);
    validRow->addWidget(pcbValidateButton);
    validRow->addWidget(pcbExportButton);
    leftLayout->addLayout(validRow);
    connect(pcbValidateButton, &QPushButton::clicked, this,
            &MainWindow::handlePcbValidate);
    connect(pcbExportButton, &QPushButton::clicked, this, &MainWindow::handlePcbExport);

    pcbOutput_ = new QTextEdit(leftPane);
    pcbOutput_->setReadOnly(true);
    pcbOutput_->setMinimumHeight(170);
    pcbOutput_->setPlaceholderText(QStringLiteral(
        "PCB: board / components / nets / placements / validation / "
        "artifacts / job / errors…"));
    leftLayout->addWidget(pcbOutput_);

    // ---- Firmware workspace: project JSON chains between ops on the
    // shared worker thread (same JobManager path as PCB/math/workflows).
    auto* fwTitle = new QLabel(QStringLiteral("Firmware (deterministic)"), leftPane);
    fwTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(fwTitle);

    auto* fwNameRow = new QHBoxLayout();
    fwName_ = new QLineEdit(leftPane);
    fwName_->setPlaceholderText(QStringLiteral("Project name"));
    fwName_->setText(QStringLiteral("trinity_fw"));
    auto* fwCreateButton = new QPushButton(QStringLiteral("Create Project"), leftPane);
    fwNameRow->addWidget(fwName_);
    fwNameRow->addWidget(fwCreateButton);
    leftLayout->addLayout(fwNameRow);
    connect(fwCreateButton, &QPushButton::clicked, this, &MainWindow::handleFwCreate);

    auto* fwMcuRow = new QHBoxLayout();
    fwMcu_ = new QComboBox(leftPane);
    for (const auto& model : trinity::engines::FirmwareEngine::mcuNames()) {
        fwMcu_->addItem(QString::fromStdString(model));
    }
    auto* fwSelectButton = new QPushButton(QStringLiteral("Select MCU"), leftPane);
    fwMcuRow->addWidget(fwMcu_, 1);
    fwMcuRow->addWidget(fwSelectButton);
    leftLayout->addLayout(fwMcuRow);
    connect(fwSelectButton, &QPushButton::clicked, this, &MainWindow::handleFwSelectMcu);

    auto* fwPinRow = new QHBoxLayout();
    fwPin_ = new QLineEdit(leftPane);
    fwPin_->setPlaceholderText(QStringLiteral("Pin (GPIO2)"));
    fwFunc_ = new QLineEdit(leftPane);
    fwFunc_->setPlaceholderText(QStringLiteral("Function"));
    fwFunc_->setText(QStringLiteral("status_led"));
    fwDir_ = new QComboBox(leftPane);
    fwDir_->addItems({QStringLiteral("out"), QStringLiteral("in"), QStringLiteral("inout")});
    fwPinRow->addWidget(fwPin_);
    fwPinRow->addWidget(fwFunc_);
    fwPinRow->addWidget(fwDir_);
    leftLayout->addLayout(fwPinRow);
    auto* fwPinButton = new QPushButton(QStringLiteral("Configure Pin"), leftPane);
    leftLayout->addWidget(fwPinButton);
    connect(fwPinButton, &QPushButton::clicked, this, &MainWindow::handleFwConfigurePin);

    auto* fwPeriRow = new QHBoxLayout();
    fwKind_ = new QComboBox(leftPane);
    fwKind_->addItems({QStringLiteral("uart"), QStringLiteral("i2c"),
                       QStringLiteral("pwm")});
    fwPinA_ = new QLineEdit(leftPane);
    fwPinA_->setPlaceholderText(QStringLiteral("TX/SDA/pin"));
    fwPinB_ = new QLineEdit(leftPane);
    fwPinB_->setPlaceholderText(QStringLiteral("RX/SCL"));
    fwParam_ = new QLineEdit(leftPane);
    fwParam_->setPlaceholderText(QStringLiteral("baud/freq"));
    fwParam_->setText(QStringLiteral("115200"));
    fwPeriRow->addWidget(fwKind_);
    fwPeriRow->addWidget(fwPinA_);
    fwPeriRow->addWidget(fwPinB_);
    fwPeriRow->addWidget(fwParam_);
    leftLayout->addLayout(fwPeriRow);
    connect(fwKind_, &QComboBox::currentTextChanged, this, [this](const QString& kind) {
        if (fwPinA_ == nullptr || fwPinB_ == nullptr || fwParam_ == nullptr) {
            return;
        }
        if (kind == QStringLiteral("uart")) {
            fwPinA_->setPlaceholderText(QStringLiteral("TX (GPIO1)"));
            fwPinB_->setPlaceholderText(QStringLiteral("RX (GPIO3)"));
            fwParam_->setPlaceholderText(QStringLiteral("baud"));
            fwParam_->setText(QStringLiteral("115200"));
        } else if (kind == QStringLiteral("i2c")) {
            fwPinA_->setPlaceholderText(QStringLiteral("SDA (GPIO21)"));
            fwPinB_->setPlaceholderText(QStringLiteral("SCL (GPIO22)"));
            fwParam_->setPlaceholderText(QStringLiteral("(unused)"));
            fwParam_->setText(QString());
        } else {
            fwPinA_->setPlaceholderText(QStringLiteral("PWM pin (GPIO18)"));
            fwPinB_->setPlaceholderText(QStringLiteral("(unused)"));
            fwParam_->setPlaceholderText(QStringLiteral("freq Hz"));
            fwParam_->setText(QStringLiteral("1000"));
        }
    });
    auto* fwPeriButton =
        new QPushButton(QStringLiteral("Configure Peripheral"), leftPane);
    leftLayout->addWidget(fwPeriButton);
    connect(fwPeriButton, &QPushButton::clicked, this,
            &MainWindow::handleFwConfigurePeripheral);

    auto* fwActionRow = new QHBoxLayout();
    auto* fwGenerateButton = new QPushButton(QStringLiteral("Generate"), leftPane);
    auto* fwValidateButton = new QPushButton(QStringLiteral("Validate"), leftPane);
    fwProfile_ = new QComboBox(leftPane);
    fwProfile_->addItems({QStringLiteral("debug"), QStringLiteral("release")});
    auto* fwBuildButton = new QPushButton(QStringLiteral("Build"), leftPane);
    fwActionRow->addWidget(fwGenerateButton);
    fwActionRow->addWidget(fwValidateButton);
    fwActionRow->addWidget(fwProfile_);
    fwActionRow->addWidget(fwBuildButton);
    leftLayout->addLayout(fwActionRow);
    connect(fwGenerateButton, &QPushButton::clicked, this, &MainWindow::handleFwGenerate);
    connect(fwValidateButton, &QPushButton::clicked, this, &MainWindow::handleFwValidate);
    connect(fwBuildButton, &QPushButton::clicked, this, &MainWindow::handleFwBuild);

    fwOutput_ = new QTextEdit(leftPane);
    fwOutput_->setReadOnly(true);
    fwOutput_->setMinimumHeight(200);
    fwOutput_->setPlaceholderText(QStringLiteral(
        "Firmware: project / MCU / pins / peripherals / requirements / "
        "validation / generated files / build / artifacts / job / errors…"));
    leftLayout->addWidget(fwOutput_);

    // ---- Simulation workspace: structured sim jobs + 3 time-series charts.
    auto* simTitle = new QLabel(QStringLiteral("Simulation (deterministic)"), leftPane);
    simTitle->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(simTitle);

    simModel_ = new QComboBox(leftPane);
    simModel_->addItem(QStringLiteral("Linear motion"));
    simModel_->addItem(QStringLiteral("Projectile"));
    simModel_->addItem(QStringLiteral("Constant acceleration"));
    simModel_->addItem(QStringLiteral("Dynamics (F=m·a)"));
    leftLayout->addWidget(simModel_);

    auto* simRow1 = new QHBoxLayout();
    simDuration_ = new QLineEdit(leftPane);
    simDuration_->setPlaceholderText(QStringLiteral("Duration s (e.g. 5)"));
    simDuration_->setText(QStringLiteral("5"));
    simVelocity_ = new QLineEdit(leftPane);
    simVelocity_->setPlaceholderText(QStringLiteral("v0 m/s (e.g. 10)"));
    simVelocity_->setText(QStringLiteral("10"));
    simAccel_ = new QLineEdit(leftPane);
    simAccel_->setPlaceholderText(QStringLiteral("a m/s² (e.g. 2)"));
    simAccel_->setText(QStringLiteral("2"));
    simRow1->addWidget(simDuration_);
    simRow1->addWidget(simVelocity_);
    simRow1->addWidget(simAccel_);
    leftLayout->addLayout(simRow1);

    auto* simRow2 = new QHBoxLayout();
    simAngle_ = new QLineEdit(leftPane);
    simAngle_->setPlaceholderText(QStringLiteral("Angle deg (projectile)"));
    simAngle_->setText(QStringLiteral("45"));
    simMass_ = new QLineEdit(leftPane);
    simMass_->setPlaceholderText(QStringLiteral("Mass g (dynamics)"));
    simMass_->setText(QStringLiteral("1000"));
    simForce_ = new QLineEdit(leftPane);
    simForce_->setPlaceholderText(QStringLiteral("Force N (dynamics)"));
    simForce_->setText(QStringLiteral("10"));
    simRow2->addWidget(simAngle_);
    simRow2->addWidget(simMass_);
    simRow2->addWidget(simForce_);
    leftLayout->addLayout(simRow2);

    auto* simRunButton = new QPushButton(QStringLiteral("Run Simulation (async)"), leftPane);
    // worker_ is injected later via setWorkerService(); only the pipeline is
    // known at construction. handleSimRun() guards worker_ at click time.
    simRunButton->setEnabled(pipeline_ != nullptr);
    leftLayout->addWidget(simRunButton);
    connect(simRunButton, &QPushButton::clicked, this, &MainWindow::handleSimRun);

    simOutput_ = new QTextEdit(leftPane);
    simOutput_->setReadOnly(true);
    simOutput_->setMinimumHeight(120);
    simOutput_->setPlaceholderText(QStringLiteral(
        "Simulation: job / method / steps / validation / artifacts / errors…"));
    leftLayout->addWidget(simOutput_);

    simPosChart_ = new TimeSeriesWidget(leftPane);
    simPosChart_->setMinimumHeight(140);
    leftLayout->addWidget(simPosChart_);
    simVelChart_ = new TimeSeriesWidget(leftPane);
    simVelChart_->setMinimumHeight(140);
    leftLayout->addWidget(simVelChart_);
    simAccChart_ = new TimeSeriesWidget(leftPane);
    simAccChart_->setMinimumHeight(140);
    leftLayout->addWidget(simAccChart_);

    // ---- Research workspace: deterministic local index (no web, no LLM).
    auto* researchHeader = new QLabel(QStringLiteral("Research (local index)"), leftPane);
    researchHeader->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(researchHeader);

    researchTitle_ = new QLineEdit(leftPane);
    researchTitle_->setPlaceholderText(QStringLiteral("Document title (for Index)"));
    leftLayout->addWidget(researchTitle_);

    researchText_ = new QTextEdit(leftPane);
    researchText_->setMinimumHeight(60);
    researchText_->setPlaceholderText(QStringLiteral("Document body text (for Index)"));
    leftLayout->addWidget(researchText_);

    researchQuery_ = new QLineEdit(leftPane);
    researchQuery_->setPlaceholderText(QStringLiteral("Query (for Search / Summarize)"));
    leftLayout->addWidget(researchQuery_);

    auto* researchActionRow = new QHBoxLayout();
    auto* researchIndexButton = new QPushButton(QStringLiteral("Index"), leftPane);
    auto* researchSearchButton = new QPushButton(QStringLiteral("Search"), leftPane);
    auto* researchSummarizeButton = new QPushButton(QStringLiteral("Summarize"), leftPane);
    researchActionRow->addWidget(researchIndexButton);
    researchActionRow->addWidget(researchSearchButton);
    researchActionRow->addWidget(researchSummarizeButton);
    leftLayout->addLayout(researchActionRow);
    connect(researchIndexButton, &QPushButton::clicked, this, &MainWindow::handleResearchIndex);
    connect(researchSearchButton, &QPushButton::clicked, this, &MainWindow::handleResearchSearch);
    connect(researchSummarizeButton, &QPushButton::clicked, this,
            &MainWindow::handleResearchSummarize);

    researchOutput_ = new QTextEdit(leftPane);
    researchOutput_->setReadOnly(true);
    researchOutput_->setMinimumHeight(140);
    researchOutput_->setPlaceholderText(QStringLiteral(
        "Research: job / hits / summary / validation / artifacts / errors…"));
    leftLayout->addWidget(researchOutput_);

    // ---- Robotics workspace: deterministic DH kinematics (no physics, no LLM).
    auto* roboticsHeader = new QLabel(QStringLiteral("Robotics (DH kinematics)"), leftPane);
    roboticsHeader->setStyleSheet(QStringLiteral("font-size: 13px; font-weight: 600;"));
    leftLayout->addWidget(roboticsHeader);

    robotName_ = new QLineEdit(leftPane);
    robotName_->setPlaceholderText(QStringLiteral("Robot name (for Export URDF)"));
    leftLayout->addWidget(robotName_);

    dhChain_ = new QTextEdit(leftPane);
    dhChain_->setMinimumHeight(56);
    dhChain_->setPlaceholderText(QStringLiteral(
        "DH params JSON array, optional (default 2-link): "
        "[{\"a\":1,\"alpha\":0,\"d\":0,\"theta_offset\":0}, …]"));
    leftLayout->addWidget(dhChain_);

    jointAngles_ = new QLineEdit(leftPane);
    jointAngles_->setPlaceholderText(QStringLiteral(
        "Joint angles rad, comma-separated (FK), e.g. 0, 0"));
    leftLayout->addWidget(jointAngles_);

    auto* trajectoryRow = new QHBoxLayout();
    startJoint_ = new QLineEdit(leftPane);
    startJoint_->setPlaceholderText(QStringLiteral("Start joints (Trajectory)"));
    goalJoint_ = new QLineEdit(leftPane);
    goalJoint_->setPlaceholderText(QStringLiteral("Goal joints (Trajectory)"));
    duration_ = new QLineEdit(leftPane);
    duration_->setPlaceholderText(QStringLiteral("Duration s"));
    robotDt_ = new QLineEdit(leftPane);
    robotDt_->setPlaceholderText(QStringLiteral("dt s"));
    trajectoryRow->addWidget(startJoint_);
    trajectoryRow->addWidget(goalJoint_);
    trajectoryRow->addWidget(duration_);
    trajectoryRow->addWidget(robotDt_);
    leftLayout->addLayout(trajectoryRow);

    auto* roboticsActionRow = new QHBoxLayout();
    auto* roboticsFkButton = new QPushButton(QStringLiteral("Forward Kinematics"), leftPane);
    auto* roboticsPlanButton = new QPushButton(QStringLiteral("Plan Trajectory"), leftPane);
    auto* roboticsExportButton = new QPushButton(QStringLiteral("Export URDF"), leftPane);
    roboticsActionRow->addWidget(roboticsFkButton);
    roboticsActionRow->addWidget(roboticsPlanButton);
    roboticsActionRow->addWidget(roboticsExportButton);
    leftLayout->addLayout(roboticsActionRow);
    connect(roboticsFkButton, &QPushButton::clicked, this, &MainWindow::handleRoboticsFk);
    connect(roboticsPlanButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsPlan);
    connect(roboticsExportButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsExport);

    auto* roboticsIrHeader = new QLabel(
        QStringLiteral("Robot model (create → state → FK/IK → trajectory)"), leftPane);
    roboticsIrHeader->setStyleSheet(QStringLiteral("font-size: 12px; font-weight: 600;"));
    leftLayout->addWidget(roboticsIrHeader);

    auto* roboticsIrRow = new QHBoxLayout();
    robotLinkCount_ = new QLineEdit(leftPane);
    robotLinkCount_->setPlaceholderText(QStringLiteral("Links (Create)"));
    robotLinkLength_ = new QLineEdit(leftPane);
    robotLinkLength_->setPlaceholderText(QStringLiteral("Link mm (Create)"));
    robotTarget_ = new QLineEdit(leftPane);
    robotTarget_->setPlaceholderText(QStringLiteral("IK target x,y,z m"));
    robotProject_ = new QLineEdit(leftPane);
    robotProject_->setPlaceholderText(QStringLiteral("Project id (auto)"));
    roboticsIrRow->addWidget(robotLinkCount_);
    roboticsIrRow->addWidget(robotLinkLength_);
    roboticsIrRow->addWidget(robotTarget_);
    roboticsIrRow->addWidget(robotProject_);
    leftLayout->addLayout(roboticsIrRow);

    // Joint configuration: type/axis/limits feed create_robot and add_joint.
    auto* roboticsJointCfgRow = new QHBoxLayout();
    robotJointType_ = new QComboBox(leftPane);
    robotJointType_->addItems({QStringLiteral("revolute"), QStringLiteral("prismatic"),
                               QStringLiteral("fixed")});
    robotJointAxis_ = new QLineEdit(leftPane);
    robotJointAxis_->setPlaceholderText(QStringLiteral("Axis x,y,z"));
    robotJointLimits_ = new QLineEdit(leftPane);
    robotJointLimits_->setPlaceholderText(QStringLiteral("Limits low,high (deg or mm)"));
    roboticsJointCfgRow->addWidget(robotJointType_);
    roboticsJointCfgRow->addWidget(robotJointAxis_);
    roboticsJointCfgRow->addWidget(robotJointLimits_);
    leftLayout->addLayout(roboticsJointCfgRow);

    // Model authoring: link/joint names and parent/child for add_link/add_joint.
    auto* roboticsNamesRow = new QHBoxLayout();
    robotNewLinkName_ = new QLineEdit(leftPane);
    robotNewLinkName_->setPlaceholderText(QStringLiteral("Link name (Add Link)"));
    robotNewJointName_ = new QLineEdit(leftPane);
    robotNewJointName_->setPlaceholderText(QStringLiteral("Joint name (Add Joint)"));
    robotParentLink_ = new QLineEdit(leftPane);
    robotParentLink_->setPlaceholderText(QStringLiteral("Parent link"));
    robotChildLink_ = new QLineEdit(leftPane);
    robotChildLink_->setPlaceholderText(QStringLiteral("Child link"));
    roboticsNamesRow->addWidget(robotNewLinkName_);
    roboticsNamesRow->addWidget(robotNewJointName_);
    roboticsNamesRow->addWidget(robotParentLink_);
    roboticsNamesRow->addWidget(robotChildLink_);
    leftLayout->addLayout(roboticsNamesRow);

    auto* roboticsModelActions = new QHBoxLayout();
    auto* roboticsAddLinkButton = new QPushButton(QStringLiteral("Add Link"), leftPane);
    auto* roboticsAddJointButton = new QPushButton(QStringLiteral("Add Joint"), leftPane);
    roboticsModelActions->addWidget(roboticsAddLinkButton);
    roboticsModelActions->addWidget(roboticsAddJointButton);
    leftLayout->addLayout(roboticsModelActions);
    connect(roboticsAddLinkButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsAddLink);
    connect(roboticsAddJointButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsAddJoint);

    auto* roboticsIrActions = new QHBoxLayout();
    auto* roboticsCreateButton = new QPushButton(QStringLiteral("Create Robot"), leftPane);
    auto* roboticsStateButton = new QPushButton(QStringLiteral("Set Joint State"), leftPane);
    auto* roboticsIrFkButton = new QPushButton(QStringLiteral("Compute FK"), leftPane);
    auto* roboticsIkButton = new QPushButton(QStringLiteral("Solve IK"), leftPane);
    auto* roboticsGenButton = new QPushButton(QStringLiteral("Generate Trajectory"), leftPane);
    auto* roboticsValidateButton = new QPushButton(QStringLiteral("Validate Robot"), leftPane);
    roboticsIrActions->addWidget(roboticsCreateButton);
    roboticsIrActions->addWidget(roboticsStateButton);
    roboticsIrActions->addWidget(roboticsIrFkButton);
    roboticsIrActions->addWidget(roboticsIkButton);
    roboticsIrActions->addWidget(roboticsGenButton);
    roboticsIrActions->addWidget(roboticsValidateButton);
    leftLayout->addLayout(roboticsIrActions);
    connect(roboticsCreateButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsCreate);
    connect(roboticsStateButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsSetState);
    connect(roboticsIrFkButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsComputeFk);
    connect(roboticsIkButton, &QPushButton::clicked, this, &MainWindow::handleRoboticsIk);
    connect(roboticsGenButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsGenerate);
    connect(roboticsValidateButton, &QPushButton::clicked, this,
            &MainWindow::handleRoboticsValidate);

    roboticsOutput_ = new QTextEdit(leftPane);
    roboticsOutput_->setReadOnly(true);
    roboticsOutput_->setMinimumHeight(140);
    roboticsOutput_->setPlaceholderText(QStringLiteral(
        "Robotics: job / end effector / trajectory / URDF path / validation / errors…"));
    leftLayout->addWidget(roboticsOutput_);

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

void MainWindow::handleMathSubmit() {
    if (mathOp_ == nullptr || mathExpr_ == nullptr || mathParams_ == nullptr ||
        mathOutput_ == nullptr || pipeline_ == nullptr) {
        return;
    }
    const int op = mathOp_->currentIndex();
    const std::string expr = mathExpr_->text().trimmed().toStdString();
    const std::string params = mathParams_->text().trimmed().toStdString();
    std::string request;
    switch (op) {
        case 1:  // Solve equation (generic string solver).
            request = "Solve " + expr;
            if (!params.empty()) {
                request += " with " + params;
            }
            break;
        case 2:  // solve_linear {a, b}.
            request = "Solve linear with " + params;
            break;
        case 3:  // solve_quadratic {a, b, c}.
            request = "Solve quadratic with " + params;
            break;
        case 4:  // convert value/from/to.
            request = "Convert " + expr;
            break;
        case 5:  // formula name + inputs.
            request = "Formula " + expr + " with " + params;
            break;
        default:  // Evaluate expression, optional variables.
            request = "Calculate " + expr;
            if (!params.empty()) {
                request += " with " + params;
            }
            break;
    }
    try {
        // Async submit: the worker thread runs
        // JobManager -> EngineRegistry -> MathEngine; the 1 s refresh
        // timer renders the persisted job below without blocking the UI.
        const auto result = pipeline_->submit(request);
        if (!result.success || result.jobId.empty()) {
            QString report = QStringLiteral("Math rejected:\n") +
                             QString::fromStdString(request) + QStringLiteral("\n");
            if (!result.error.is_null()) {
                report += QString::fromStdString(result.error.dump(2));
            }
            mathOutput_->setPlainText(report);
            lastMathJobId_.clear();
            return;
        }
        lastMathJobId_ = result.jobId;
        mathOutput_->setPlainText(QStringLiteral("Math submitted:\n") +
                                  QString::fromStdString(request) +
                                  QStringLiteral("\nJob: ") +
                                  QString::fromStdString(result.jobId) +
                                  QStringLiteral("\nPolling for completion…"));
    } catch (const std::exception& exc) {
        mathOutput_->setPlainText(QStringLiteral("Math submit failed: ") +
                                  QString::fromStdString(exc.what()));
        lastMathJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::refreshMathResult() {
    if (mathOutput_ == nullptr || jobs_ == nullptr || lastMathJobId_.empty()) {
        return;
    }
    trinity::jobs::Job job;
    try {
        job = jobs_->get(lastMathJobId_);
    } catch (...) {
        return;
    }
    const QString shortId = QString::fromStdString(
        job.jobId.size() > 8 ? job.jobId.substr(0, 8) : job.jobId);
    QString report =
        QStringLiteral("Job: %1  •  %2/%3  •  %4\n")
            .arg(shortId, QString::fromStdString(job.engine),
                 QString::fromStdString(job.operation),
                 QString::fromStdString(toString(job.status)));
    if (!job.input.is_null() && !job.input.empty()) {
        report += QStringLiteral("Input: ") +
                  QString::fromStdString(job.input.dump()) + QStringLiteral("\n");
    }
    if (!job.result.is_null() && job.result.is_object()) {
        const auto& envelope = job.result;
        if (envelope.contains("result")) {
            report += QStringLiteral("Result: ") +
                      QString::fromStdString(envelope["result"].dump(2)) +
                      QStringLiteral("\n");
        }
        if (envelope.contains("validation") && !envelope["validation"].is_null()) {
            const auto& v = envelope["validation"];
            report += QStringLiteral("Validation: ") +
                      QString::fromStdString(v.value("status", "?")) +
                      QStringLiteral(" — ") +
                      QString::fromStdString(v.value("message", "")) +
                      QStringLiteral("\n");
        }
        if (envelope.contains("errors") && envelope["errors"].is_array() &&
            !envelope["errors"].empty()) {
            std::string errs = envelope["errors"].dump(2);
            if (errs.size() > 600) {
                errs = errs.substr(0, 600) + "…";
            }
            report += QStringLiteral("Errors: ") + QString::fromStdString(errs) +
                      QStringLiteral("\n");
        }
    }
    if (!job.error.is_null()) {
        std::string err = job.error.dump();
        if (err.size() > 300) {
            err = err.substr(0, 300) + "…";
        }
        report += QStringLiteral("Job error: ") + QString::fromStdString(err) +
                  QStringLiteral("\n");
    }
    // Execution time from persisted lifecycle timestamps.
    const QDateTime started =
        QDateTime::fromString(QString::fromStdString(job.startedAt), Qt::ISODate);
    const QDateTime completed =
        QDateTime::fromString(QString::fromStdString(job.completedAt), Qt::ISODate);
    QString timing = QStringLiteral("Started: %1  Completed: %2")
                         .arg(QString::fromStdString(job.startedAt),
                              QString::fromStdString(job.completedAt.empty() ? "—"
                                                                             : job.completedAt));
    if (started.isValid() && completed.isValid()) {
        timing += QStringLiteral("  (%1 ms)").arg(started.msecsTo(completed));
    }
    report += timing;
    if (mathOutput_->toPlainText() != report) {
        mathOutput_->setPlainText(report);
    }
}

// --- Simulation workspace: structured sim jobs on the shared worker.

namespace {

bool simNumber(const QString& text, double& valueOut) {
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return false;
    }
    valueOut = value;
    return true;
}

std::string simOperationForIndex(int index) {
    switch (index) {
        case 1:
            return "simulate_projectile";
        case 2:
            return "simulate_constant_acceleration";
        case 3:
            return "simulate_dynamics";
        default:
            return "simulate_linear_motion";
    }
}

}  // namespace

void MainWindow::handleSimRun() {
    if (simModel_ == nullptr || simDuration_ == nullptr || simOutput_ == nullptr ||
        worker_ == nullptr) {
        return;
    }
    // Validation errors must stay visible: clearing lastSimJobId_ stops the
    // refresh timer from overwriting the message with the previous job's
    // report on the next tick.
    const auto fail = [this](const QString& message) {
        lastSimJobId_.clear();
        simOutput_->setPlainText(message);
    };
    double duration = 0.0;
    if (!simNumber(simDuration_->text(), duration) || duration <= 0.0) {
        fail(QStringLiteral("Simulation: duration_s must be a positive number"));
        return;
    }
    const std::string op = simOperationForIndex(simModel_->currentIndex());
    trinity::core::Json params = {{"duration_s", duration}, {"dt", 0.01}};

    if (op == "simulate_linear_motion" || op == "simulate_constant_acceleration") {
        double v0 = 0.0;
        if (simVelocity_ != nullptr && !simVelocity_->text().trimmed().isEmpty()) {
            if (!simNumber(simVelocity_->text(), v0)) {
                fail(QStringLiteral("Simulation: velocity must be a number"));
                return;
            }
        }
        params["initial_velocity_m_s"] = v0;
        double accel = 0.0;
        if (simAccel_ != nullptr && !simAccel_->text().trimmed().isEmpty()) {
            if (!simNumber(simAccel_->text(), accel)) {
                fail(QStringLiteral("Simulation: acceleration must be a number"));
                return;
            }
        }
        if (op == "simulate_constant_acceleration" && accel == 0.0) {
            fail(QStringLiteral("Simulation: constant acceleration requires a non-zero a"));
            return;
        }
        params["acceleration_m_s2"] = accel;
    } else if (op == "simulate_projectile") {
        double v0 = 0.0;
        if (simVelocity_ == nullptr || !simNumber(simVelocity_->text(), v0) || v0 <= 0.0) {
            fail(QStringLiteral("Simulation: projectile requires a positive initial velocity"));
            return;
        }
        double angle = 45.0;
        if (simAngle_ != nullptr && !simAngle_->text().trimmed().isEmpty()) {
            if (!simNumber(simAngle_->text(), angle)) {
                fail(QStringLiteral("Simulation: angle must be a number"));
                return;
            }
        }
        params["initial_velocity_m_s"] = v0;
        params["launch_angle_deg"] = angle;
    } else {
        double massG = 0.0;
        double forceN = 0.0;
        if (simMass_ == nullptr || !simNumber(simMass_->text(), massG) || massG <= 0.0) {
            fail(QStringLiteral("Simulation: dynamics requires a positive mass in grams"));
            return;
        }
        if (simForce_ == nullptr || !simNumber(simForce_->text(), forceN)) {
            fail(QStringLiteral("Simulation: dynamics requires a force in N"));
            return;
        }
        params["mass_g"] = massG;
        params["force_N"] = forceN;
        double v0 = 0.0;
        if (simVelocity_ != nullptr && !simVelocity_->text().trimmed().isEmpty()) {
            if (simNumber(simVelocity_->text(), v0)) {
                params["initial_velocity_m_s"] = v0;
            }
        }
    }

    try {
        lastSimJobId_ = worker_->submit("simulation", op, params);
        simOutput_->setPlainText(QStringLiteral("Simulation submitted (%1), job %2…")
                                     .arg(QString::fromStdString(op),
                                          QString::fromStdString(lastSimJobId_)));
        if (simPosChart_ != nullptr) simPosChart_->clear();
        if (simVelChart_ != nullptr) simVelChart_->clear();
        if (simAccChart_ != nullptr) simAccChart_->clear();
    } catch (const std::exception& exc) {
        simOutput_->setPlainText(QStringLiteral("Simulation submit failed: ") +
                                 QString::fromStdString(exc.what()));
        lastSimJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::refreshSimResult() {
    if (simOutput_ == nullptr || jobs_ == nullptr || lastSimJobId_.empty()) {
        return;
    }
    trinity::jobs::Job job;
    try {
        job = jobs_->get(lastSimJobId_);
    } catch (...) {
        return;
    }
    const QString shortId = QString::fromStdString(
        job.jobId.size() > 8 ? job.jobId.substr(0, 8) : job.jobId);
    QString report =
        QStringLiteral("Job: %1  •  %2/%3  •  %4\n")
            .arg(shortId, QString::fromStdString(job.engine),
                 QString::fromStdString(job.operation),
                 QString::fromStdString(toString(job.status)));

    if (!job.result.is_null() && job.result.is_object()) {
        const auto& result = job.result;
        if (result.contains("result") && result["result"].is_object()) {
            const auto& sim = result["result"];
            report += QStringLiteral("Method: %1  Steps: %2  Samples: %3  Downsampled: %4\n")
                          .arg(QString::fromStdString(sim.value("method", "")),
                               QString::number(sim.value("step_count", 0LL)),
                               QString::number(sim.value("sample_count", 0LL)),
                               sim.value("downsampled", false) ? QStringLiteral("yes")
                                                               : QStringLiteral("no"));
            if (result.contains("checks") && result["checks"].is_object()) {
                const auto& checks = result["checks"];
                report += QStringLiteral("Checks: ok=%1 — %2\n")
                              .arg(checks.value("ok", false) ? QStringLiteral("true")
                                                             : QStringLiteral("false"),
                                   QString::fromStdString(checks.value("message", "")));
            }
            if (sim.contains("samples") && sim["samples"].is_array()) {
                std::vector<SeriesPoint> pos;
                std::vector<SeriesPoint> vel;
                std::vector<SeriesPoint> acc;
                const auto& samples = sim["samples"];
                const size_t n = samples.size();
                const size_t stride = n > 4000 ? (n / 4000) : 1;
                pos.reserve(n / stride + 1);
                vel.reserve(n / stride + 1);
                acc.reserve(n / stride + 1);
                for (size_t i = 0; i < n; i += stride) {
                    const auto& s = samples[i];
                    const double t = s.value("t", 0.0);
                    pos.push_back({t, s["position"].value("x", 0.0)});
                    vel.push_back({t, s["velocity"].value("x", 0.0)});
                    acc.push_back({t, s["acceleration"].value("x", 0.0)});
                }
                if (n > 0 && stride > 1) {
                    const auto& last = samples[n - 1];
                    const double t = last.value("t", 0.0);
                    pos.push_back({t, last["position"].value("x", 0.0)});
                    vel.push_back({t, last["velocity"].value("x", 0.0)});
                    acc.push_back({t, last["acceleration"].value("x", 0.0)});
                }
                if (simPosChart_ != nullptr) {
                    simPosChart_->setSeries(pos, QStringLiteral("Position x (m)"),
                                            QStringLiteral("t (s)"),
                                            QStringLiteral("m"));
                }
                if (simVelChart_ != nullptr) {
                    simVelChart_->setSeries(vel, QStringLiteral("Velocity x (m/s)"),
                                            QStringLiteral("t (s)"),
                                            QStringLiteral("m/s"));
                }
                if (simAccChart_ != nullptr) {
                    simAccChart_->setSeries(acc, QStringLiteral("Acceleration x (m/s²)"),
                                            QStringLiteral("t (s)"),
                                            QStringLiteral("m/s²"));
                }
            }
        }
        if (result.contains("checks") && !result.contains("result")) {
            report += QStringLiteral("Checks: ") +
                      QString::fromStdString(result["checks"].dump(2)) + QStringLiteral("\n");
        }
    }
    if (!job.error.is_null()) {
        std::string err = job.error.dump();
        if (err.size() > 300) {
            err = err.substr(0, 300) + "…";
        }
        report += QStringLiteral("Error: ") + QString::fromStdString(err) + QStringLiteral("\n");
    }
    if (simOutput_->toPlainText() != report) {
        simOutput_->setPlainText(report);
    }
}

// --- Research workspace: deterministic local index on the shared worker.
// Empty inputs are reported inline without submitting a job.
void MainWindow::handleResearchIndex() {
    if (researchTitle_ == nullptr || researchText_ == nullptr || researchOutput_ == nullptr ||
        worker_ == nullptr) {
        return;
    }
    const QString title = researchTitle_->text().trimmed();
    const QString body = researchText_->toPlainText().trimmed();
    if (title.isEmpty() || body.isEmpty()) {
        researchOutput_->setPlainText(
            QStringLiteral("Research: index requires a document title and text"));
        return;
    }
    trinity::core::Json params = {{"title", title.toStdString()},
                                  {"text", body.toStdString()}};
    try {
        lastResearchJobId_ = worker_->submit("research", "index_document", params);
        researchOutput_->setPlainText(
            QStringLiteral("Index submitted (%1), job %2…")
                .arg(QStringLiteral("index_document"),
                     QString::fromStdString(lastResearchJobId_)));
    } catch (const std::exception& exc) {
        researchOutput_->setPlainText(QStringLiteral("Research index submit failed: ") +
                                      QString::fromStdString(exc.what()));
        lastResearchJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleResearchSearch() {
    if (researchQuery_ == nullptr || researchOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const QString query = researchQuery_->text().trimmed();
    if (query.isEmpty()) {
        researchOutput_->setPlainText(QStringLiteral("Research: search requires a query"));
        return;
    }
    trinity::core::Json params = {{"query", query.toStdString()}};
    try {
        lastResearchJobId_ = worker_->submit("research", "search", params);
        researchOutput_->setPlainText(
            QStringLiteral("Search submitted (%1), job %2…")
                .arg(QStringLiteral("search"), QString::fromStdString(lastResearchJobId_)));
    } catch (const std::exception& exc) {
        researchOutput_->setPlainText(QStringLiteral("Research search submit failed: ") +
                                      QString::fromStdString(exc.what()));
        lastResearchJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleResearchSummarize() {
    if (researchQuery_ == nullptr || researchOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const QString query = researchQuery_->text().trimmed();
    if (query.isEmpty()) {
        researchOutput_->setPlainText(QStringLiteral("Research: summarize requires a query"));
        return;
    }
    trinity::core::Json params = {{"query", query.toStdString()}};
    try {
        lastResearchJobId_ = worker_->submit("research", "summarize_results", params);
        researchOutput_->setPlainText(
            QStringLiteral("Summarize submitted (%1), job %2…")
                .arg(QStringLiteral("summarize_results"),
                     QString::fromStdString(lastResearchJobId_)));
    } catch (const std::exception& exc) {
        researchOutput_->setPlainText(QStringLiteral("Research summarize submit failed: ") +
                                      QString::fromStdString(exc.what()));
        lastResearchJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::refreshResearchResult() {
    if (researchOutput_ == nullptr || jobs_ == nullptr || lastResearchJobId_.empty()) {
        return;
    }
    trinity::jobs::Job job;
    try {
        job = jobs_->get(lastResearchJobId_);
    } catch (...) {
        return;
    }
    const QString shortId = QString::fromStdString(
        job.jobId.size() > 8 ? job.jobId.substr(0, 8) : job.jobId);
    QString report =
        QStringLiteral("Job: %1  •  %2/%3  •  %4\n")
            .arg(shortId, QString::fromStdString(job.engine),
                 QString::fromStdString(job.operation),
                 QString::fromStdString(toString(job.status)));

    if (!job.result.is_null() && job.result.is_object()) {
        const auto& envelope = job.result;
        if (envelope.contains("result") && envelope["result"].is_object()) {
            const auto& res = envelope["result"];
            if (res.contains("hit_count")) {
                report += QStringLiteral("Hits: %1 / %2 documents\n")
                              .arg(res.value("hit_count", 0LL))
                              .arg(res.value("total_documents", 0LL));
                if (res.contains("hits") && res["hits"].is_array()) {
                    const auto& hits = res["hits"];
                    const size_t shown = hits.size() < 10 ? hits.size() : 10;
                    for (size_t i = 0; i < shown; ++i) {
                        const auto& hit = hits[i];
                        report += QStringLiteral("  #%1 %2  score=%3  «%4»\n      %5\n")
                                      .arg(hit.value("rank", 0LL))
                                      .arg(QString::fromStdString(hit.value("doc_id", "")))
                                      .arg(hit.value("score", 0.0), 0, 'f', 4)
                                      .arg(QString::fromStdString(hit.value("title", "")))
                                      .arg(QString::fromStdString(hit.value("snippet", "")));
                    }
                }
            }
            if (res.contains("summary") && res["summary"].is_array()) {
                report += QStringLiteral("Summary sentences: %1\n")
                              .arg(static_cast<qint64>(res["summary"].size()));
                for (const auto& line : res["summary"]) {
                    report += QStringLiteral("  [%1] %2\n")
                                  .arg(QString::fromStdString(line.value("title", "")))
                                  .arg(QString::fromStdString(line.value("sentence", "")));
                }
            }
            if (res.contains("document_count") && !res.contains("hit_count") &&
                !res.contains("summary")) {
                report += QStringLiteral("Result: ") +
                          QString::fromStdString(res.dump(2)) + QStringLiteral("\n");
            } else if (!res.contains("hit_count") && !res.contains("summary")) {
                report += QStringLiteral("Result: ") +
                          QString::fromStdString(res.dump(2)) + QStringLiteral("\n");
            }
            if (res.contains("index_path")) {
                report += QStringLiteral("Artifact: ") +
                          QString::fromStdString(res.value("index_path", "")) +
                          QStringLiteral("\n");
            }
        }
        if (envelope.contains("validation") && !envelope["validation"].is_null()) {
            const auto& v = envelope["validation"];
            report += QStringLiteral("Validation: ") +
                      QString::fromStdString(v.value("status", "?")) + QStringLiteral(" — ") +
                      QString::fromStdString(v.value("message", "")) + QStringLiteral("\n");
        }
        if (envelope.contains("errors") && envelope["errors"].is_array() &&
            !envelope["errors"].empty()) {
            std::string errs = envelope["errors"].dump(2);
            if (errs.size() > 600) {
                errs = errs.substr(0, 600) + "…";
            }
            report += QStringLiteral("Errors: ") + QString::fromStdString(errs) +
                      QStringLiteral("\n");
        }
    }
    if (!job.error.is_null()) {
        std::string err = job.error.dump();
        if (err.size() > 300) {
            err = err.substr(0, 300) + "…";
        }
        report += QStringLiteral("Job error: ") + QString::fromStdString(err) +
                  QStringLiteral("\n");
    }
    if (researchOutput_->toPlainText() != report) {
        researchOutput_->setPlainText(report);
    }
}

// --- Robotics workspace: deterministic DH kinematics on the shared worker.
// Empty or malformed inputs are reported inline without submitting a job.
namespace {

bool roboticsNumberList(const QString& text, std::vector<double>& valuesOut) {
    valuesOut.clear();
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true;  // optional input left blank
    }
    const QStringList parts = trimmed.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const double value = part.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            return false;
        }
        valuesOut.push_back(value);
    }
    return !valuesOut.empty();
}

trinity::core::Json roboticsJsonArray(const std::vector<double>& values) {
    trinity::core::Json arr = trinity::core::Json::array();
    for (const double value : values) {
        arr.push_back(value);
    }
    return arr;
}

}  // namespace

void MainWindow::handleRoboticsFk() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    trinity::core::Json params = trinity::core::Json::object();
    const QString chainText = dhChain_ != nullptr ? dhChain_->toPlainText().trimmed() : QString();
    if (!chainText.isEmpty()) {
        try {
            const trinity::core::Json chain = trinity::core::Json::parse(chainText.toStdString());
            if (!chain.is_array() || chain.empty()) {
                roboticsOutput_->setPlainText(
                    QStringLiteral("Robotics: DH params must be a non-empty JSON array"));
                return;
            }
            params["dh_params"] = chain;
        } catch (const std::exception& exc) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: DH params are not valid JSON: ") +
                QString::fromStdString(exc.what()));
            return;
        }
    }
    std::vector<double> angles;
    if (jointAngles_ != nullptr &&
        !roboticsNumberList(jointAngles_->text(), angles)) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: joint angles must be comma-separated numbers"));
        return;
    }
    if (!angles.empty()) {
        params["joint_angles"] = roboticsJsonArray(angles);
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "forward_kinematics", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("Forward kinematics submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics FK submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsPlan() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    std::vector<double> start;
    std::vector<double> goal;
    if (startJoint_ == nullptr || goalJoint_ == nullptr ||
        !roboticsNumberList(startJoint_->text(), start) || start.empty() ||
        !roboticsNumberList(goalJoint_->text(), goal) || goal.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: trajectory requires start and goal joint lists"));
        return;
    }
    if (start.size() != goal.size()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: start and goal joint lists must match in length"));
        return;
    }
    trinity::core::Json params = {{"joint_start", roboticsJsonArray(start)},
                                  {"joint_goal", roboticsJsonArray(goal)}};
    if (duration_ != nullptr && !duration_->text().trimmed().isEmpty()) {
        bool ok = false;
        const double seconds = duration_->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(seconds) || seconds <= 0.0) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: duration must be a positive number of seconds"));
            return;
        }
        params["duration_s"] = seconds;
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "plan_trajectory", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("Trajectory submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics trajectory submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsExport() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    trinity::core::Json params = trinity::core::Json::object();
    const QString chainText = dhChain_ != nullptr ? dhChain_->toPlainText().trimmed() : QString();
    if (!chainText.isEmpty()) {
        try {
            const trinity::core::Json chain = trinity::core::Json::parse(chainText.toStdString());
            if (!chain.is_array() || chain.empty()) {
                roboticsOutput_->setPlainText(
                    QStringLiteral("Robotics: DH params must be a non-empty JSON array"));
                return;
            }
            params["dh_params"] = chain;
        } catch (const std::exception& exc) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: DH params are not valid JSON: ") +
                QString::fromStdString(exc.what()));
            return;
        }
    }
    if (robotName_ != nullptr && !robotName_->text().trimmed().isEmpty()) {
        params["robot_name"] = robotName_->text().trimmed().toStdString();
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "export_urdf", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("URDF export submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics export submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

// --- Robotics IR model chain: project id captured from results chains
// create -> state -> FK/IK -> trajectory -> validate. Submit failures
// clear the pending job id so the poller cannot report a stale job.
std::string MainWindow::roboticsProjectId() const {
    const QString overrideText =
        robotProject_ != nullptr ? robotProject_->text().trimmed() : QString();
    if (!overrideText.isEmpty()) {
        return overrideText.toStdString();
    }
    return hasRoboticsProject_ ? lastRoboticsProjectId_ : std::string();
}

void MainWindow::handleRoboticsCreate() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    trinity::core::Json params = trinity::core::Json::object();
    if (robotName_ != nullptr && !robotName_->text().trimmed().isEmpty()) {
        params["robot_name"] = robotName_->text().trimmed().toStdString();
    }
    bool ok = false;
    const int links =
        robotLinkCount_ != nullptr ? robotLinkCount_->text().trimmed().toInt(&ok) : 0;
    if (!ok || links < 1) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: Create needs a link count of 1 or more"));
        return;
    }
    params["link_count"] = links;
    if (robotLinkLength_ != nullptr && !robotLinkLength_->text().trimmed().isEmpty()) {
        bool lenOk = false;
        const double mm = robotLinkLength_->text().trimmed().toDouble(&lenOk);
        if (!lenOk || !std::isfinite(mm) || mm < 0.0) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: link length must be a non-negative number (mm)"));
            return;
        }
        params["link_length_mm"] = mm;
    }
    QString configError;
    if (!roboticsJointConfig(params, configError)) {
        roboticsOutput_->setPlainText(configError);
        return;
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "create_robot", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("Robot create submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics create submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsSetState() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string projectId = roboticsProjectId();
    if (projectId.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: create a robot first (no project id)"));
        return;
    }
    std::vector<double> angles;
    if (jointAngles_ == nullptr || !roboticsNumberList(jointAngles_->text(), angles) ||
        angles.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: joint state needs comma-separated joint values"));
        return;
    }
    try {
        lastRoboticsJobId_ = worker_->submit(
            "robotics", "set_joint_state",
            {{"project_id", projectId}, {"joint_positions", roboticsJsonArray(angles)}});
        roboticsOutput_->setPlainText(
            QStringLiteral("Joint state submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics state submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsComputeFk() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string projectId = roboticsProjectId();
    if (projectId.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: create a robot first (no project id)"));
        return;
    }
    trinity::core::Json params = {{"project_id", projectId}};
    std::vector<double> angles;
    if (jointAngles_ != nullptr && !jointAngles_->text().trimmed().isEmpty()) {
        if (!roboticsNumberList(jointAngles_->text(), angles)) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: joint angles must be comma-separated numbers"));
            return;
        }
        if (!angles.empty()) {
            params["joint_positions"] = roboticsJsonArray(angles);
        }
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "compute_forward_kinematics", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("Model FK submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics FK submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsIk() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string projectId = roboticsProjectId();
    if (projectId.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: create a robot first (no project id)"));
        return;
    }
    std::vector<double> target;
    if (robotTarget_ == nullptr || !roboticsNumberList(robotTarget_->text(), target) ||
        target.size() != 3) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: IK needs a target of 3 numbers (x,y,z in meters)"));
        return;
    }
    try {
        lastRoboticsJobId_ = worker_->submit(
            "robotics", "inverse_kinematics",
            {{"project_id", projectId}, {"target_xyz_m", roboticsJsonArray(target)}});
        roboticsOutput_->setPlainText(QStringLiteral("IK submitted, job %1…")
                                          .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(QStringLiteral("Robotics IK submit failed: ") +
                                      QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsGenerate() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string projectId = roboticsProjectId();
    if (projectId.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: create a robot first (no project id)"));
        return;
    }
    std::vector<double> start;
    std::vector<double> goal;
    if (startJoint_ == nullptr || goalJoint_ == nullptr ||
        !roboticsNumberList(startJoint_->text(), start) || start.empty() ||
        !roboticsNumberList(goalJoint_->text(), goal) || goal.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: trajectory requires start and goal joint lists"));
        return;
    }
    if (start.size() != goal.size()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: start and goal joint lists must match in length"));
        return;
    }
    trinity::core::Json params = {{"project_id", projectId},
                                  {"joint_start", roboticsJsonArray(start)},
                                  {"joint_goal", roboticsJsonArray(goal)}};
    if (duration_ != nullptr && !duration_->text().trimmed().isEmpty()) {
        bool ok = false;
        const double seconds = duration_->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(seconds) || seconds <= 0.0) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: duration must be a positive number of seconds"));
            return;
        }
        params["duration_s"] = seconds;
    }
    if (robotDt_ != nullptr && !robotDt_->text().trimmed().isEmpty()) {
        bool ok = false;
        const double dt = robotDt_->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(dt) || dt <= 0.0) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: dt must be a positive number of seconds"));
            return;
        }
        params["dt"] = dt;
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "generate_trajectory", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("Model trajectory submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics trajectory submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsValidate() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string projectId = roboticsProjectId();
    if (projectId.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: create a robot first (no project id)"));
        return;
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "validate_robot",
                                             {{"project_id", projectId}});
        roboticsOutput_->setPlainText(
            QStringLiteral("Robot validation submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics validation submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

bool MainWindow::roboticsJointConfig(trinity::core::Json& params, QString& errorOut) {
    if (robotJointType_ != nullptr) {
        params["joint_type"] = robotJointType_->currentText().toStdString();
    }
    if (robotJointAxis_ != nullptr && !robotJointAxis_->text().trimmed().isEmpty()) {
        std::vector<double> axis;
        if (!roboticsNumberList(robotJointAxis_->text(), axis) || axis.size() != 3) {
            errorOut = QStringLiteral("Robotics: joint axis must be 3 numbers (e.g. 0,0,1)");
            return false;
        }
        params["axis"] = roboticsJsonArray(axis);
    }
    if (robotJointLimits_ != nullptr && !robotJointLimits_->text().trimmed().isEmpty()) {
        std::vector<double> limits;
        if (!roboticsNumberList(robotJointLimits_->text(), limits) || limits.size() != 2) {
            errorOut = QStringLiteral("Robotics: joint limits must be 'lower,upper'");
            return false;
        }
        const bool prismatic =
            robotJointType_ != nullptr &&
            robotJointType_->currentText() == QStringLiteral("prismatic");
        params[prismatic ? "limit_lower_mm" : "limit_lower_deg"] = limits[0];
        params[prismatic ? "limit_upper_mm" : "limit_upper_deg"] = limits[1];
    }
    return true;
}

void MainWindow::handleRoboticsAddLink() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string projectId = roboticsProjectId();
    if (projectId.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: create a robot first (no project id)"));
        return;
    }
    const QString linkName =
        robotNewLinkName_ != nullptr ? robotNewLinkName_->text().trimmed() : QString();
    if (linkName.isEmpty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: Add Link needs a link name"));
        return;
    }
    trinity::core::Json params = {{"project_id", projectId},
                                  {"link_name", linkName.toStdString()}};
    if (robotLinkLength_ != nullptr && !robotLinkLength_->text().trimmed().isEmpty()) {
        bool ok = false;
        const double mm = robotLinkLength_->text().trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(mm) || mm < 0.0) {
            roboticsOutput_->setPlainText(
                QStringLiteral("Robotics: link length must be a non-negative number (mm)"));
            return;
        }
        params["length_mm"] = mm;
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "add_link", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("Add link submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics add link submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::handleRoboticsAddJoint() {
    if (roboticsOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string projectId = roboticsProjectId();
    if (projectId.empty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: create a robot first (no project id)"));
        return;
    }
    const QString jointName =
        robotNewJointName_ != nullptr ? robotNewJointName_->text().trimmed() : QString();
    const QString parentLink =
        robotParentLink_ != nullptr ? robotParentLink_->text().trimmed() : QString();
    const QString childLink =
        robotChildLink_ != nullptr ? robotChildLink_->text().trimmed() : QString();
    if (jointName.isEmpty() || parentLink.isEmpty() || childLink.isEmpty()) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics: Add Joint needs joint name, parent link and child link"));
        return;
    }
    trinity::core::Json params = {{"project_id", projectId},
                                  {"joint_name", jointName.toStdString()},
                                  {"parent_link", parentLink.toStdString()},
                                  {"child_link", childLink.toStdString()}};
    QString configError;
    if (!roboticsJointConfig(params, configError)) {
        roboticsOutput_->setPlainText(configError);
        return;
    }
    try {
        lastRoboticsJobId_ = worker_->submit("robotics", "add_joint", params);
        roboticsOutput_->setPlainText(
            QStringLiteral("Add joint submitted, job %1…")
                .arg(QString::fromStdString(lastRoboticsJobId_)));
    } catch (const std::exception& exc) {
        roboticsOutput_->setPlainText(
            QStringLiteral("Robotics add joint submit failed: ") +
            QString::fromStdString(exc.what()));
        lastRoboticsJobId_.clear();
    }
    refreshJobs();
}

void MainWindow::refreshRoboticsResult() {    if (roboticsOutput_ == nullptr || jobs_ == nullptr || lastRoboticsJobId_.empty()) {
        return;
    }
    trinity::jobs::Job job;
    try {
        job = jobs_->get(lastRoboticsJobId_);
    } catch (...) {
        return;
    }
    const QString shortId = QString::fromStdString(
        job.jobId.size() > 8 ? job.jobId.substr(0, 8) : job.jobId);
    QString report =
        QStringLiteral("Job: %1  •  %2/%3  •  %4\n")
            .arg(shortId, QString::fromStdString(job.engine),
                 QString::fromStdString(job.operation),
                 QString::fromStdString(toString(job.status)));

    if (!job.result.is_null() && job.result.is_object()) {
        const auto& envelope = job.result;
        // The job store persists the bare engine result (not the envelope),
        // so accept both shapes: {"result": {...}} and the result itself.
        {
            const auto& res = (envelope.contains("result") && envelope["result"].is_object())
                                  ? envelope["result"]
                                  : envelope;
            if (res.contains("project_id") && res["project_id"].is_string()) {
                const std::string seen = res["project_id"].get<std::string>();
                if (!seen.empty()) {
                    lastRoboticsProjectId_ = seen;
                    hasRoboticsProject_ = true;
                    if (robotProject_ != nullptr &&
                        robotProject_->text().trimmed().isEmpty()) {
                        robotProject_->setText(QString::fromStdString(seen));
                    }
                }
            }
            if (res.contains("end_effector") && res["end_effector"].is_object() &&
                res["end_effector"].contains("position")) {
                const auto& pos = res["end_effector"]["position"];
                const std::string chainSource =
                    res.value("chain_source", res.value("method", "?"));
                const std::string anglesSource = res.value(
                    "angles_source", res.value("joint_positions_source", "?"));
                report += QStringLiteral("EE: (%1, %2, %3) m  chain=%4  angles=%5\n")
                              .arg(pos.value("x", 0.0), 0, 'f', 4)
                              .arg(pos.value("y", 0.0), 0, 'f', 4)
                              .arg(pos.value("z", 0.0), 0, 'f', 4)
                              .arg(QString::fromStdString(chainSource))
                              .arg(QString::fromStdString(anglesSource));
                if (res.contains("ik") && res["ik"].is_object()) {
                    const auto& ik = res["ik"];
                    report += QStringLiteral("IK: converged=%1 iters=%2 err=%3 tol=%4 "
                                             "within_limits=%5%6\n")
                                  .arg(ik.value("converged", false) ? "yes" : "no")
                                  .arg(ik.value("iterations", 0LL))
                                  .arg(ik.value("final_error_m", 0.0), 0, 'g', 4)
                                  .arg(ik.value("tolerance_m", 0.0), 0, 'g', 2)
                                  .arg(ik.value("within_limits", false) ? "yes" : "no")
                                  .arg(ik.contains("message")
                                           ? QStringLiteral(" note=") +
                                                 QString::fromStdString(
                                                     ik.value("message", ""))
                                           : QString());
                }
            } else if (res.contains("links") || res.contains("joints")) {
                // Model authoring results (create_robot / add_link / add_joint):
                // links, joints with types + limits, and the current state.
                const std::string pid = res.value("project_id", "");
                report += QStringLiteral("Robot: %1  project=%2  links=%3 joints=%4 actuated=%5\n")
                              .arg(QString::fromStdString(res.value("robot_name", "?")))
                              .arg(QString::fromStdString(
                                  pid.size() > 8 ? pid.substr(0, 8) : pid))
                              .arg(res.contains("link_count")
                                       ? res.value("link_count", 0LL)
                                       : static_cast<long long>(
                                             res.value("links", core::Json::array()).size()))
                              .arg(res.contains("joint_count")
                                       ? res.value("joint_count", 0LL)
                                       : static_cast<long long>(
                                             res.value("joints", core::Json::array()).size()))
                              .arg(res.value("actuated_joint_count", 0LL));
                if (res.contains("joints") && res["joints"].is_array()) {
                    for (const auto& joint : res["joints"]) {
                        if (!joint.is_object()) {
                            continue;
                        }
                        double ax = 0.0;
                        double ay = 0.0;
                        double az = 0.0;
                        if (joint.contains("axis") && joint["axis"].is_object()) {
                            ax = joint["axis"].value("x", 0.0);
                            ay = joint["axis"].value("y", 0.0);
                            az = joint["axis"].value("z", 0.0);
                        }
                        QString line = QStringLiteral("  %1 [%2] axis=(%3,%4,%5)")
                                           .arg(QString::fromStdString(
                                               joint.value("name", "?")))
                                           .arg(QString::fromStdString(
                                               joint.value("type", "?")))
                                           .arg(ax, 0, 'g', 4)
                                           .arg(ay, 0, 'g', 4)
                                           .arg(az, 0, 'g', 4);
                        if (joint.contains("limit") && joint["limit"].is_object() &&
                            joint["limit"].value("specified", false)) {
                            line += QStringLiteral(" limits=[%1,%2]")
                                        .arg(joint["limit"].value("lower", 0.0), 0, 'g', 4)
                                        .arg(joint["limit"].value("upper", 0.0), 0, 'g', 4);
                        }
                        report += line + QStringLiteral("\n");
                    }
                }
                if (res.contains("initial_state") && res["initial_state"].is_object() &&
                    res["initial_state"].contains("positions")) {
                    report += QStringLiteral("State: ") +
                              QString::fromStdString(
                                  res["initial_state"]["positions"].dump()) +
                              QStringLiteral("\n");
                }
                if (res.contains("model_validation")) {
                    report += QStringLiteral("Model valid: %1\n")
                                  .arg(res["model_validation"].value("passed", false)
                                           ? "yes"
                                           : "NO");
                }
            } else if (res.contains("start_reached")) {
                const std::string finalStr = res.contains("goal_positions")
                                                 ? res["goal_positions"].dump()
                                                 : (res.contains("final_positions")
                                                        ? res["final_positions"].dump()
                                                        : "?");
                const std::string methodStr =
                    res.value("method", res.value("integrator", "?"));
                report += QStringLiteral(
                              "Trajectory: start=%1 goal=%2  samples=%3  final=(%4)  [%5]\n")
                              .arg(res.value("start_reached", false) ? "reached" : "MISS")
                              .arg(res.value("goal_reached", false) ? "reached" : "MISS")
                              .arg(res.value("sample_count", 0LL))
                              .arg(QString::fromStdString(finalStr))
                              .arg(QString::fromStdString(methodStr));
                              
                if (res.contains("trajectory") && res["trajectory"].is_array() && simPosChart_ != nullptr) {
                    std::vector<SeriesPoint> posSeries;
                    const auto& traj = res["trajectory"];
                    for (size_t i = 0; i < traj.size(); ++i) {
                        const auto& pt = traj[i];
                        if (pt.is_object() && pt.contains("time") && pt.contains("positions") && pt["positions"].is_array() && !pt["positions"].empty()) {
                            double t = pt["time"].get<double>();
                            double p = pt["positions"][0].get<double>();
                            posSeries.push_back({t, p});
                        }
                    }
                    simPosChart_->setSeries(posSeries, "Position Trajectory", "Time (s)", "Position");
                }

            } else if (res.contains("path")) {
                report += QStringLiteral("URDF: %1\n  sha256=%2  revolute=%3 joints=%4\n")
                              .arg(QString::fromStdString(res.value("path", "")))
                              .arg(QString::fromStdString(res.value("sha256", "")))
                              .arg(res.value("revolute_joints", 0LL))
                              .arg(res.value("joint_count", 0LL));
            } else if (res.contains("model_validation") ||
                       res.contains("state_validation") ||
                       res.contains("joint_state_validation")) {
                const std::string pid = res.value("project_id", "");
                report += QStringLiteral("Robot: %1  project=%2  joints=%3 actuated=%4 "
                                         "links=%5\n")
                              .arg(QString::fromStdString(res.value("robot_name", "?")))
                              .arg(QString::fromStdString(
                                  pid.size() > 8 ? pid.substr(0, 8) : pid))
                              .arg(res.value("joint_count", 0LL))
                              .arg(res.value("actuated_joint_count", 0LL))
                              .arg(res.value("link_count", 0LL));
                if (res.contains("model_validation")) {
                    report += QStringLiteral("Model valid: %1\n")
                                  .arg(res["model_validation"].value("passed", false)
                                           ? "yes"
                                           : "NO");
                }
                const std::string stateKey = res.contains("state_validation")
                                                 ? "state_validation"
                                                 : "joint_state_validation";
                if (res.contains(stateKey)) {
                    std::string positions =
                        res.contains("positions") ? res["positions"].dump() : "?";
                    if (positions.size() > 160) {
                        positions = positions.substr(0, 160) + "…";
                    }
                    report += QStringLiteral("State: %1  valid: %2\n")
                                  .arg(QString::fromStdString(positions))
                                  .arg(res[stateKey].value("passed", false) ? "yes" : "NO");
                }
                if (res.contains("passed") && res["passed"].is_boolean()) {
                    report += QStringLiteral("Overall: %1\n")
                                  .arg(res.value("passed", false) ? "PASS" : "FAIL");
                }
            } else if (!res.is_null() && !res.empty()) {
                report += QStringLiteral("Result: ") +
                          QString::fromStdString(res.dump(2)) + QStringLiteral("\n");
            }
        }
        if (envelope.contains("validation") && !envelope["validation"].is_null()) {
            const auto& v = envelope["validation"];
            report += QStringLiteral("Validation: ") +
                      QString::fromStdString(v.value("status", "?")) + QStringLiteral(" — ") +
                      QString::fromStdString(v.value("message", "")) + QStringLiteral("\n");
        }
        if (envelope.contains("errors") && envelope["errors"].is_array() &&
            !envelope["errors"].empty()) {
            std::string errs = envelope["errors"].dump(2);
            if (errs.size() > 600) {
                errs = errs.substr(0, 600) + "…";
            }
            report += QStringLiteral("Errors: ") + QString::fromStdString(errs) +
                      QStringLiteral("\n");
        }
    }
    if (!job.error.is_null()) {
        std::string err = job.error.dump();
        if (err.size() > 300) {
            err = err.substr(0, 300) + "…";
        }
        report += QStringLiteral("Job error: ") + QString::fromStdString(err) +
                  QStringLiteral("\n");
    }
    if (roboticsOutput_->toPlainText() != report) {
        roboticsOutput_->setPlainText(report);
    }
    // Show the arm in the 3D viewer once per completed FK/IK job: the mesh
    // is synthesized from the in-memory FK frames (no new renderer).
    if (viewerController_ != nullptr && job.jobId == lastRoboticsJobId_ &&
        job.jobId != lastRoboticsViewerJobId_ &&
        job.status == trinity::jobs::JobStatus::Completed &&
        (job.operation == "compute_forward_kinematics" ||
         job.operation == "inverse_kinematics" || job.operation == "forward_kinematics")) {
        lastRoboticsViewerJobId_ = job.jobId;
        viewerController_->openJob(job.jobId);
    }
}

// --- PCB workspace: structured pcb jobs on the shared worker thread.
// Number parsing failures are reported inline without submitting.

namespace {

bool pcbNumber(const QString& text, double& valueOut) {
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return false;
    }
    valueOut = value;
    return true;
}

}  // namespace

void MainWindow::handlePcbCreate() {
    if (pcbWidth_ == nullptr || pcbHeight_ == nullptr || pcbOutput_ == nullptr ||
        worker_ == nullptr) {
        return;
    }
    double width = 0.0;
    double height = 0.0;
    if (!pcbNumber(pcbWidth_->text(), width) || !pcbNumber(pcbHeight_->text(), height)) {
        pcbOutput_->setPlainText(QStringLiteral("PCB: board width/height must be numbers"));
        return;
    }
    trinity::core::Json params = {{"width_mm", width}, {"height_mm", height}};
    if (pcbThick_ != nullptr && !pcbThick_->text().trimmed().isEmpty()) {
        double thick = 0.0;
        if (!pcbNumber(pcbThick_->text(), thick)) {
            pcbOutput_->setPlainText(QStringLiteral("PCB: thickness must be a number"));
            return;
        }
        params["thickness_mm"] = thick;
    }
    try {
        lastPcbJobId_ = worker_->submit("pcb", "create_board", params);
        hasPcbDesign_ = false;
        pcbOutput_->setPlainText(QStringLiteral("PCB: board submitted, job %1…")
                                     .arg(QString::fromStdString(lastPcbJobId_)));
    } catch (const std::exception& exc) {
        pcbOutput_->setPlainText(QStringLiteral("PCB submit failed: ") +
                                 QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handlePcbAddComponent() {
    if (pcbRef_ == nullptr || pcbValue_ == nullptr || pcbFootprint_ == nullptr ||
        pcbOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasPcbDesign_) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: create a board first (no design in context)"));
        return;
    }
    const std::string ref = pcbRef_->text().trimmed().toStdString();
    const std::string footprint = pcbFootprint_->text().trimmed().toStdString();
    if (ref.empty() || footprint.empty()) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: component ref and footprint are required"));
        return;
    }
    trinity::core::Json params = {{"design", lastPcbDesign_},
                                  {"ref", ref},
                                  {"footprint", footprint},
                                  {"value", pcbValue_->text().trimmed().toStdString()}};
    try {
        lastPcbJobId_ = worker_->submit("pcb", "add_component", params);
        pcbOutput_->setPlainText(QStringLiteral("PCB: add %1 submitted, job %2…")
                                     .arg(QString::fromStdString(ref),
                                          QString::fromStdString(lastPcbJobId_)));
    } catch (const std::exception& exc) {
        pcbOutput_->setPlainText(QStringLiteral("PCB submit failed: ") +
                                 QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handlePcbAddNet() {
    if (pcbNetName_ == nullptr || pcbNetPins_ == nullptr || pcbOutput_ == nullptr ||
        worker_ == nullptr) {
        return;
    }
    if (!hasPcbDesign_) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: create a board first (no design in context)"));
        return;
    }
    const std::string name = pcbNetName_->text().trimmed().toStdString();
    if (name.empty()) {
        pcbOutput_->setPlainText(QStringLiteral("PCB: net name is required"));
        return;
    }
    trinity::core::Json pins = trinity::core::Json::array();
    for (const QString& part :
         pcbNetPins_->text().split(QStringLiteral(","), Qt::SkipEmptyParts)) {
        const QString pin = part.trimmed();
        if (!pin.isEmpty()) {
            pins.push_back(pin.toStdString());
        }
    }
    if (pins.empty()) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: net pins are required (e.g. U1.19, U2.13)"));
        return;
    }
    trinity::core::Json params = {
        {"design", lastPcbDesign_}, {"name", name}, {"pins", pins}};
    try {
        lastPcbJobId_ = worker_->submit("pcb", "add_net", params);
        pcbOutput_->setPlainText(QStringLiteral("PCB: net %1 submitted, job %2…")
                                     .arg(QString::fromStdString(name),
                                          QString::fromStdString(lastPcbJobId_)));
    } catch (const std::exception& exc) {
        pcbOutput_->setPlainText(QStringLiteral("PCB submit failed: ") +
                                 QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handlePcbPlace() {
    if (pcbPlaceRef_ == nullptr || pcbPlaceX_ == nullptr || pcbPlaceY_ == nullptr ||
        pcbOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasPcbDesign_) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: create a board first (no design in context)"));
        return;
    }
    const std::string ref = pcbPlaceRef_->text().trimmed().toStdString();
    double x = 0.0;
    double y = 0.0;
    if (ref.empty() || !pcbNumber(pcbPlaceX_->text(), x) ||
        !pcbNumber(pcbPlaceY_->text(), y)) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: ref and numeric X/Y are required"));
        return;
    }
    double rot = 0.0;
    if (pcbPlaceRot_ != nullptr && !pcbPlaceRot_->text().trimmed().isEmpty() &&
        !pcbNumber(pcbPlaceRot_->text(), rot)) {
        pcbOutput_->setPlainText(QStringLiteral("PCB: rotation must be a number"));
        return;
    }
    trinity::core::Json params = {{"design", lastPcbDesign_},
                                  {"ref", ref},
                                  {"x_mm", x},
                                  {"y_mm", y},
                                  {"rotation_deg", rot}};
    try {
        lastPcbJobId_ = worker_->submit("pcb", "place_component", params);
        pcbOutput_->setPlainText(QStringLiteral("PCB: place %1 submitted, job %2…")
                                     .arg(QString::fromStdString(ref),
                                          QString::fromStdString(lastPcbJobId_)));
    } catch (const std::exception& exc) {
        pcbOutput_->setPlainText(QStringLiteral("PCB submit failed: ") +
                                 QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handlePcbValidate() {
    if (pcbOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasPcbDesign_) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: create a board first (no design in context)"));
        return;
    }
    try {
        lastPcbJobId_ =
            worker_->submit("pcb", "validate_design", {{"design", lastPcbDesign_}});
        pcbOutput_->setPlainText(QStringLiteral("PCB: validation submitted, job %1…")
                                     .arg(QString::fromStdString(lastPcbJobId_)));
    } catch (const std::exception& exc) {
        pcbOutput_->setPlainText(QStringLiteral("PCB submit failed: ") +
                                 QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handlePcbExport() {
    if (pcbOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasPcbDesign_) {
        pcbOutput_->setPlainText(
            QStringLiteral("PCB: create a board first (no design in context)"));
        return;
    }
    try {
        lastPcbJobId_ = worker_->submit(
            "pcb", "export",
            {{"design", lastPcbDesign_}, {"project", "trinity_pcb"}});
        pcbOutput_->setPlainText(QStringLiteral("PCB: export submitted, job %1…")
                                     .arg(QString::fromStdString(lastPcbJobId_)));
    } catch (const std::exception& exc) {
        pcbOutput_->setPlainText(QStringLiteral("PCB submit failed: ") +
                                 QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

// --- Firmware workspace: structured firmware jobs on the shared worker
// thread. The live project JSON chains between ops (same pattern as PCB).

void MainWindow::handleFwCreate() {
    if (fwName_ == nullptr || fwOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    const std::string name = fwName_->text().trimmed().toStdString();
    if (name.empty()) {
        fwOutput_->setPlainText(QStringLiteral("Firmware: project name is required"));
        return;
    }
    try {
        lastFwJobId_ = worker_->submit("firmware", "create_project", {{"name", name}});
        hasFwProject_ = false;
        lastFwProject_ = core::Json::object();
        fwOutput_->setPlainText(QStringLiteral("Firmware: create project '%1' submitted, job %2…")
                                    .arg(QString::fromStdString(name),
                                         QString::fromStdString(lastFwJobId_)));
    } catch (const std::exception& exc) {
        fwOutput_->setPlainText(QStringLiteral("Firmware submit failed: ") +
                                QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handleFwSelectMcu() {
    if (fwMcu_ == nullptr || fwOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasFwProject_) {
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: create a project first (no project in context)"));
        return;
    }
    const std::string mcu = fwMcu_->currentText().toStdString();
    try {
        lastFwJobId_ = worker_->submit(
            "firmware", "select_mcu", {{"project", lastFwProject_}, {"mcu", mcu}});
        fwOutput_->setPlainText(QStringLiteral("Firmware: select %1 submitted, job %2…")
                                    .arg(QString::fromStdString(mcu),
                                         QString::fromStdString(lastFwJobId_)));
    } catch (const std::exception& exc) {
        fwOutput_->setPlainText(QStringLiteral("Firmware submit failed: ") +
                                QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handleFwConfigurePin() {
    if (fwPin_ == nullptr || fwFunc_ == nullptr || fwDir_ == nullptr ||
        fwOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasFwProject_) {
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: select an MCU first (no project in context)"));
        return;
    }
    const std::string pin = fwPin_->text().trimmed().toStdString();
    const std::string function = fwFunc_->text().trimmed().toStdString();
    const std::string direction = fwDir_->currentText().toStdString();
    if (pin.empty() || function.empty()) {
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: pin and function are required"));
        return;
    }
    try {
        lastFwJobId_ = worker_->submit("firmware", "configure_pin",
                                       {{"project", lastFwProject_},
                                        {"pin", pin},
                                        {"function", function},
                                        {"direction", direction}});
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: pin %1 (%2) submitted, job %3…")
                .arg(QString::fromStdString(pin), QString::fromStdString(direction),
                     QString::fromStdString(lastFwJobId_)));
    } catch (const std::exception& exc) {
        fwOutput_->setPlainText(QStringLiteral("Firmware submit failed: ") +
                                QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handleFwConfigurePeripheral() {
    if (fwKind_ == nullptr || fwPinA_ == nullptr || fwOutput_ == nullptr ||
        worker_ == nullptr) {
        return;
    }
    if (!hasFwProject_) {
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: select an MCU first (no project in context)"));
        return;
    }
    const std::string kind = fwKind_->currentText().toStdString();
    const std::string a = fwPinA_->text().trimmed().toStdString();
    const std::string b =
        fwPinB_ != nullptr ? fwPinB_->text().trimmed().toStdString() : std::string();
    const std::string param =
        fwParam_ != nullptr ? fwParam_->text().trimmed().toStdString() : std::string();
    core::Json params = {{"project", lastFwProject_}, {"kind", kind}};
    if (kind == "uart") {
        if (a.empty() || b.empty()) {
            fwOutput_->setPlainText(
                QStringLiteral("Firmware: UART requires TX and RX pins"));
            return;
        }
        params["peripheral"] = "UART0";
        params["tx"] = a;
        params["rx"] = b;
        if (!param.empty()) {
            params["baud"] = std::atoi(param.c_str());
        }
    } else if (kind == "i2c") {
        if (a.empty() || b.empty()) {
            fwOutput_->setPlainText(
                QStringLiteral("Firmware: I2C requires SDA and SCL pins"));
            return;
        }
        params["peripheral"] = "I2C0";
        params["sda"] = a;
        params["scl"] = b;
    } else {
        if (a.empty()) {
            fwOutput_->setPlainText(QStringLiteral("Firmware: PWM requires a pin"));
            return;
        }
        params["peripheral"] = "PWM";
        params["pin"] = a;
        if (!param.empty()) {
            params["freq_hz"] = std::atoi(param.c_str());
        }
    }
    try {
        lastFwJobId_ = worker_->submit("firmware", "configure_peripheral", params);
        fwOutput_->setPlainText(QStringLiteral("Firmware: %1 submitted, job %2…")
                                    .arg(QString::fromStdString(kind),
                                         QString::fromStdString(lastFwJobId_)));
    } catch (const std::exception& exc) {
        fwOutput_->setPlainText(QStringLiteral("Firmware submit failed: ") +
                                QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handleFwGenerate() {
    if (fwOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasFwProject_) {
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: configure a project first (no project in context)"));
        return;
    }
    try {
        lastFwJobId_ =
            worker_->submit("firmware", "generate_firmware", {{"project", lastFwProject_}});
        fwOutput_->setPlainText(QStringLiteral("Firmware: generate submitted, job %1…")
                                    .arg(QString::fromStdString(lastFwJobId_)));
    } catch (const std::exception& exc) {
        fwOutput_->setPlainText(QStringLiteral("Firmware submit failed: ") +
                                QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handleFwValidate() {
    if (fwOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasFwProject_) {
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: configure a project first (no project in context)"));
        return;
    }
    try {
        lastFwJobId_ =
            worker_->submit("firmware", "validate_project", {{"project", lastFwProject_}});
        fwOutput_->setPlainText(QStringLiteral("Firmware: validate submitted, job %1…")
                                    .arg(QString::fromStdString(lastFwJobId_)));
    } catch (const std::exception& exc) {
        fwOutput_->setPlainText(QStringLiteral("Firmware submit failed: ") +
                                QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::handleFwBuild() {
    if (fwOutput_ == nullptr || worker_ == nullptr) {
        return;
    }
    if (!hasFwProject_) {
        fwOutput_->setPlainText(
            QStringLiteral("Firmware: generate sources first (no project in context)"));
        return;
    }
    core::Json project = lastFwProject_;
    if (project.contains("build") && project["build"].is_object() &&
        fwProfile_ != nullptr) {
        project["build"]["profile"] = fwProfile_->currentText().toStdString();
    }
    try {
        lastFwJobId_ = worker_->submit("firmware", "build", {{"project", project}});
        fwOutput_->setPlainText(QStringLiteral("Firmware: build submitted, job %1…")
                                    .arg(QString::fromStdString(lastFwJobId_)));
    } catch (const std::exception& exc) {
        fwOutput_->setPlainText(QStringLiteral("Firmware submit failed: ") +
                                QString::fromStdString(exc.what()));
    }
    refreshJobs();
}

void MainWindow::refreshFwResult() {
    if (fwOutput_ == nullptr || jobs_ == nullptr || lastFwJobId_.empty()) {
        return;
    }
    trinity::jobs::Job job;
    try {
        job = jobs_->get(lastFwJobId_);
    } catch (...) {
        return;
    }
    const QString shortId = QString::fromStdString(
        job.jobId.size() > 8 ? job.jobId.substr(0, 8) : job.jobId);
    QString report =
        QStringLiteral("Job: %1  •  firmware/%2  •  %3\n")
            .arg(shortId, QString::fromStdString(job.operation),
                 QString::fromStdString(toString(job.status)));
    if (!job.result.is_null() && job.result.is_object()) {
        const auto& envelope = job.result;
        if (envelope.contains("result") && envelope["result"].is_object()) {
            const auto& data = envelope["result"];
            if (data.contains("project") && data["project"].is_object()) {
                lastFwProject_ = data["project"];
                hasFwProject_ = true;
                const auto& p = lastFwProject_;
                report += QStringLiteral("Project: %1  •  MCU: %2  •  Clock: %3 Hz\n")
                              .arg(QString::fromStdString(p.value("name", "")),
                                   p.value("has_mcu", false)
                                       ? QString::fromStdString(p["mcu"].value("model", "?"))
                                       : QStringLiteral("(none)"),
                                   QString::number(static_cast<qulonglong>(
                                       p.value("clock_hz", 0LL))));
                QStringList pinList;
                if (p.contains("pin_mappings") && p["pin_mappings"].is_array()) {
                    for (const auto& m : p["pin_mappings"]) {
                        pinList << QString::fromStdString(m.value("mcu_pin", "?")) +
                                       QStringLiteral(":") +
                                       QString::fromStdString(m.value("function", "?"));
                    }
                }
                report += QStringLiteral("Pins: %1\n")
                              .arg(pinList.isEmpty() ? QStringLiteral("(none)")
                                                     : pinList.join(QStringLiteral(", ")));
                QStringList reqList;
                if (p.contains("requirements") && p["requirements"].is_array()) {
                    for (const auto& r : p["requirements"]) {
                        reqList << QString::fromStdString(r.value("kind", "?"));
                    }
                }
                report += QStringLiteral("Requirements: %1\n")
                              .arg(reqList.isEmpty() ? QStringLiteral("(none)")
                                                     : reqList.join(QStringLiteral(", ")));
                if (p.contains("build") && p["build"].is_object()) {
                    report += QStringLiteral("Build: %1 / %2 / %3\n")
                                  .arg(QString::fromStdString(
                                           p["build"].value("profile", "debug")),
                                       QString::fromStdString(p["build"].value("arch", "?")),
                                       QString::fromStdString(
                                           p["build"].value("toolchain", "?")));
                }
            }
            if (data.contains("files") && data["files"].is_array()) {
                report += QStringLiteral("Generated files:\n");
                for (const auto& f : data["files"]) {
                    report += QStringLiteral("  %1 (%2 B)\n")
                                  .arg(QString::fromStdString(f.value("path", "?")),
                                       QString::number(
                                           static_cast<qulonglong>(f.value("bytes", 0))));
                }
            }
            if (data.contains("rules") && data["rules"].is_array()) {
                report += QStringLiteral("Validation rules:\n");
                for (const auto& rule : data["rules"]) {
                    if (!rule.value("passed", true)) {
                        report += QStringLiteral("  FAIL [%1] %2\n")
                                      .arg(QString::fromStdString(rule.value("rule", "?")),
                                           QString::fromStdString(
                                               rule.value("message", "")));
                    }
                }
            }
            if (data.contains("passed")) {
                report += QStringLiteral("Validation: %1\n")
                              .arg(data.value("passed", false)
                                       ? QStringLiteral("VALIDATED")
                                       : QStringLiteral("INVALID"));
            }
            if (data.contains("executed")) {
                const bool executed = data.value("executed", false);
                report += QStringLiteral("Build: %1\n")
                              .arg(executed
                                       ? QStringLiteral("executed, exit %1, success=%2")
                                             .arg(data.value("exit_code", -1))
                                             .arg(data.value("success", false)
                                                      ? QStringLiteral("true")
                                                      : QStringLiteral("false"))
                                       : QStringLiteral(
                                             "CAPABILITY_UNAVAILABLE (no toolchain)"));
                if (data.contains("stdout") &&
                    !data["stdout"].get<std::string>().empty()) {
                    report += QStringLiteral("stdout: ") +
                              QString::fromStdString(data["stdout"].get<std::string>()) +
                              QStringLiteral("\n");
                }
                if (data.contains("stderr") &&
                    !data["stderr"].get<std::string>().empty()) {
                    report += QStringLiteral("stderr: ") +
                              QString::fromStdString(data["stderr"].get<std::string>()) +
                              QStringLiteral("\n");
                }
            }
            if (data.contains("message")) {
                report += QStringLiteral("Message: ") +
                          QString::fromStdString(data.value("message", "")) +
                          QStringLiteral("\n");
            }
        }
        if (envelope.contains("artifacts") && envelope["artifacts"].is_array() &&
            !envelope["artifacts"].empty()) {
            report += QStringLiteral("Artifacts:\n");
            for (const auto& art : envelope["artifacts"]) {
                report += QStringLiteral("  %1 (%2, %3 B, sha256 %4…)\n")
                              .arg(QString::fromStdString(art.value("path", "")),
                                   QString::fromStdString(art.value("type", "")),
                                   QString::number(static_cast<qulonglong>(
                                       art.value("size_bytes", 0LL))),
                                   QString::fromStdString(
                                       art.value("checksum", "").substr(0, 12)));
            }
        }
        if (envelope.contains("validation") && !envelope["validation"].is_null()) {
            const auto& v = envelope["validation"];
            report += QStringLiteral("Validation: ") +
                      QString::fromStdString(v.value("status", "?")) + QStringLiteral(" — ") +
                      QString::fromStdString(v.value("message", "")) + QStringLiteral("\n");
        }
        if (envelope.contains("errors") && envelope["errors"].is_array() &&
            !envelope["errors"].empty()) {
            std::string errs = envelope["errors"].dump(2);
            if (errs.size() > 600) {
                errs = errs.substr(0, 600) + "…";
            }
            report += QStringLiteral("Errors: ") + QString::fromStdString(errs) +
                      QStringLiteral("\n");
        }
    }
    if (!job.error.is_null()) {
        std::string err = job.error.dump();
        if (err.size() > 300) {
            err = err.substr(0, 300) + "…";
        }
        report += QStringLiteral("Job error: ") + QString::fromStdString(err) +
                  QStringLiteral("\n");
    }
    if (fwOutput_->toPlainText() != report) {
        fwOutput_->setPlainText(report);
    }
}

void MainWindow::refreshPcbResult() {
    if (pcbOutput_ == nullptr || jobs_ == nullptr || lastPcbJobId_.empty()) {
        return;
    }
    trinity::jobs::Job job;
    try {
        job = jobs_->get(lastPcbJobId_);
    } catch (...) {
        return;
    }
    const QString shortId = QString::fromStdString(
        job.jobId.size() > 8 ? job.jobId.substr(0, 8) : job.jobId);
    QString report =
        QStringLiteral("Job: %1  •  pcb/%2  •  %3\n")
            .arg(shortId, QString::fromStdString(job.operation),
                 QString::fromStdString(toString(job.status)));
    if (!job.result.is_null() && job.result.is_object()) {
        const auto& envelope = job.result;
        if (envelope.contains("result") && envelope["result"].is_object()) {
            const auto& data = envelope["result"];
            if (data.contains("design") && data["design"].is_object()) {
                // Chain the live design for the next op and the viewer.
                lastPcbDesign_ = data["design"];
                hasPcbDesign_ = true;
                const auto& design = data["design"];
                if (design.contains("board")) {
                    report += QStringLiteral("Board: ") +
                              QString::fromStdString(
                                  design["board"].value("width_mm", 0.0) == 0.0
                                      ? design["board"].dump()
                                      : ("W " + std::to_string(design["board"].value(
                                                                    "width_mm", 0.0)) +
                                         " x H " +
                                         std::to_string(design["board"].value(
                                             "height_mm", 0.0)) +
                                         " x T " +
                                         std::to_string(design["board"].value(
                                             "thickness_mm", 0.0)) +
                                         " mm")) +
                              QStringLiteral("\n");
                }
                if (design.contains("components")) {
                    report += QStringLiteral("Components: %1  •  Nets: %2  •  "
                                             "Placements: %3\n")
                                  .arg(design["components"].is_array()
                                           ? static_cast<qulonglong>(
                                                 design["components"].size())
                                           : 0)
                                  .arg(design["nets"].is_array() ? static_cast<qulonglong>(
                                                                       design["nets"].size())
                                                                 : 0)
                                  .arg(design["placements"].is_array()
                                           ? static_cast<qulonglong>(
                                                 design["placements"].size())
                                           : 0);
                    if (design["components"].is_array()) {
                        QStringList refs;
                        for (const auto& comp : design["components"]) {
                            refs << QString::fromStdString(comp.value("ref", "?"));
                        }
                        report += QStringLiteral("Refs: ") + refs.join(QStringLiteral(", ")) +
                                  QStringLiteral("\n");
                    }
                }
            }
            if (data.contains("rules") && data["rules"].is_array()) {
                report += QStringLiteral("Rules:\n");
                for (const auto& rule : data["rules"]) {
                    if (!rule.value("passed", true)) {
                        report += QStringLiteral("  FAIL [%1] %2\n")
                                      .arg(QString::fromStdString(rule.value("rule", "?")),
                                           QString::fromStdString(
                                               rule.value("message", "")));
                    }
                }
            }
            if (data.contains("project")) {
                report += QStringLiteral("Project: ") +
                          QString::fromStdString(data.value("project", "")) +
                          QStringLiteral("\n");
            }
        }
        if (envelope.contains("artifacts") && envelope["artifacts"].is_array() &&
            !envelope["artifacts"].empty()) {
            report += QStringLiteral("Artifacts:\n");
            for (const auto& art : envelope["artifacts"]) {
                report += QStringLiteral("  %1 (%2, %3 B, sha256 %4…)\n")
                              .arg(QString::fromStdString(art.value("path", "")),
                                   QString::fromStdString(art.value("type", "")),
                                   QString::number(
                                       static_cast<qulonglong>(art.value("size_bytes", 0LL))),
                                   QString::fromStdString(
                                       art.value("checksum", "").substr(0, 12)));
            }
        }
        if (envelope.contains("validation") && !envelope["validation"].is_null()) {
            const auto& v = envelope["validation"];
            report += QStringLiteral("Validation: ") +
                      QString::fromStdString(v.value("status", "?")) +
                      QStringLiteral(" — ") +
                      QString::fromStdString(v.value("message", "")) +
                      QStringLiteral("\n");
        }
        if (envelope.contains("errors") && envelope["errors"].is_array() &&
            !envelope["errors"].empty()) {
            std::string errs = envelope["errors"].dump(2);
            if (errs.size() > 600) {
                errs = errs.substr(0, 600) + "…";
            }
            report += QStringLiteral("Errors: ") + QString::fromStdString(errs) +
                      QStringLiteral("\n");
        }
    }
    if (!job.error.is_null()) {
        std::string err = job.error.dump();
        if (err.size() > 300) {
            err = err.substr(0, 300) + "…";
        }
        report += QStringLiteral("Job error: ") + QString::fromStdString(err) +
                  QStringLiteral("\n");
    }
    const QDateTime started =
        QDateTime::fromString(QString::fromStdString(job.startedAt), Qt::ISODate);
    const QDateTime completed =
        QDateTime::fromString(QString::fromStdString(job.completedAt), Qt::ISODate);
    QString timing = QStringLiteral("Started: %1  Completed: %2")
                         .arg(QString::fromStdString(job.startedAt),
                              QString::fromStdString(job.completedAt.empty() ? "—"
                                                                             : job.completedAt));
    if (started.isValid() && completed.isValid()) {
        timing += QStringLiteral("  (%1 ms)").arg(started.msecsTo(completed));
    }
    report += timing;
    if (pcbOutput_->toPlainText() != report) {
        pcbOutput_->setPlainText(report);
    }
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
    refreshMathResult();
    refreshPcbResult();
    refreshFwResult();
    refreshSimResult();
    refreshResearchResult();
    refreshRoboticsResult();
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
    
    if (artifactRepo_ != nullptr) {
        try {
            auto a = artifactRepo_->get(artifactId);
            QString path = QString::fromStdString(a.path).toLower();
            if (path.endsWith(QStringLiteral(".png")) || 
                path.endsWith(QStringLiteral(".jpg")) || 
                path.endsWith(QStringLiteral(".jpeg")) || 
                path.endsWith(QStringLiteral(".bmp"))) {
                if (viewerPanel_) viewerPanel_->showImage(QString::fromStdString(a.path));
                return;
            } else {
                if (viewerPanel_) viewerPanel_->showImage(QString());
            }
        } catch(...) {}
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

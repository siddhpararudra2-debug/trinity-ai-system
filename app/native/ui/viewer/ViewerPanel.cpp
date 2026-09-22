#include "ViewerPanel.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>

#include <cmath>

#include "trinity/validation/ValidationResult.hpp"
#include "trinity/viewer/Measure.hpp"

namespace trinity::ui {

ViewerPanel::ViewerPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    statusLabel_ = new QLabel(QStringLiteral("No model loaded"), this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    errorLabel_ = new QLabel(this);
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QStringLiteral("color: #b42318;"));
    errorLabel_->setVisible(false);
    layout->addWidget(errorLabel_);

    auto* viewRow = new QHBoxLayout();
    auto* fitBtn = new QPushButton(QStringLiteral("Fit (F)"), this);
    auto* resetBtn = new QPushButton(QStringLiteral("Reset (R)"), this);
    auto* reloadBtn = new QPushButton(QStringLiteral("Reload"), this);
    viewRow->addWidget(fitBtn);
    viewRow->addWidget(resetBtn);
    viewRow->addWidget(reloadBtn);
    layout->addLayout(viewRow);
    connect(fitBtn, &QPushButton::clicked, this, &ViewerPanel::fitRequested);
    connect(resetBtn, &QPushButton::clicked, this, &ViewerPanel::resetRequested);
    connect(reloadBtn, &QPushButton::clicked, this, &ViewerPanel::reloadRequested);

    projectionBox_ = new QComboBox(this);
    projectionBox_->addItem(QStringLiteral("Perspective"));
    projectionBox_->addItem(QStringLiteral("Orthographic"));
    renderBox_ = new QComboBox(this);
    renderBox_->addItem(QStringLiteral("Solid"));
    renderBox_->addItem(QStringLiteral("Wireframe"));
    layout->addWidget(new QLabel(QStringLiteral("Projection"), this));
    layout->addWidget(projectionBox_);
    layout->addWidget(new QLabel(QStringLiteral("Display"), this));
    layout->addWidget(renderBox_);
    connect(projectionBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) {
                if (!suppressSignals_) {
                    emit projectionChanged(i);
                }
            });
    connect(renderBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) {
                if (!suppressSignals_) {
                    emit renderModeChanged(i);
                }
            });

    gridCheck_ = new QCheckBox(QStringLiteral("Grid"), this);
    gridCheck_->setChecked(true);
    axesCheck_ = new QCheckBox(QStringLiteral("Axes"), this);
    axesCheck_->setChecked(true);
    measureCheck_ = new QCheckBox(QStringLiteral("Measure (click 2 vertices)"), this);
    layout->addWidget(gridCheck_);
    layout->addWidget(axesCheck_);
    layout->addWidget(measureCheck_);
    connect(gridCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (!suppressSignals_) {
            emit gridToggled(on);
        }
    });
    connect(axesCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (!suppressSignals_) {
            emit axesToggled(on);
        }
    });
    connect(measureCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (!suppressSignals_) {
            emit measureModeToggled(on);
        }
    });
    auto* clearBtn = new QPushButton(QStringLiteral("Clear measurement"), this);
    layout->addWidget(clearBtn);
    connect(clearBtn, &QPushButton::clicked, this, &ViewerPanel::measureCleared);

    bboxLabel_ = new QLabel(QStringLiteral("Dimensions: —"), this);
    bboxLabel_->setWordWrap(true);
    layout->addWidget(new QLabel(QStringLiteral("Bounding dimensions (mm)"), this));
    layout->addWidget(bboxLabel_);

    measureLabel_ = new QLabel(QStringLiteral("Measurement: —"), this);
    measureLabel_->setWordWrap(true);
    layout->addWidget(new QLabel(QStringLiteral("Measurement"), this));
    layout->addWidget(measureLabel_);

    validationBadge_ = new QLabel(QStringLiteral("Validation: —"), this);
    validationBadge_->setWordWrap(true);
    validationDetail_ = new QLabel(this);
    validationDetail_->setWordWrap(true);
    validationDetail_->setStyleSheet(QStringLiteral("font-size: 12px; color: #555;"));
    layout->addWidget(new QLabel(QStringLiteral("Validation"), this));
    layout->addWidget(validationBadge_);
    layout->addWidget(validationDetail_);

    artifactDetail_ = new QLabel(QStringLiteral("Artifact: —"), this);
    artifactDetail_->setWordWrap(true);
    artifactDetail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(new QLabel(QStringLiteral("Selected artifact"), this));
    layout->addWidget(artifactDetail_);

    layout->addWidget(new QLabel(QStringLiteral("Artifacts"), this));
    artifactTable_ = new QTableWidget(this);
    artifactTable_->setColumnCount(7);
    artifactTable_->setHorizontalHeaderLabels(
        QStringList() << "Name" << "Type" << "Size" << "SHA-256" << "Validation" << "Job ID"
                      << "Created");
    artifactTable_->horizontalHeader()->setStretchLastSection(true);
    artifactTable_->verticalHeader()->setVisible(false);
    artifactTable_->setEditTriggers(QTableWidget::NoEditTriggers);
    artifactTable_->setSelectionBehavior(QTableWidget::SelectRows);
    artifactTable_->setMinimumHeight(120);
    layout->addWidget(artifactTable_);
    connect(artifactTable_, &QTableWidget::cellClicked, this,
            &ViewerPanel::onArtifactCellClicked);

    imagePreview_ = new QLabel(this);
    imagePreview_->setAlignment(Qt::AlignCenter);
    imagePreview_->setScaledContents(false);
    imagePreview_->setVisible(false);
    layout->addWidget(imagePreview_);

    layout->addStretch(1);
}

void ViewerPanel::setLoading(bool on) {
    if (on) {
        statusLabel_->setText(QStringLiteral("Loading model…"));
    }
}

void ViewerPanel::setErrorText(const QString& message) {
    errorLabel_->setVisible(!message.isEmpty());
    errorLabel_->setText(message);
}

void ViewerPanel::showImage(const QString& path) {
    if (path.isEmpty()) {
        imagePreview_->clear();
        imagePreview_->setVisible(false);
        return;
    }
    QPixmap pixmap(path);
    if (!pixmap.isNull()) {
        imagePreview_->setPixmap(pixmap.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        imagePreview_->setVisible(true);
    } else {
        imagePreview_->clear();
        imagePreview_->setVisible(false);
    }
}

void ViewerPanel::setState(const viewer::ViewerState& state) {
    suppressSignals_ = true;
    projectionBox_->setCurrentIndex(
        state.projection() == viewer::ProjectionMode::Perspective ? 0 : 1);
    renderBox_->setCurrentIndex(state.renderMode() == viewer::RenderMode::Solid ? 0 : 1);
    gridCheck_->setChecked(state.gridEnabled());
    axesCheck_->setChecked(state.axesEnabled());
    suppressSignals_ = false;

    if (state.loading()) {
        statusLabel_->setText(QStringLiteral("Loading model…"));
    } else if (state.hasMesh()) {
        const size_t tris = state.mesh()->triangleCount();
        statusLabel_->setText(QStringLiteral("Model: %1 triangles").arg(
            static_cast<qulonglong>(tris)));
    } else {
        statusLabel_->setText(QStringLiteral("No model loaded"));
    }
    setErrorText(QString::fromStdString(state.error()));

    updateValidation(state);
    updateBoundingBox(state);
    updateMeasurement(state);
    updateArtifactDetail(state);
}

void ViewerPanel::updateValidation(const viewer::ViewerState& state) {
    const std::string text = validation::toString(state.validation().status);
    validationBadge_->setText(QStringLiteral("Validation: %1")
                                  .arg(QString::fromStdString(text)));
    // Never represent invalid as verified: INVALID/FAILED are red, only
    // VALIDATED/VERIFIED are green.
    QString color = QStringLiteral("#888");
    if (state.validation().status == validation::ValidationStatus::Validated ||
        state.validation().status == validation::ValidationStatus::Verified) {
        color = QStringLiteral("#1a7f37");
    } else if (state.validation().status == validation::ValidationStatus::Invalid) {
        color = QStringLiteral("#b42318");
    }
    validationBadge_->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;").arg(color));

    QString detail = QString::fromStdString(state.validation().message);
    QStringList extra;
    for (const auto& m : state.validation().messages) {
        if (!m.passed || m.severity == validation::Severity::Warning ||
            m.severity == validation::Severity::Error) {
            extra << QStringLiteral("[%1] %2: %3")
                         .arg(QString::fromStdString(validation::toString(m.severity)),
                              QString::fromStdString(m.rule),
                              QString::fromStdString(m.message));
        }
    }
    if (!extra.isEmpty()) {
        if (!detail.isEmpty()) {
            detail += QStringLiteral("\n");
        }
        detail += extra.join(QStringLiteral("\n"));
    }
    if (!state.validation().error.is_null()) {
        if (!detail.isEmpty()) {
            detail += QStringLiteral("\n");
        }
        detail += QString::fromStdString(state.validation().error.dump());
    }
    validationDetail_->setText(detail);
}

void ViewerPanel::updateBoundingBox(const viewer::ViewerState& state) {
    if (!state.hasBoundingBox()) {
        bboxLabel_->setText(QStringLiteral("Dimensions: —"));
        return;
    }
    const auto& box = *state.boundingBox();
    // Values calculated from the actual mesh, never hardcoded.
    bboxLabel_->setText(
        QStringLiteral("W %1 × H %2 × D %3 mm").arg(box.spanX()).arg(box.spanY()).arg(
            box.spanZ()));
}

void ViewerPanel::updateMeasurement(const viewer::ViewerState& state) {
    const auto& m = state.measurement();
    if (!m.complete()) {
        measureLabel_->setText(m.hasA ? QStringLiteral("Measurement: pick second point…")
                                      : QStringLiteral("Measurement: —"));
        return;
    }
    const double dist = viewer::measureDistance(m.a, m.b);
    const auto d = viewer::measureDelta(m.a, m.b);
    measureLabel_->setText(QStringLiteral("d=%.3f mm  (dx=%.3f, dy=%.3f, dz=%.3f)")
                               .arg(dist)
                               .arg(d[0])
                               .arg(d[1])
                               .arg(d[2]));
}

void ViewerPanel::updateArtifactDetail(const viewer::ViewerState& state) {
    if (!state.hasArtifact()) {
        artifactDetail_->setText(QStringLiteral("Artifact: —"));
        return;
    }
    const auto& a = state.artifact();
    std::string name = a.path;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    artifactDetail_->setText(
        QStringLiteral("Name: %1\nType: %2\nSize: %3 B\nSHA-256: %4\nJob: %5\nCreated: %6")
            .arg(QString::fromStdString(name), QString::fromStdString(a.type),
                 QString::number(static_cast<qulonglong>(a.sizeBytes)),
                 QString::fromStdString(a.checksum),
                 QString::fromStdString(a.jobId.size() > 8 ? a.jobId.substr(0, 8) : a.jobId),
                 QString::fromStdString(a.createdAt)));
}

void ViewerPanel::setArtifacts(const std::vector<ArtifactRow>& rows) {
    rows_ = rows;
    artifactTable_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const auto& a = rows[static_cast<size_t>(r)].artifact;
        std::string name = a.path;
        const auto slash = name.find_last_of("/\\");
        if (slash != std::string::npos) {
            name = name.substr(slash + 1);
        }
        if (name.empty()) {
            name = a.artifactId.size() > 8 ? a.artifactId.substr(0, 8) : a.artifactId;
        }
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(name));
        nameItem->setData(Qt::UserRole, QString::fromStdString(a.artifactId));
        nameItem->setToolTip(QString::fromStdString(a.path));
        artifactTable_->setItem(r, 0, nameItem);
        artifactTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(a.type)));
        artifactTable_->setItem(
            r, 2, new QTableWidgetItem(QString::number(static_cast<qulonglong>(a.sizeBytes))));
        const QString shortHash = QString::fromStdString(
            a.checksum.size() > 12 ? a.checksum.substr(0, 12) + "…" : a.checksum);
        auto* hashItem = new QTableWidgetItem(shortHash);
        hashItem->setToolTip(QString::fromStdString(a.checksum));
        artifactTable_->setItem(r, 3, hashItem);
        artifactTable_->setItem(
            r, 4,
            new QTableWidgetItem(QString::fromStdString(rows[static_cast<size_t>(r)]
                                                           .validationState)));
        const QString shortJob = QString::fromStdString(
            a.jobId.size() > 8 ? a.jobId.substr(0, 8) : a.jobId);
        auto* jobItem = new QTableWidgetItem(shortJob);
        jobItem->setToolTip(QString::fromStdString(a.jobId));
        artifactTable_->setItem(r, 5, jobItem);
        artifactTable_->setItem(r, 6,
                                new QTableWidgetItem(QString::fromStdString(a.createdAt)));
    }
}

void ViewerPanel::onArtifactCellClicked(int row, int /*column*/) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
        return;
    }
    const std::string id = rows_[static_cast<size_t>(row)].artifact.artifactId;
    if (id.empty()) {
        return;
    }
    emit artifactSelected(id);
}

}  // namespace trinity::ui

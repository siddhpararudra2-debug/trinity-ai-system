#pragma once

// Right inspector: parameters / validation / artifacts + viewer controls.
// Shows artifact name, type, size, SHA-256, validation state, job id and
// created time. Only emits artifactSelected for rows that exist; integrity
// failures are reported by the controller as useful errors.

#include <QWidget>

#include <string>
#include <vector>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/viewer/ViewerState.hpp"

class QLabel;
class QTableWidget;
class QComboBox;
class QCheckBox;
class QPushButton;

namespace trinity::ui {

struct ArtifactRow {
    artifacts::Artifact artifact;
    std::string validationState;  // GENERATED/VALIDATED/VERIFIED/INVALID
};

class ViewerPanel : public QWidget {
    Q_OBJECT

public:
    explicit ViewerPanel(QWidget* parent = nullptr);

    void setState(const viewer::ViewerState& state);
    void setArtifacts(const std::vector<ArtifactRow>& rows);
    void setLoading(bool on);
    void setErrorText(const QString& message);

signals:
    void fitRequested();
    void resetRequested();
    void projectionChanged(int index);  // 0 perspective, 1 orthographic
    void renderModeChanged(int index);  // 0 solid, 1 wireframe
    void gridToggled(bool on);
    void axesToggled(bool on);
    void measureModeToggled(bool on);
    void measureCleared();
    void artifactSelected(const std::string& artifactId);
    void reloadRequested();

private slots:
    void onArtifactCellClicked(int row, int column);

private:
    void updateValidation(const viewer::ViewerState& state);
    void updateBoundingBox(const viewer::ViewerState& state);
    void updateMeasurement(const viewer::ViewerState& state);
    void updateArtifactDetail(const viewer::ViewerState& state);

    QLabel* statusLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QComboBox* projectionBox_ = nullptr;
    QComboBox* renderBox_ = nullptr;
    QCheckBox* gridCheck_ = nullptr;
    QCheckBox* axesCheck_ = nullptr;
    QCheckBox* measureCheck_ = nullptr;
    QLabel* bboxLabel_ = nullptr;
    QLabel* measureLabel_ = nullptr;
    QLabel* validationBadge_ = nullptr;
    QLabel* validationDetail_ = nullptr;
    QLabel* artifactDetail_ = nullptr;
    QTableWidget* artifactTable_ = nullptr;
    std::vector<ArtifactRow> rows_;
    bool suppressSignals_ = false;
};

}  // namespace trinity::ui

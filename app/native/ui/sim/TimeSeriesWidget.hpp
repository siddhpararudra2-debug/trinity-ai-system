#pragma once

// Lightweight time-series chart for the Simulation workspace.
// QPainter only — no viewport overlay, no external plotting library.

#include <QWidget>

#include <vector>

namespace trinity::ui {

struct SeriesPoint {
    double x = 0.0;
    double y = 0.0;
};

class TimeSeriesWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimeSeriesWidget(QWidget* parent = nullptr);

    void setSeries(const std::vector<SeriesPoint>& points, const QString& title,
                   const QString& xLabel, const QString& yLabel);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<SeriesPoint> points_;
    QString title_;
    QString xLabel_;
    QString yLabel_;
};

}  // namespace trinity::ui

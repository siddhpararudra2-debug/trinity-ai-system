#include "TimeSeriesWidget.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>

namespace trinity::ui {

TimeSeriesWidget::TimeSeriesWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TimeSeriesWidget::setSeries(const std::vector<SeriesPoint>& points,
                                 const QString& title, const QString& xLabel,
                                 const QString& yLabel) {
    points_ = points;
    title_ = title;
    xLabel_ = xLabel;
    yLabel_ = yLabel;
    update();
}

void TimeSeriesWidget::clear() {
    points_.clear();
    title_.clear();
    xLabel_.clear();
    yLabel_.clear();
    update();
}

void TimeSeriesWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(24, 26, 32));

    const int marginL = 48;
    const int marginR = 12;
    const int marginT = 28;
    const int marginB = 32;
    const QRect plot(marginL, marginT, width() - marginL - marginR,
                     height() - marginT - marginB);

    painter.setPen(QColor(180, 184, 192));
    painter.drawText(QRect(0, 4, width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
                     title_.isEmpty() ? QStringLiteral("Time series") : title_);

    if (points_.size() < 2 || plot.width() < 10 || plot.height() < 10) {
        painter.setPen(QColor(120, 124, 132));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("No data"));
        return;
    }

    double minX = points_.front().x;
    double maxX = points_.front().x;
    double minY = points_.front().y;
    double maxY = points_.front().y;
    for (const auto& p : points_) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            continue;
        }
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    if (maxX - minX < 1e-12) {
        maxX = minX + 1.0;
    }
    if (maxY - minY < 1e-12) {
        const double pad = std::fabs(maxY) * 0.05 + 1e-3;
        minY -= pad;
        maxY += pad;
    }

    painter.setPen(QPen(QColor(70, 74, 84), 1));
    painter.drawRect(plot);

    auto toPixel = [&](const SeriesPoint& p) {
        const double nx = (p.x - minX) / (maxX - minX);
        const double ny = (p.y - minY) / (maxY - minY);
        return QPointF(plot.left() + nx * plot.width(),
                       plot.bottom() - ny * plot.height());
    };

    painter.setPen(QPen(QColor(80, 170, 255), 2));
    bool started = false;
    QPointF prev;
    for (const auto& p : points_) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            started = false;
            continue;
        }
        const QPointF pt = toPixel(p);
        if (!started) {
            painter.drawPoint(pt);
            started = true;
        } else {
            painter.drawLine(prev, pt);
        }
        prev = pt;
    }

    painter.setPen(QColor(150, 154, 162));
    painter.drawText(QRect(plot.left(), plot.bottom() + 4, plot.width(), 20),
                     Qt::AlignHCenter | Qt::AlignTop,
                     xLabel_.isEmpty() ? QStringLiteral("t") : xLabel_);
    painter.save();
    painter.translate(12, plot.center().y());
    painter.rotate(-90);
    painter.drawText(QRect(-plot.height() / 2, -10, plot.height(), 20),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     yLabel_.isEmpty() ? QStringLiteral("value") : yLabel_);
    painter.restore();
}

}  // namespace trinity::ui

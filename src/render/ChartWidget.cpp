#include "ChartWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>
#include <limits>

namespace erebus::render {
namespace {

// Dark scientific theme
static const QColor BG        {8,  12,  22};
static const QColor GRID      {30, 45,  70};
static const QColor AXIS_COLOR{80, 110, 160};
static const QColor TEXT_COLOR{200, 215, 240};
static const QColor ZERO_LINE {60,  80, 120};

// Format a double nicely for axis labels
QString fmtTick(double v) {
    if (!std::isfinite(v)) return "-";
    if (std::abs(v) >= 1e6 || (std::abs(v) > 0 && std::abs(v) < 1e-3))
        return QString::number(v, 'e', 2);
    if (std::abs(v) < 10.0) return QString::number(v, 'f', 3);
    if (std::abs(v) < 100.0) return QString::number(v, 'f', 2);
    return QString::number(v, 'f', 1);
}

} // anon

// ─── Constructors ─────────────────────────────────────────────────────────────

ChartWidget::ChartWidget(const QString& title, const QString& yLabel,
                         const TelemetryRecorder* recorder,
                         std::vector<ChartSeries> series, QWidget* parent)
    : QWidget(parent), mode_(Mode::TimeSeries), title_(title),
      yLabel_(yLabel), recorder_(recorder), series_(std::move(series))
{
    setMinimumSize(180, 140);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

ChartWidget::ChartWidget(const QString& title, Mode mode, QWidget* parent)
    : QWidget(parent), mode_(mode), title_(title)
{
    setMinimumSize(180, 140);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ChartWidget::setBarData(std::vector<BarItem> items) {
    barItems_ = std::move(items);
    if (isVisible()) update();
}

void ChartWidget::setScatterData(std::vector<ScatterPoint> pts,
                                 const QString& xLabel, const QString& yLabel) {
    scatterPts_    = std::move(pts);
    xLabelScatter_ = xLabel;
    yLabelScatter_ = yLabel;
    if (isVisible()) update();
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void ChartWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Background
    p.fillRect(rect(), BG);

    // Title
    QFont titleFont = p.font();
    titleFont.setPointSize(8);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(TEXT_COLOR);
    p.drawText(QRect(0, 2, width(), 16), Qt::AlignHCenter, title_);

    const QRect plot = plotRect(rect());
    if (plot.width() < 20 || plot.height() < 20) return;

    // Border
    p.setPen(QPen(AXIS_COLOR, 1));
    p.drawRect(plot);

    switch (mode_) {
        case Mode::TimeSeries: drawTimeSeries(p, plot); break;
        case Mode::Bar:        drawBar       (p, plot); break;
        case Mode::Scatter:    drawScatter   (p, plot); break;
    }
}

QRect ChartWidget::plotRect(const QRect& w, int margin) {
    return QRect(margin + 10, 20, w.width() - margin - 18, w.height() - margin - 8);
}

QString ChartWidget::formatValue(double v) { return fmtTick(v); }

// ─── Time Series ─────────────────────────────────────────────────────────────

void ChartWidget::drawTimeSeries(QPainter& p, const QRect& plot) {
    if (!recorder_ || series_.empty()) {
        p.setPen(QColor(120, 120, 150));
        p.drawText(plot, Qt::AlignCenter, "No data");
        return;
    }

    double xMin =  std::numeric_limits<double>::max();
    double xMax = -std::numeric_limits<double>::max();
    double yMin =  std::numeric_limits<double>::max();
    double yMax = -std::numeric_limits<double>::max();
    bool hasAnyData = false;

    for (const auto& s : series_) {
        const std::string key = s.key.toStdString();
        if (!recorder_->has(key)) continue;
        for (const auto& [t, v] : recorder_->get(key)) {
            if (!std::isfinite(t) || !std::isfinite(v)) continue;
            xMin = std::min(xMin, t);
            xMax = std::max(xMax, t);
            yMin = std::min(yMin, v);
            yMax = std::max(yMax, v);
            hasAnyData = true;
        }
    }

    if (!hasAnyData) {
        p.setPen(QColor(120, 120, 150));
        p.drawText(plot, Qt::AlignCenter, "Awaiting telemetry...");
        return;
    }

    if (xMin >= xMax) xMax = xMin + 1.0;
    if (yMin == yMax) {
        const double pad = (yMin == 0.0) ? 1.0 : std::abs(yMin) * 0.1;
        yMin -= pad;
        yMax += pad;
    } else {
        const double yPad = (yMax - yMin) * 0.05;
        yMin -= yPad;
        yMax += yPad;
    }

    const auto toX = [&](double t) {
        return plot.left() + static_cast<int>((t - xMin) / (xMax - xMin) * plot.width());
    };
    const auto toY = [&](double v) {
        return plot.bottom() - static_cast<int>((v - yMin) / (yMax - yMin) * plot.height());
    };

    // Bounded Y-grid ticks (fixed 5 iterations - immune to floating point truncation hangs)
    p.setClipRect(plot);
    QFont tickFont = p.font(); tickFont.setPointSize(7); p.setFont(tickFont);

    constexpr int kYTicks = 5;
    for (int i = 0; i <= kYTicks; ++i) {
        const double frac = static_cast<double>(i) / kYTicks;
        const double yv   = yMin + frac * (yMax - yMin);
        const int py = toY(yv);
        if (py < plot.top() || py > plot.bottom()) continue;

        p.setPen(QPen(GRID, 1, Qt::DotLine));
        p.drawLine(plot.left(), py, plot.right(), py);

        p.setPen(TEXT_COLOR);
        p.setClipping(false);
        p.drawText(QRect(0, py - 7, plot.left() - 2, 14),
                   Qt::AlignRight | Qt::AlignVCenter, fmtTick(yv));
        p.setClipRect(plot);
    }

    // Bounded X-grid ticks (fixed 4 iterations)
    constexpr int kXTicks = 4;
    for (int i = 0; i <= kXTicks; ++i) {
        const double frac = static_cast<double>(i) / kXTicks;
        const double xt   = xMin + frac * (xMax - xMin);
        const int px = toX(xt);
        if (px < plot.left() || px > plot.right()) continue;

        p.setPen(QPen(GRID, 1, Qt::DotLine));
        p.drawLine(px, plot.top(), px, plot.bottom());

        p.setPen(TEXT_COLOR);
        p.setClipping(false);
        p.drawText(QRect(px - 24, plot.bottom() + 2, 48, 12),
                   Qt::AlignCenter, fmtTick(xt / 86400.0));
        p.setClipRect(plot);
    }

    // Y-label (rotated)
    p.setClipping(false);
    p.save();
    p.translate(6, plot.top() + plot.height() / 2);
    p.rotate(-90);
    QFont ylFont = p.font(); ylFont.setPointSize(7); p.setFont(ylFont);
    p.setPen(AXIS_COLOR);
    p.drawText(QRect(-plot.height()/2, -6, plot.height(), 12), Qt::AlignCenter, yLabel_);
    p.restore();
    p.setClipRect(plot);

    // X-label
    p.setClipping(false);
    p.drawText(QRect(plot.left(), height() - 12, plot.width(), 12),
               Qt::AlignCenter, "Time (days)");
    p.setClipRect(plot);

    // Draw series lines
    for (const auto& s : series_) {
        const std::string key = s.key.toStdString();
        if (!recorder_->has(key)) continue;
        const auto& data = recorder_->get(key);
        if (data.size() < 2) continue;

        QPainterPath path;
        bool first = true;
        for (const auto& [t, v] : data) {
            if (!std::isfinite(t) || !std::isfinite(v)) continue;
            const int px = toX(t);
            const int py = toY(v);
            if (first) { path.moveTo(px, py); first = false; }
            else        path.lineTo(px, py);
        }
        p.setPen(QPen(s.color, 1.5F));
        p.drawPath(path);
    }

    p.setClipping(false);

    // Legend
    if (series_.size() > 1) {
        int ly = plot.bottom() - static_cast<int>(series_.size()) * 13 - 2;
        for (const auto& s : series_) {
            p.fillRect(plot.right() - 42, ly, 10, 8, s.color);
            p.setPen(TEXT_COLOR);
            QFont lf = p.font(); lf.setPointSize(6); p.setFont(lf);
            p.drawText(plot.right() - 30, ly - 1, 29, 10,
                       Qt::AlignLeft | Qt::AlignVCenter, s.label);
            ly += 13;
        }
    }
}

// ─── Bar Chart ────────────────────────────────────────────────────────────────

void ChartWidget::drawBar(QPainter& p, const QRect& plot) {
    if (barItems_.empty()) {
        p.setPen(QColor(120, 120, 150));
        p.drawText(plot, Qt::AlignCenter, "No data");
        return;
    }

    double vMin = 0.0, vMax = 0.0;
    for (const auto& item : barItems_) {
        if (!std::isfinite(item.value)) continue;
        vMin = std::min(vMin, item.value);
        vMax = std::max(vMax, item.value);
    }
    if (vMin == vMax) vMax = vMin + 1.0;

    const double yPad = (vMax - vMin) * 0.08;
    vMin -= yPad; vMax += yPad;

    const auto toY = [&](double v) {
        return plot.bottom() - static_cast<int>((v - vMin) / (vMax - vMin) * plot.height());
    };
    const int zeroY = toY(0.0);

    // Bounded Grid ticks
    p.setClipRect(plot);
    QFont tf = p.font(); tf.setPointSize(7); p.setFont(tf);
    constexpr int kBarTicks = 5;
    for (int i = 0; i <= kBarTicks; ++i) {
        const double frac = static_cast<double>(i) / kBarTicks;
        const double yv   = vMin + frac * (vMax - vMin);
        const int py = toY(yv);
        if (py < plot.top() || py > plot.bottom()) continue;

        p.setPen(QPen(GRID, 1, Qt::DotLine));
        p.drawLine(plot.left(), py, plot.right(), py);

        p.setPen(TEXT_COLOR);
        p.setClipping(false);
        p.drawText(QRect(0, py - 7, plot.left() - 2, 14),
                   Qt::AlignRight | Qt::AlignVCenter, fmtTick(yv));
        p.setClipRect(plot);
    }
    p.setPen(QPen(ZERO_LINE, 1));
    p.drawLine(plot.left(), zeroY, plot.right(), zeroY);

    // Bars
    const int n    = static_cast<int>(barItems_.size());
    const int barW = std::max(2, (plot.width() - 4) / n - 2);
    const int gap  = std::max(1, (plot.width() - 4 - n * barW) / (n + 1));

    for (int i = 0; i < n; ++i) {
        const auto& item = barItems_[i];
        if (!std::isfinite(item.value)) continue;
        const int x       = plot.left() + gap + i * (barW + gap);
        const int barTop  = toY(std::max(0.0, item.value));
        const int barBot  = toY(std::min(0.0, item.value));
        const QRect bar(x, barTop, barW, std::max(1, barBot - barTop));

        p.fillRect(bar, item.color);
        p.setPen(QPen(item.color.lighter(130), 1));
        p.drawRect(bar);

        // X label
        p.setClipping(false);
        p.setPen(TEXT_COLOR);
        p.save();
        const QString lbl = item.label.length() > 5 ? item.label.left(5) : item.label;
        p.translate(x + barW / 2, plot.bottom() + 2);
        p.rotate(-45);
        QFont lf = p.font(); lf.setPointSize(6); p.setFont(lf);
        p.drawText(QPoint(0, 0), lbl);
        p.restore();
        p.setClipRect(plot);
    }
    p.setClipping(false);
}

// ─── Scatter Plot ─────────────────────────────────────────────────────────────

void ChartWidget::drawScatter(QPainter& p, const QRect& plot) {
    if (scatterPts_.empty()) {
        p.setPen(QColor(120, 120, 150));
        p.drawText(plot, Qt::AlignCenter, "No data");
        return;
    }

    double xMin =  std::numeric_limits<double>::max();
    double xMax = -std::numeric_limits<double>::max();
    double yMin =  std::numeric_limits<double>::max();
    double yMax = -std::numeric_limits<double>::max();
    for (const auto& pt : scatterPts_) {
        if (!std::isfinite(pt.x) || !std::isfinite(pt.y)) continue;
        xMin = std::min(xMin, pt.x); xMax = std::max(xMax, pt.x);
        yMin = std::min(yMin, pt.y); yMax = std::max(yMax, pt.y);
    }
    if (xMin >= xMax) xMax = xMin + 1.0;
    if (yMin >= yMax) yMax = yMin + 1.0;

    const double xPad = (xMax - xMin) * 0.08;
    const double yPad = (yMax - yMin) * 0.08;
    xMin -= xPad; xMax += xPad;
    yMin -= yPad; yMax += yPad;

    const auto toX = [&](double v) {
        return plot.left() + static_cast<int>((v - xMin) / (xMax - xMin) * plot.width());
    };
    const auto toY = [&](double v) {
        return plot.bottom() - static_cast<int>((v - yMin) / (yMax - yMin) * plot.height());
    };

    // Bounded Grid ticks
    p.setClipRect(plot);
    QFont tf = p.font(); tf.setPointSize(7); p.setFont(tf);

    constexpr int kScatterTicks = 5;
    for (int i = 0; i <= kScatterTicks; ++i) {
        const double frac = static_cast<double>(i) / kScatterTicks;
        const double xv   = xMin + frac * (xMax - xMin);
        const int px = toX(xv);

        p.setPen(QPen(GRID, 1, Qt::DotLine));
        p.drawLine(px, plot.top(), px, plot.bottom());

        p.setPen(TEXT_COLOR);
        p.setClipping(false);
        p.drawText(QRect(px - 20, plot.bottom() + 2, 40, 12), Qt::AlignCenter, fmtTick(xv));
        p.setClipRect(plot);
    }

    for (int i = 0; i <= kScatterTicks; ++i) {
        const double frac = static_cast<double>(i) / kScatterTicks;
        const double yv   = yMin + frac * (yMax - yMin);
        const int py = toY(yv);

        p.setPen(QPen(GRID, 1, Qt::DotLine));
        p.drawLine(plot.left(), py, plot.right(), py);

        p.setPen(TEXT_COLOR);
        p.setClipping(false);
        p.drawText(QRect(0, py - 7, plot.left() - 2, 14),
                   Qt::AlignRight | Qt::AlignVCenter, fmtTick(yv));
        p.setClipRect(plot);
    }

    // Points
    for (const auto& pt : scatterPts_) {
        if (!std::isfinite(pt.x) || !std::isfinite(pt.y)) continue;
        const int px = toX(pt.x), py = toY(pt.y);
        p.setBrush(pt.color);
        p.setPen(QPen(pt.color.lighter(140), 1));
        p.drawEllipse(QPoint(px, py), 3, 3);
        if (!pt.label.isEmpty()) {
            p.setPen(TEXT_COLOR);
            QFont lf = p.font(); lf.setPointSize(6); p.setFont(lf);
            p.drawText(px + 4, py - 2, pt.label);
        }
    }
    p.setClipping(false);

    // Axis labels
    p.setPen(AXIS_COLOR);
    QFont af = p.font(); af.setPointSize(7); p.setFont(af);
    p.drawText(QRect(plot.left(), height() - 12, plot.width(), 12),
               Qt::AlignCenter, xLabelScatter_);
    p.save();
    p.translate(6, plot.top() + plot.height() / 2);
    p.rotate(-90);
    p.drawText(QRect(-plot.height()/2, -6, plot.height(), 12),
               Qt::AlignCenter, yLabelScatter_);
    p.restore();
}

} // namespace erebus::render

#pragma once
#include "../telemetry/TelemetryRecorder.h"
#include <QColor>
#include <QString>
#include <QWidget>
#include <vector>

namespace erebus::render {

// ─── Data types used by ChartWidget ──────────────────────────────────────────

struct ChartSeries {
    QString       key;      // TelemetryRecorder key
    QString       label;    // Legend label
    QColor        color;
};

struct BarItem {
    QString label;
    double  value{};
    QColor  color;
};

struct ScatterPoint {
    double  x{};
    double  y{};
    QColor  color;
    QString label;  // short label drawn near point
};

// ─── ChartWidget ─────────────────────────────────────────────────────────────
// Pure-QPainter chart widget.  Three modes:
//   TimeSeries — reads TelemetryRecorder, draws multi-line time-series plot.
//   Bar        — snapshot bar chart from BarItem vector.
//   Scatter    — 2D scatter / phase-space plot.

class ChartWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { TimeSeries, Bar, Scatter };

    // TimeSeries constructor
    explicit ChartWidget(const QString& title, const QString& yLabel,
                         const TelemetryRecorder* recorder,
                         std::vector<ChartSeries> series,
                         QWidget* parent = nullptr);

    // Bar / Scatter constructor (recorder unused)
    explicit ChartWidget(const QString& title, Mode mode, QWidget* parent = nullptr);

    void setBarData   (std::vector<BarItem>     items);
    void setScatterData(std::vector<ScatterPoint> pts,
                        const QString& xLabel, const QString& yLabel);

    [[nodiscard]] QSize sizeHint()        const override { return {320, 220}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {200, 160}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void drawTimeSeries(QPainter& p, const QRect& plotArea);
    void drawBar       (QPainter& p, const QRect& plotArea);
    void drawScatter   (QPainter& p, const QRect& plotArea);

    static QString formatValue(double v);
    static QRect   plotRect   (const QRect& widget, int margin = 42);

    Mode                       mode_;
    QString                    title_;
    QString                    yLabel_;
    QString                    xLabelScatter_;
    QString                    yLabelScatter_;
    const TelemetryRecorder*   recorder_{};
    std::vector<ChartSeries>   series_;
    std::vector<BarItem>       barItems_;
    std::vector<ScatterPoint>  scatterPts_;
};

} // namespace erebus::render

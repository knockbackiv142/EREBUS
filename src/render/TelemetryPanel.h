#pragma once
#include "../telemetry/TelemetryRecorder.h"
#include "../physics/Simulation.h"
#include "ChartWidget.h"
#include "HeatmapWidget.h"
#include <QDockWidget>
#include <QTimer>
#include <vector>

class QTabWidget;

namespace erebus::render {

class TelemetryPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit TelemetryPanel(QWidget* parent = nullptr);

    void setSimulation(physics::Simulation* sim);

    // Call every 250 ms from MainWindow::updateTelemetry()
    void recordTelemetry();
    void refresh();

    // ── Dedicated recording session API ──────────────────────────────────────
    void startRecording() { recorder_.startSession(); }
    void stopRecording()  { recorder_.stopSession(); }
    [[nodiscard]] bool isRecording() const noexcept { return recorder_.isRecording(); }
    [[nodiscard]] std::size_t sessionRecordCount() const noexcept { return recorder_.sessionRecordCount(); }
    [[nodiscard]] const TelemetryRecorder& recorder() const noexcept { return recorder_; }
    [[nodiscard]] TelemetryRecorder& recorder() noexcept { return recorder_; }

    bool exportSession(const QString& filePath, const QString& presetName = "Default");
    bool exportLiveBuffer(const QString& filePath, const QString& presetName = "Default");

private:
    void createConservationTab (QTabWidget*);
    void createPBHTab          (QTabWidget*);
    void createEarthTab        (QTabWidget*);
    void createOuterPlanetsTab (QTabWidget*);
    void createTNOTab          (QTabWidget*);
    void createMapsTab         (QTabWidget*);
    void createStatsTab        (QTabWidget*);

    void updateSnapshotCharts();

    ChartWidget* makeTS(const QString& title, const QString& yLabel,
                        std::vector<ChartSeries> series);

    static QWidget* makeScrollTab(QTabWidget* tabs, const QString& label,
                                   std::vector<QWidget*> widgets, int cols = 2);

    physics::Simulation*  simulation_{};
    erebus::TelemetryRecorder recorder_;
    std::vector<ChartWidget*>  tsCharts_;    // all time-series charts
    std::vector<HeatmapWidget*> heatmaps_;

    // Snapshot charts (Tab 7 Statistics)
    ChartWidget* phaseChart_{};
    ChartWidget* eccBar_{};
    ChartWidget* smaBar_{};
    ChartWidget* keBar_{};
};

} // namespace erebus::render

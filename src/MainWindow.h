#pragma once

#include "physics/Simulation.h"
#include "render/UniverseView.h"
#include <QLabel>
#include <QMainWindow>

class QDoubleSpinBox;
class QComboBox;
class QSlider;

namespace erebus {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private slots:
    void toggleSimulation();
    void updateTelemetry();
    void applyPbhMass(double value);
    void updateSelectedBody(int index);
    void applyPbhPreset(int index);

private:
    physics::Simulation simulation_;
    render::UniverseView* view_{};
    QLabel* telemetry_{};
    QDoubleSpinBox* massControl_{};
    QDoubleSpinBox* timestepControl_{};
    QLabel* selectionTelemetry_{};
};

} // namespace erebus

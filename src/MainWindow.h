#pragma once

#include "physics/Simulation.h"
#include "render/UniverseView.h"
#include "render/TelemetryPanel.h"
#include "render/NotesPanel.h"
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>

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
    void toggleTelemetryRecording();
    void exportLiveTelemetry();

private:
    physics::Simulation      simulation_;
    render::UniverseView*    view_{};
    render::TelemetryPanel*  telPanel_{};
    render::NotesPanel*      notesPanel_{};
    QLabel*                  telemetry_{};
    QDoubleSpinBox*          massControl_{};
    QDoubleSpinBox*          timestepControl_{};
    QLabel*                  selectionTelemetry_{};
    QPushButton*             recordBtn_{};
    QPushButton*             exportLiveBtn_{};
    QLabel*                  recordStatus_{};
};

} // namespace erebus

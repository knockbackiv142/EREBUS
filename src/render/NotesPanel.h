#pragma once
#include "../physics/Simulation.h"
#include <QDockWidget>
#include <QTextEdit>

namespace erebus::render {

// Scientific notes dock widget.
// Displays auto-generated rich-text notes about the active PBH preset,
// paper citations, Schwarzschild radius, Hill sphere, and OGLE context.
class NotesPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit NotesPanel(QWidget* parent = nullptr);

    void setSimulation(const physics::Simulation* sim);

    // Call when the PBH preset changes (preset=0,1,2)
    void updateForPreset(int preset);

private:
    void rebuild();

    const physics::Simulation* simulation_{};
    int   activePreset_{0};
    QTextEdit* text_{};
};

} // namespace erebus::render

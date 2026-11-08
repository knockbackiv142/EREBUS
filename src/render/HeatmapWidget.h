#pragma once
#include "../physics/Simulation.h"
#include <QImage>
#include <QString>
#include <QWidget>

namespace erebus::render {

enum class HeatmapMode {
    GravPotential,   // log-stretched Newtonian potential, viridis colormap
    TidalForce,      // |g(x,y) - g(0,0)| from Sun, plasma colormap
    GravAccel,       // log10(|total grav accel|), plasma colormap
};

class HeatmapWidget : public QWidget {
    Q_OBJECT
public:
    explicit HeatmapWidget(HeatmapMode mode, const QString& title,
                           QWidget* parent = nullptr);

    void setSimulation(const physics::Simulation* sim) { simulation_ = sim; dirty_ = true; }
    void setExtentAu(double au) { extentAu_ = au; dirty_ = true; }

    // Call to trigger recompute + repaint
    void refresh() { dirty_ = true; if (isVisible()) update(); }
    void markDirty() noexcept { dirty_ = true; }

    [[nodiscard]] QSize sizeHint()        const override { return {340, 300}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {240, 220}; }

protected:
    void paintEvent  (QPaintEvent*) override;
    void resizeEvent (QResizeEvent*) override { dirty_ = true; }

private:
    void recompute();

    // Colormaps: t in [0,1]
    static QRgb  viridis (double t);
    static QRgb  plasma  (double t);

    HeatmapMode              mode_;
    QString                  title_;
    const physics::Simulation* simulation_{};
    double                   extentAu_{80.0};
    bool                     dirty_{true};
    QImage                   image_;
    double                   vMin_{0.0};
    double                   vMax_{1.0};
};

} // namespace erebus::render

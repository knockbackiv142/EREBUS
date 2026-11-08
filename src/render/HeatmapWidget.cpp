#include "HeatmapWidget.h"
#include <QPainter>
#include <QFontMetrics>
#include <cmath>
#include <algorithm>
#include <array>

namespace erebus::render {
namespace {

constexpr double AU = 1.495978707e11;
constexpr double G  = 6.67430e-11;
constexpr double c  = 2.99792458e8;

// ── Colormaps ─────────────────────────────────────────────────────────────────
// 10-stop Viridis approximation (dark purple → teal → yellow)
static const std::array<std::array<uint8_t,3>, 10> VIRIDIS_STOPS = {{
    {68,  1,   84 },
    {72,  40,  120},
    {62,  74,  137},
    {49,  104, 142},
    {38,  130, 142},
    {31,  158, 137},
    {53,  183, 121},
    {110, 206, 88 },
    {181, 222, 43 },
    {253, 231, 37 }
}};

// 10-stop Plasma approximation (dark blue → magenta → yellow)
static const std::array<std::array<uint8_t,3>, 10> PLASMA_STOPS = {{
    {13,  8,   135},
    {75,  3,   161},
    {125, 3,   168},
    {168, 34,  150},
    {203, 70,  121},
    {229, 107, 93 },
    {248, 148, 65 },
    {253, 195, 40 },
    {240, 249, 33 },
    {240, 249, 33 }
}};

QRgb interpColormap(const std::array<std::array<uint8_t,3>,10>& stops, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double scaled = t * 9.0;
    const int    lo     = static_cast<int>(scaled);
    const int    hi     = std::min(lo + 1, 9);
    const double f      = scaled - lo;
    const auto& a = stops[lo];
    const auto& b = stops[hi];
    return qRgb(static_cast<int>(a[0] + f*(b[0]-a[0])),
                static_cast<int>(a[1] + f*(b[1]-a[1])),
                static_cast<int>(a[2] + f*(b[2]-a[2])));
}

} // anon

// ─── Public ───────────────────────────────────────────────────────────────────

HeatmapWidget::HeatmapWidget(HeatmapMode mode, const QString& title, QWidget* parent)
    : QWidget(parent), mode_(mode), title_(title)
{
    setMinimumSize(240, 220);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

QRgb HeatmapWidget::viridis(double t) { return interpColormap(VIRIDIS_STOPS, t); }
QRgb HeatmapWidget::plasma (double t) { return interpColormap(PLASMA_STOPS,  t); }

// ─── Recompute ────────────────────────────────────────────────────────────────

void HeatmapWidget::recompute() {
    dirty_ = false;
    if (!simulation_) { image_ = QImage(); return; }

    // Lightweight resolution for smooth performance on integrated GPUs (Intel Iris Xe)
    constexpr int RES = 72;
    image_ = QImage(RES, RES, QImage::Format_RGB32);

    const auto& bodies = simulation_->bodies();
    const double ext   = extentAu_;

    // Sample scalar field at each grid cell
    std::vector<double> field(RES * RES);
    double fMin =  std::numeric_limits<double>::max();
    double fMax = -std::numeric_limits<double>::max();

    // Precompute body positions in AU (ecliptic XY plane projection)
    struct BPos { double xAU; double yAU; double massKg; bool massive; };
    std::vector<BPos> bpos;
    bpos.reserve(bodies.size());
    for (const auto& b : bodies)
        bpos.push_back({b.positionM.x()/AU, b.positionM.y()/AU, b.massKg, b.massive});

    for (int j = 0; j < RES; ++j) {
        const double xAU = -ext + 2.0 * ext * j / (RES - 1);
        for (int k = 0; k < RES; ++k) {
            const double yAU = -ext + 2.0 * ext * k / (RES - 1);
            double val = 0.0;

            if (mode_ == HeatmapMode::GravPotential) {
                // Softened Newtonian potential Φ = −Σ G*m/sqrt(r²+ε²)
                for (const auto& b : bpos) {
                    if (!b.massive) continue;
                    const double dx = xAU - b.xAU;
                    const double dy = yAU - b.yAU;
                    const double soft = 0.001; // 0.001 AU softening
                    const double r    = std::sqrt(dx*dx + dy*dy + soft*soft);
                    val -= G * b.massKg / (r * AU);  // J/kg
                }
                // log-stretch for dynamic range
                if (val < 0.0) val = std::log(-val);

            } else if (mode_ == HeatmapMode::TidalForce) {
                // Tidal acceleration relative to Sun: |g(r) - g(0)| from Sun only
                const auto& sun = bpos[0];
                // g at (xAU,yAU) due to Sun
                const double dx0 = xAU - sun.xAU;
                const double dy0 = yAU - sun.yAU;
                const double r0  = std::max(std::sqrt(dx0*dx0+dy0*dy0), 0.001) * AU;
                const double gx0 = -G * sun.massKg * dx0*AU / (r0*r0*r0);
                const double gy0 = -G * sun.massKg * dy0*AU / (r0*r0*r0);
                // g at origin due to Sun
                const double dxC = -sun.xAU, dyC = -sun.yAU;
                const double rC  = std::max(std::sqrt(dxC*dxC+dyC*dyC), 0.001) * AU;
                const double gxC = -G * sun.massKg * dxC*AU / (rC*rC*rC);
                const double gyC = -G * sun.massKg * dyC*AU / (rC*rC*rC);
                val = std::log(std::max(std::hypot(gx0-gxC, gy0-gyC), 1e-15));

            } else { // GravAccel
                // Total grav acceleration magnitude from all massive bodies
                double ax = 0.0, ay = 0.0;
                for (const auto& b : bpos) {
                    if (!b.massive) continue;
                    const double dx = (b.xAU - xAU) * AU;
                    const double dy = (b.yAU - yAU) * AU;
                    const double soft = 0.001 * AU;
                    const double r3   = std::pow(dx*dx + dy*dy + soft*soft, 1.5);
                    ax += G * b.massKg * dx / r3;
                    ay += G * b.massKg * dy / r3;
                }
                val = std::log10(std::max(std::hypot(ax, ay), 1e-20));
            }

            field[j * RES + k] = val;
            fMin = std::min(fMin, val);
            fMax = std::max(fMax, val);
        }
    }

    vMin_ = fMin;
    vMax_ = fMax;
    if (fMin >= fMax) fMax = fMin + 1.0;

    const bool usePlasma = (mode_ != HeatmapMode::GravPotential);

    for (int j = 0; j < RES; ++j) {
        for (int k = 0; k < RES; ++k) {
            const double t = (field[j * RES + k] - fMin) / (fMax - fMin);
            const QRgb   c = usePlasma ? plasma(t) : viridis(t);
            // image: row=k (y), col=j (x) — flip y so +y is up
            image_.setPixel(j, RES - 1 - k, c);
        }
    }
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void HeatmapWidget::paintEvent(QPaintEvent*) {
    if (dirty_) recompute();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(8, 12, 22));

    // Title
    QFont tf = p.font(); tf.setPointSize(8); tf.setBold(true); p.setFont(tf);
    p.setPen(QColor(200, 215, 240));
    p.drawText(QRect(0, 2, width(), 16), Qt::AlignHCenter, title_);

    if (image_.isNull()) {
        p.setPen(QColor(120, 120, 150));
        p.drawText(rect(), Qt::AlignCenter, "No simulation");
        return;
    }

    // Map area (leave margins for labels + colorbar)
    const int cbW    = 18;  // colorbar width
    const int margin = 44;
    const QRect mapR(margin + 4, 20, width() - margin - cbW - 10, height() - margin - 8);

    // Draw heatmap scaled to mapR
    p.drawImage(mapR, image_);

    // ── Colorbar ──────────────────────────────────────────────────────────────
    const int cbX = mapR.right() + 4;
    const int cbH = mapR.height();
    const int cbY = mapR.top();
    QImage cbImg(1, cbH, QImage::Format_RGB32);
    const bool usePlasma = (mode_ != HeatmapMode::GravPotential);
    for (int row = 0; row < cbH; ++row) {
        const double t = 1.0 - static_cast<double>(row) / cbH;
        cbImg.setPixel(0, row, usePlasma ? plasma(t) : viridis(t));
    }
    p.drawImage(QRect(cbX, cbY, cbW, cbH), cbImg);
    p.setPen(QColor(80, 110, 160));
    p.drawRect(cbX, cbY, cbW, cbH);

    // Colorbar tick labels (5 ticks)
    QFont lf = p.font(); lf.setPointSize(6); p.setFont(lf);
    p.setPen(QColor(200, 215, 240));
    for (int tick = 0; tick <= 4; ++tick) {
        const double frac = static_cast<double>(tick) / 4.0;
        const double val  = vMin_ + frac * (vMax_ - vMin_);
        const int    py   = cbY + cbH - static_cast<int>(frac * cbH);
        p.drawLine(cbX + cbW, py, cbX + cbW + 3, py);
        p.drawText(QRect(cbX + cbW + 4, py - 7, 32, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(val, 'g', 3));
    }

    // ── Axis labels ───────────────────────────────────────────────────────────
    const double ext = extentAu_;
    const auto axLabel = [&](double auVal) { return QString::number(auVal, 'g', 3) + " AU"; };
    // X-axis
    p.drawText(QRect(mapR.left()-5, mapR.bottom()+3, 60, 12),
               Qt::AlignLeft, axLabel(-ext));
    p.drawText(QRect(mapR.left() + mapR.width()/2 - 20, mapR.bottom()+3, 40, 12),
               Qt::AlignCenter, "0");
    p.drawText(QRect(mapR.right()-55, mapR.bottom()+3, 60, 12),
               Qt::AlignRight, axLabel(ext));
    // Y-axis
    p.save();
    p.translate(6, mapR.top() + mapR.height()/2);
    p.rotate(-90);
    p.drawText(QRect(-mapR.height()/2, -6, mapR.height(), 12),
               Qt::AlignCenter, "Y (AU)");
    p.restore();
    p.drawText(QRect(2, mapR.top()-1, mapR.left()-4, 10),
               Qt::AlignRight | Qt::AlignTop, axLabel(ext));
    p.drawText(QRect(2, mapR.bottom()-9, mapR.left()-4, 10),
               Qt::AlignRight | Qt::AlignBottom, axLabel(-ext));

    // ── Body overlays ─────────────────────────────────────────────────────────
    if (simulation_) {
        const auto& bodies = simulation_->bodies();
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const auto& b = bodies[i];
            const double bxAU = b.positionM.x() / AU;
            const double byAU = b.positionM.y() / AU;
            if (std::abs(bxAU) > ext * 1.05 || std::abs(byAU) > ext * 1.05) continue;

            const int px = mapR.left() + static_cast<int>((bxAU + ext) / (2.0*ext) * mapR.width());
            const int py = mapR.bottom() - static_cast<int>((byAU + ext) / (2.0*ext) * mapR.height());

            QColor dot(120, 185, 240);
            int    dotR = 3;
            if (b.name == "Sun")        { dot = QColor(255, 190, 45);  dotR = 5; }
            else if (b.hypothetical)    { dot = QColor(205, 90, 245);  dotR = 4; }
            else if (i < 9)             { dot = QColor(100, 220, 160); dotR = 3; }

            p.setPen(Qt::NoPen);
            p.setBrush(dot);
            p.drawEllipse(QPoint(px, py), dotR, dotR);
        }
    }

    // Border
    p.setPen(QColor(30, 45, 70));
    p.setBrush(Qt::NoBrush);
    p.drawRect(mapR);
}

} // namespace erebus::render

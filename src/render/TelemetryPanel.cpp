#include "TelemetryPanel.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace erebus::render {
namespace {

constexpr double AU      = 1.495978707e11;

// Body palette — indexed by body order (0=Sun skipped for most charts)
static const std::array<QColor, 14> PALETTE = {{
    QColor(255, 190,  45),  // 0 Sun
    QColor(170, 165, 155),  // 1 Mercury
    QColor(235, 190, 110),  // 2 Venus
    QColor( 75, 145, 235),  // 3 Earth
    QColor(210,  95,  65),  // 4 Mars
    QColor(205, 155, 110),  // 5 Jupiter
    QColor(220, 190, 125),  // 6 Saturn
    QColor(110, 210, 220),  // 7 Uranus
    QColor( 70, 105, 225),  // 8 Neptune
    QColor(120, 185, 240),  // 9 Sedna
    QColor(160, 220, 180),  // 10 VP113
    QColor(235, 190, 110),  // 11 Goblin
    QColor(235, 125,  90),  // 12 Drac
    QColor(205,  90, 245),  // 13 PBH
}};

} // anon

// ─── Constructor ──────────────────────────────────────────────────────────────

TelemetryPanel::TelemetryPanel(QWidget* parent) : QDockWidget("Telemetry & Science Graphs", parent) {
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                QDockWidget::DockWidgetClosable);
    setMinimumHeight(280);

    auto* tabs = new QTabWidget;
    tabs->setTabPosition(QTabWidget::North);
    tabs->setStyleSheet("QTabWidget::pane { border: 1px solid #1e2d46; background: #08080e; }"
                        "QTabBar::tab { background: #0d1424; color: #aaaacc; padding: 4px 8px; }"
                        "QTabBar::tab:selected { background: #182540; color: #d8e8ff; }");
    setWidget(tabs);

    createConservationTab (tabs);
    createPBHTab          (tabs);
    createEarthTab        (tabs);
    createOuterPlanetsTab (tabs);
    createTNOTab          (tabs);
    createMapsTab         (tabs);
    createStatsTab        (tabs);

    connect(tabs, &QTabWidget::currentChanged, this, [this](int) { refresh(); });
}

// ─── setSimulation ────────────────────────────────────────────────────────────

void TelemetryPanel::setSimulation(physics::Simulation* sim) {
    simulation_ = sim;
    for (auto* hm : heatmaps_) hm->setSimulation(sim);
}

// ─── Helper: make a scroll tab with a grid of widgets ─────────────────────────

QWidget* TelemetryPanel::makeScrollTab(QTabWidget* tabs, const QString& label,
                                        std::vector<QWidget*> widgets, int cols) {
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);
    grid->setSpacing(4);
    grid->setContentsMargins(4, 4, 4, 4);
    int i = 0;
    for (auto* w : widgets) {
        grid->addWidget(w, i / cols, i % cols);
        ++i;
    }
    auto* scroll = new QScrollArea;
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: #08080e; }");
    tabs->addTab(scroll, label);
    return scroll;
}

// ─── Helper: make a time-series chart ────────────────────────────────────────

ChartWidget* TelemetryPanel::makeTS(const QString& title, const QString& yLabel,
                                     std::vector<ChartSeries> series) {
    auto* w = new ChartWidget(title, yLabel, &recorder_, std::move(series));
    tsCharts_.push_back(w);
    return w;
}

// ─── Tab 1: Conservation Laws ─────────────────────────────────────────────────

void TelemetryPanel::createConservationTab(QTabWidget* tabs) {
    auto* totalE = makeTS("Total Mechanical Energy", "E (J)",
        {{"energy.total", "Total E", QColor(240,240,100)}});
    auto* ke = makeTS("Kinetic Energy", "KE (J)",
        {{"energy.kinetic", "KE", QColor(80,200,120)}});
    auto* pe = makeTS("Potential Energy", "PE (J)",
        {{"energy.potential", "PE", QColor(220,80,80)}});
    auto* Lz = makeTS("Angular Momentum Magnitude", "|L| (kg·m²/s)",
        {{"momentum.angular", "L", QColor(180,120,255)}});
    makeScrollTab(tabs, "Conservation", {totalE, ke, pe, Lz}, 2);
}

// ─── Tab 2: PBH Dynamics ─────────────────────────────────────────────────────

void TelemetryPanel::createPBHTab(QTabWidget* tabs) {
    auto* dist = makeTS("PBH Distance from Sun", "Distance (AU)",
        {{"pbh.distance", "PBH-Sun", PALETTE[13]}});
    auto* vel = makeTS("PBH Velocity", "Speed (km/s)",
        {{"pbh.velocity", "PBH vel", PALETTE[13]}});
    auto* sma = makeTS("PBH Semi-Major Axis", "a (AU)",
        {{"pbh.sma", "PBH a", QColor(205,90,245)}});
    auto* ecc = makeTS("PBH Eccentricity", "e",
        {{"pbh.ecc", "PBH e", QColor(235,150,245)}});
    auto* angle = makeTS("PBH Angular Position", "θ (deg)",
        {{"pbh.angle", "PBH θ", QColor(185,80,220)}});
    auto* goblin = makeTS("Goblin–PBH Distance", "Distance (AU)",
        {{"goblin.dist_pbh", "Goblin-PBH", PALETTE[11]}});
    makeScrollTab(tabs, "PBH Dynamics", {dist, vel, sma, ecc, angle, goblin}, 2);
}

// ─── Tab 3: Earth & Inner ────────────────────────────────────────────────────

void TelemetryPanel::createEarthTab(QTabWidget* tabs) {
    auto* eSMA  = makeTS("Earth Semi-Major Axis", "a (AU)",
        {{"earth.sma", "Earth a", PALETTE[3]}});
    auto* eEcc  = makeTS("Earth Eccentricity", "e",
        {{"earth.ecc", "Earth e", PALETTE[3]}});
    auto* eInc  = makeTS("Earth Inclination", "i (deg)",
        {{"earth.inc", "Earth i", QColor(100,180,255)}});
    auto* ePBH  = makeTS("Earth – PBH Distance", "Distance (AU)",
        {{"earth.dist_pbh", "Earth-PBH", PALETTE[13]}});
    auto* eSun  = makeTS("Earth – Sun Distance", "Distance (AU)",
        {{"earth.dist_sun", "Earth-Sun", PALETTE[0]}});
    auto* sunV  = makeTS("Sun Barycenter Velocity", "Speed (m/s)",
        {{"sun.velocity", "Sun vel", PALETTE[0]}});
    makeScrollTab(tabs, "Earth & Inner", {eSMA, eEcc, eInc, ePBH, eSun, sunV}, 2);
}

// ─── Tab 4: Outer Planets ────────────────────────────────────────────────────

void TelemetryPanel::createOuterPlanetsTab(QTabWidget* tabs) {
    auto* nSMA = makeTS("Neptune Semi-Major Axis", "a (AU)",
        {{"neptune.sma", "Neptune a", PALETTE[8]}});
    auto* nEcc = makeTS("Neptune Eccentricity", "e",
        {{"neptune.ecc", "Neptune e", PALETTE[8]}});
    auto* nPBH = makeTS("Neptune – PBH Distance", "Distance (AU)",
        {{"neptune.dist_pbh", "Nept-PBH", PALETTE[13]}});
    auto* jEcc = makeTS("Jupiter Eccentricity", "e",
        {{"jupiter.ecc", "Jupiter e", PALETTE[5]}});

    // Multi-line outer planet inclinations
    auto* outerInc = makeTS("Outer Planet Inclinations", "i (deg)", {
        {"jupiter.inc",  "Jupiter",  PALETTE[5]},
        {"saturn.inc",   "Saturn",   PALETTE[6]},
        {"uranus.inc",   "Uranus",   PALETTE[7]},
        {"neptune.inc",  "Neptune",  PALETTE[8]},
    });
    makeScrollTab(tabs, "Outer Planets", {nSMA, nEcc, nPBH, jEcc, outerInc}, 2);
}

// ─── Tab 5: TNOs ─────────────────────────────────────────────────────────────

void TelemetryPanel::createTNOTab(QTabWidget* tabs) {
    auto* sedEcc  = makeTS("Sedna Eccentricity",  "e", {{"sedna.ecc",  "Sedna",  PALETTE[9]}});
    auto* vpEcc   = makeTS("VP113 Eccentricity",  "e", {{"vp113.ecc",  "VP113",  PALETTE[10]}});
    auto* gobEcc  = makeTS("Goblin Eccentricity", "e", {{"goblin.ecc", "Goblin", PALETTE[11]}});
    auto* dracEcc = makeTS("Drac Eccentricity",   "e", {{"drac.ecc",   "Drac",   PALETTE[12]}});

    // All TNO eccentricities on one chart
    auto* allTNO = makeTS("All TNO Eccentricities", "e", {
        {"sedna.ecc",  "Sedna",  PALETTE[9]},
        {"vp113.ecc",  "VP113",  PALETTE[10]},
        {"goblin.ecc", "Goblin", PALETTE[11]},
        {"drac.ecc",   "Drac",   PALETTE[12]},
    });
    makeScrollTab(tabs, "TNOs", {sedEcc, vpEcc, gobEcc, dracEcc, allTNO}, 2);
}

// ─── Tab 6: Field Maps ───────────────────────────────────────────────────────

void TelemetryPanel::createMapsTab(QTabWidget* tabs) {
    auto* hmPot   = new HeatmapWidget(HeatmapMode::GravPotential, "Gravitational Potential (Viridis)");
    auto* hmTidal = new HeatmapWidget(HeatmapMode::TidalForce,    "Tidal Force Magnitude (Plasma)");
    auto* hmAccel = new HeatmapWidget(HeatmapMode::GravAccel,     "Gravitational Acceleration (Plasma)");

    heatmaps_.push_back(hmPot);
    heatmaps_.push_back(hmTidal);
    heatmaps_.push_back(hmAccel);

    // Heatmap refresh timer: only update active/visible heatmaps every 4s
    auto* hmTimer = new QTimer(this);
    hmTimer->setInterval(4000);
    connect(hmTimer, &QTimer::timeout, this, [this] {
        for (auto* hm : heatmaps_) {
            if (hm->isVisible()) hm->refresh();
            else hm->markDirty();
        }
    });
    hmTimer->start();

    makeScrollTab(tabs, "Field Maps", {hmPot, hmTidal, hmAccel}, 2);
}

// ─── Tab 7: Statistics ───────────────────────────────────────────────────────

void TelemetryPanel::createStatsTab(QTabWidget* tabs) {
    phaseChart_ = new ChartWidget("a–e Phase Space (all bodies)", ChartWidget::Mode::Scatter);
    eccBar_     = new ChartWidget("Current Eccentricity",         ChartWidget::Mode::Bar);
    smaBar_     = new ChartWidget("Current Semi-Major Axis (AU)", ChartWidget::Mode::Bar);
    keBar_      = new ChartWidget("KE Fraction (×10³⁰ J)",       ChartWidget::Mode::Bar);
    makeScrollTab(tabs, "Statistics", {phaseChart_, eccBar_, smaBar_, keBar_}, 2);
}

// ─── recordTelemetry ─────────────────────────────────────────────────────────

void TelemetryPanel::recordTelemetry() {
    if (!simulation_) return;

    const auto tel = simulation_->telemetry();
    const double t  = tel.timeS;

    std::map<std::string, double> snap;
    const auto recVal = [&](const std::string& key, double val) {
        recorder_.record(key, t, val);
        snap[key] = val;
    };

    recVal("energy.total",     tel.totalEnergyJ);
    recVal("energy.kinetic",   tel.kineticEnergyJ);
    recVal("energy.potential", tel.potentialEnergyJ);
    recVal("momentum.linear",  tel.momentumKgMPerS);

    // Angular momentum (manual sum)
    Eigen::Vector3d L = Eigen::Vector3d::Zero();
    for (const auto& b : simulation_->bodies())
        L += b.massKg * b.positionM.cross(b.velocityMPerS);
    recVal("momentum.angular", L.norm());

    // Sun barycenter velocity
    recVal("sun.velocity", simulation_->bodies()[0].velocityMPerS.norm());

    const auto& bodies = simulation_->bodies();

    // Find PBH
    int pbhIdx = -1;
    for (int i = static_cast<int>(bodies.size()) - 1; i >= 0; --i)
        if (bodies[i].hypothetical) { pbhIdx = i; break; }

    if (pbhIdx >= 0) {
        const auto& pbh = bodies[pbhIdx];
        recVal("pbh.distance", pbh.positionM.norm() / AU);
        recVal("pbh.velocity", pbh.velocityMPerS.norm() / 1000.0);
        recVal("pbh.angle",
            std::atan2(pbh.positionM.y(), pbh.positionM.x()) * 180.0 / std::numbers::pi);
        try {
            const auto el = simulation_->orbitalElements(static_cast<std::size_t>(pbhIdx), 0);
            recVal("pbh.sma", el.semiMajorAxisM / AU);
            recVal("pbh.ecc", el.eccentricity);
        } catch (...) {}

        // Goblin (index 11) to PBH
        if (bodies.size() > 11) {
            const double gd = (bodies[11].positionM - pbh.positionM).norm() / AU;
            recVal("goblin.dist_pbh", gd);
        }
    }

    // Helper: record orbital element for body at fixed index
    const auto recEl = [&](std::size_t idx, const char* smaKey, const char* eccKey,
                           const char* incKey) {
        if (idx >= bodies.size()) return;
        try {
            const auto el = simulation_->orbitalElements(idx, 0);
            if (smaKey) recVal(smaKey, el.semiMajorAxisM / AU);
            if (eccKey) recVal(eccKey, el.eccentricity);
            if (incKey) recVal(incKey, el.inclinationRad * 180.0 / std::numbers::pi);
        } catch (...) {}
    };

    // Earth (3)
    recEl(3, "earth.sma", "earth.ecc", "earth.inc");
    if (3 < bodies.size())
        recVal("earth.dist_sun", bodies[3].positionM.norm() / AU);
    if (pbhIdx >= 0 && 3 < bodies.size())
        recVal("earth.dist_pbh",
            (bodies[3].positionM - bodies[pbhIdx].positionM).norm() / AU);

    // Jupiter (5)
    recEl(5, nullptr, "jupiter.ecc", "jupiter.inc");
    // Saturn (6)
    recEl(6, nullptr, "saturn.ecc",  "saturn.inc");
    // Uranus (7)
    recEl(7, nullptr, "uranus.ecc",  "uranus.inc");
    // Neptune (8)
    recEl(8, "neptune.sma", "neptune.ecc", "neptune.inc");
    if (pbhIdx >= 0 && 8 < bodies.size())
        recVal("neptune.dist_pbh",
            (bodies[8].positionM - bodies[pbhIdx].positionM).norm() / AU);

    // TNOs
    recEl(9,  "sedna.sma",  "sedna.ecc",  nullptr);
    recEl(10, nullptr,      "vp113.ecc",  nullptr);
    recEl(11, "goblin.sma", "goblin.ecc", nullptr);
    recEl(12, nullptr,      "drac.ecc",   nullptr);

    // Store in recording session if active
    if (recorder_.isRecording()) {
        recorder_.recordSessionSnapshot(t, snap);
    }
}

// ─── refresh ─────────────────────────────────────────────────────────────────

void TelemetryPanel::refresh() {
    // Only repaint visible charts to keep Intel Iris Xe iGPU and CPU load ultra-low
    for (auto* c : tsCharts_) {
        if (c->isVisible()) c->update();
    }
    for (auto* hm : heatmaps_) {
        if (hm->isVisible()) hm->update();
    }
    if ((phaseChart_ && phaseChart_->isVisible()) ||
        (eccBar_ && eccBar_->isVisible()) ||
        (smaBar_ && smaBar_->isVisible()) ||
        (keBar_ && keBar_->isVisible())) {
        updateSnapshotCharts();
    }
}

// ─── updateSnapshotCharts ────────────────────────────────────────────────────

void TelemetryPanel::updateSnapshotCharts() {
    if (!simulation_) return;
    const auto& bodies = simulation_->bodies();

    std::vector<BarItem>      eccItems, smaItems, keItems;
    std::vector<ScatterPoint> phasePoints;

    for (std::size_t i = 1; i < bodies.size(); ++i) {
        const auto& b = bodies[i];
        const QColor c = PALETTE[std::min(i, std::size_t(13))];
        const double ke = 0.5 * b.massKg * b.velocityMPerS.squaredNorm() / 1e30;
        keItems.push_back({QString::fromStdString(b.name), ke, c});

        try {
            const auto el = simulation_->orbitalElements(i, 0);
            if (!std::isfinite(el.semiMajorAxisM) || el.semiMajorAxisM <= 0.0) continue;
            const double aAU = el.semiMajorAxisM / AU;
            eccItems.push_back  ({QString::fromStdString(b.name), el.eccentricity, c});
            smaItems.push_back  ({QString::fromStdString(b.name), aAU, c});
            phasePoints.push_back({aAU, el.eccentricity, c,
                                   QString::fromStdString(b.name.substr(0, 4))});
        } catch (...) {}
    }

    if (phaseChart_) phaseChart_->setScatterData(phasePoints, "SMA (AU)", "Eccentricity");
    if (eccBar_)     eccBar_->setBarData(eccItems);
    if (smaBar_)     smaBar_->setBarData(smaItems);
    if (keBar_)      keBar_->setBarData(keItems);
}

bool TelemetryPanel::exportSession(const QString& filePath, const QString& presetName) {
    const std::string path = filePath.toStdString();
    if (filePath.endsWith(".json", Qt::CaseInsensitive)) {
        return TelemetryRecorder::exportRecordsToJson(path, recorder_.sessionRecords(), presetName.toStdString());
    } else {
        return TelemetryRecorder::exportRecordsToCsv(path, recorder_.sessionRecords());
    }
}

bool TelemetryPanel::exportLiveBuffer(const QString& filePath, const QString& presetName) {
    const std::string path = filePath.toStdString();
    const auto records = recorder_.liveBufferSnapshots();
    if (filePath.endsWith(".json", Qt::CaseInsensitive)) {
        return TelemetryRecorder::exportRecordsToJson(path, records, presetName.toStdString());
    } else {
        return TelemetryRecorder::exportRecordsToCsv(path, records);
    }
}

} // namespace erebus::render

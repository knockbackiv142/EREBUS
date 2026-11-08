#include "MainWindow.h"
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <exception>
#include <numbers>
#include <utility>

namespace erebus {

MainWindow::MainWindow() {
    setWindowTitle("Project Erebus - Primordial Black Hole Observatory");
    resize(1280, 800);
    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    view_ = new render::UniverseView(central);
    view_->setSimulation(&simulation_);
    connect(view_, &render::UniverseView::bodySelected, this,
            &MainWindow::updateSelectedBody);
    layout->addWidget(view_, 1);

    auto* controls = new QVBoxLayout;
    auto* group = new QGroupBox("Simulation controls", central);
    auto* form = new QFormLayout(group);
    auto* run = new QPushButton("Pause", group);
    connect(run, &QPushButton::clicked, this, [this, run] {
        toggleSimulation();
        run->setText(run->text() == "Pause" ? "Run" : "Pause");
    });
    form->addRow(run);
    massControl_ = new QDoubleSpinBox(group);
    massControl_->setRange(1.0e20, 1.0e30);
    massControl_->setDecimals(3);
    massControl_->setValue(1.0e26);
    massControl_->setSuffix(" kg");
    connect(massControl_, &QDoubleSpinBox::valueChanged, this, &MainWindow::applyPbhMass);
    form->addRow("PBH mass", massControl_);
    timestepControl_ = new QDoubleSpinBox(group);
    timestepControl_->setRange(60.0, 30.0 * 86400.0);
    timestepControl_->setValue(21600.0);
    timestepControl_->setSuffix(" s/step");
    connect(timestepControl_, &QDoubleSpinBox::valueChanged, view_,
            &render::UniverseView::setTimeStep);
    form->addRow("Timestep", timestepControl_);
    auto* speedRow = new QHBoxLayout;
    for (const auto& setting : {std::pair{"0.25x", 0.25},
                                std::pair{"1x", 1.0},
                                std::pair{"4x", 4.0},
                                std::pair{"16x", 16.0}}) {
        auto* speed = new QPushButton(QString::fromUtf8(setting.first), group);
        connect(speed, &QPushButton::clicked, this, [this, multiplier = setting.second] {
            view_->setSpeedMultiplier(multiplier);
            statusBar()->showMessage(QString("Simulation speed: %1x").arg(multiplier));
        });
        speedRow->addWidget(speed);
    }
    form->addRow("Speed", speedRow);
    auto* pbhPreset = new QComboBox(group);
    pbhPreset->addItem("Scholtz & Unwin 2020: 5 M Earth, 450 AU");
    pbhPreset->addItem("Siraj, Chyba & Tremaine 2025: 4.4 M Earth, 290 AU");
    connect(pbhPreset, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::applyPbhPreset);
    form->addRow("PBH preset", pbhPreset);
    auto* trajectories = new QCheckBox("Show calculated trajectories", group);
    trajectories->setChecked(true);
    connect(trajectories, &QCheckBox::toggled, view_,
            &render::UniverseView::setTrajectoriesVisible);
    form->addRow(trajectories);
    auto* grid = new QCheckBox("Show orbital plane and axes", group);
    grid->setChecked(true);
    connect(grid, &QCheckBox::toggled, view_,
            &render::UniverseView::setReferenceGridVisible);
    form->addRow(grid);
    auto* field = new QCheckBox("Show selected-body gravity heatmap", group);
    field->setChecked(false);
    connect(field, &QCheckBox::toggled, this, [this, field](bool visible) {
        if (visible && view_->selectedBody() < 0) {
            statusBar()->showMessage("Select or focus a celestial body first.");
            field->setChecked(false);
            return;
        }
        view_->setFieldVisible(visible);
    });
    form->addRow(field);
    auto* overview = new QPushButton("Return to Solar System view", group);
    connect(overview, &QPushButton::clicked, view_, &render::UniverseView::returnToSystemView);
    form->addRow(overview);
    controls->addWidget(group);

    auto* bodyGroup = new QGroupBox("Focus body", central);
    auto* bodyLayout = new QVBoxLayout(bodyGroup);
    auto* bodyScroll = new QScrollArea(central);
    bodyScroll->setWidget(bodyGroup);
    bodyScroll->setWidgetResizable(true);
    bodyScroll->setMinimumHeight(190);
    for (std::size_t index = 0; index < simulation_.bodies().size(); ++index) {
        auto* bodyButton = new QPushButton(
            QString::fromStdString(simulation_.bodies()[index].name), bodyGroup);
        connect(bodyButton, &QPushButton::clicked, this, [this, index] {
            view_->focusBody(static_cast<int>(index));
        });
        bodyLayout->addWidget(bodyButton);
    }
    controls->addWidget(bodyScroll);

    telemetry_ = new QLabel(central);
    telemetry_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    telemetry_->setMinimumWidth(280);
    controls->addWidget(telemetry_);
    selectionTelemetry_ = new QLabel("Selected body: none", central);
    selectionTelemetry_->setWordWrap(true);
    controls->addWidget(selectionTelemetry_);
    controls->addStretch();
    layout->addLayout(controls);
    setCentralWidget(central);

    auto* refresh = new QTimer(this);
    connect(refresh, &QTimer::timeout, this, &MainWindow::updateTelemetry);
    refresh->start(250);
    updateTelemetry();
    statusBar()->showMessage("Drag to rotate. Scroll to zoom. SI units; distances shown in AU.");
}

void MainWindow::toggleSimulation() {
    static bool running = true;
    running = !running;
    view_->setRunning(running);
}

void MainWindow::applyPbhMass(double value) {
    try { simulation_.setPbhMassKg(value); }
    catch (const std::exception& error) { statusBar()->showMessage(error.what()); }
}

void MainWindow::applyPbhPreset(int index) {
    simulation_.applyPbhPreset(index == 1);
    massControl_->setValue(simulation_.bodies().back().massKg);
}

void MainWindow::updateSelectedBody(int index) {
    if (index < 0 || index >= static_cast<int>(simulation_.bodies().size())) return;
    const auto& body = simulation_.bodies()[static_cast<std::size_t>(index)];
    if (index == 0) {
        selectionTelemetry_->setText(QString("<b>Selected:</b> Sun<br>Mass: %1 kg<br>"
            "Central reference body; heliocentric orbital elements are not defined.")
            .arg(body.massKg, 0, 'e', 6));
        return;
    }
    const auto elements = simulation_.orbitalElements(static_cast<std::size_t>(index));
    selectionTelemetry_->setText(QString("<b>Selected:</b> %1<br>a: %2 AU<br>e: %3<br>i: %4 deg<br>"
        "Omega: %5 deg<br>omega: %6 deg")
        .arg(QString::fromStdString(body.name))
        .arg(elements.semiMajorAxisM / 1.495978707e11, 0, 'f', 3)
        .arg(elements.eccentricity, 0, 'f', 5)
        .arg(elements.inclinationRad * 180.0 / std::numbers::pi, 0, 'f', 3)
        .arg(elements.longitudeOfAscendingNodeRad * 180.0 / std::numbers::pi, 0, 'f', 3)
        .arg(elements.argumentOfPeriapsisRad * 180.0 / std::numbers::pi, 0, 'f', 3));
}

void MainWindow::updateTelemetry() {
    const auto t = simulation_.telemetry();
    const auto elements = simulation_.orbitalElements(3);
    telemetry_->setText(QString(
        "<b>Live telemetry</b><br>Simulation time: %1 days<br>"
        "Total energy: %2 J<br>Momentum: %3 kg m/s<br><br>"
        "<b>Earth heliocentric orbit</b><br>a: %4 AU<br>e: %5<br>i: %6 deg")
        .arg(t.timeS / 86400.0, 0, 'f', 2)
        .arg(t.totalEnergyJ, 0, 'e', 6)
        .arg(t.momentumKgMPerS, 0, 'e', 6)
        .arg(elements.semiMajorAxisM / 1.495978707e11, 0, 'f', 6)
        .arg(elements.eccentricity, 0, 'f', 8)
        .arg(elements.inclinationRad * 180.0 / std::numbers::pi, 0, 'f', 4));
}

} // namespace erebus

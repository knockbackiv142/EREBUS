#include "MainWindow.h"
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QSlider>
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
    setWindowTitle("Project EREBUS — Primordial Black Hole Observatory");
    resize(1180, 720);
    setStyleSheet("QMainWindow, QWidget { background: #060a14; color: #c8d8f0; }"
                  "QGroupBox { border: 1px solid #1e3050; border-radius:4px; margin-top:8px; }"
                  "QGroupBox::title { color:#88aadd; }"
                  "QPushButton { background:#0d1a2e; color:#a8c0e0; border:1px solid #1e3050;"
                  "  border-radius:3px; padding:3px 8px; }"
                  "QPushButton:hover { background:#1a2a44; }"
                  "QComboBox { background:#0d1a2e; color:#a8c0e0; border:1px solid #1e3050; }"
                  "QDoubleSpinBox { background:#0d1a2e; color:#a8c0e0; border:1px solid #1e3050; }"
                  "QSlider::groove:horizontal { background:#0d1a2e; height:4px; }"
                  "QSlider::handle:horizontal { background:#3060a0; width:12px; height:12px;"
                  "  border-radius:6px; margin:-4px 0; }"
                  "QLabel { color:#b0c8e8; }");

    auto* central = new QWidget(this);
    auto* layout  = new QHBoxLayout(central);
    layout->setSpacing(6);
    layout->setContentsMargins(4, 4, 4, 4);

    view_ = new render::UniverseView(central);
    view_->setSimulation(&simulation_);
    connect(view_, &render::UniverseView::bodySelected, this, &MainWindow::updateSelectedBody);
    layout->addWidget(view_, 1);

    // ── Control panel ─────────────────────────────────────────────────────────
    auto* controls = new QVBoxLayout;
    controls->setSpacing(4);

    auto* group = new QGroupBox("Simulation Controls", central);
    auto* form  = new QFormLayout(group);
    form->setSpacing(4);

    // Run/Pause
    auto* run = new QPushButton("⏸ Pause", group);
    connect(run, &QPushButton::clicked, this, [this, run] {
        toggleSimulation();
        run->setText(run->text() == "⏸ Pause" ? "▶ Run" : "⏸ Pause");
    });
    form->addRow(run);

    // PBH mass
    massControl_ = new QDoubleSpinBox(group);
    massControl_->setRange(1.0e20, 1.0e30);
    massControl_->setDecimals(3);
    massControl_->setValue(simulation_.bodies().back().massKg);
    massControl_->setSuffix(" kg");
    connect(massControl_, &QDoubleSpinBox::valueChanged, this, &MainWindow::applyPbhMass);
    form->addRow("PBH mass", massControl_);

    // Timestep
    timestepControl_ = new QDoubleSpinBox(group);
    timestepControl_->setRange(60.0, 30.0 * 86400.0);
    timestepControl_->setValue(21600.0);
    timestepControl_->setSuffix(" s/step");
    connect(timestepControl_, &QDoubleSpinBox::valueChanged,
            view_, &render::UniverseView::setTimeStep);
    form->addRow("Timestep", timestepControl_);

    // Speed buttons
    auto* speedRow = new QHBoxLayout;
    for (const auto& setting : {std::pair{"0.25×", 0.25},
                                 std::pair{"1×",    1.0},
                                 std::pair{"4×",    4.0},
                                 std::pair{"16×",  16.0}}) {
        auto* speed = new QPushButton(QString::fromUtf8(setting.first), group);
        connect(speed, &QPushButton::clicked, this, [this, m = setting.second] {
            view_->setSpeedMultiplier(m);
            statusBar()->showMessage(QString("Simulation speed: %1×").arg(m));
        });
        speedRow->addWidget(speed);
    }
    form->addRow("Speed", speedRow);

    // PBH preset (now 3 options)
    auto* pbhPreset = new QComboBox(group);
    pbhPreset->addItem("Scholtz & Unwin 2020 — 5 M⊕, 450 AU");
    pbhPreset->addItem("Siraj, Chyba & Tremaine 2025 — 4.4 M⊕, 290 AU");
    pbhPreset->addItem("Batygin & Brown 2021 — 6.2 M⊕, 380 AU");
    connect(pbhPreset, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::applyPbhPreset);
    form->addRow("PBH preset", pbhPreset);

    // Visual toggles
    auto* traj = new QCheckBox("Show orbital trajectories", group);
    traj->setChecked(true);
    connect(traj, &QCheckBox::toggled, view_, &render::UniverseView::setTrajectoriesVisible);
    form->addRow(traj);

    auto* grid = new QCheckBox("Show orbital plane / axes", group);
    grid->setChecked(true);
    connect(grid, &QCheckBox::toggled, view_, &render::UniverseView::setReferenceGridVisible);
    form->addRow(grid);

    auto* stars = new QCheckBox("Show star field", group);
    stars->setChecked(false);
    connect(stars, &QCheckBox::toggled, view_, &render::UniverseView::setStarsVisible);
    form->addRow(stars);

    auto* field = new QCheckBox("Show gravitational potential field", group);
    field->setChecked(false);
    connect(field, &QCheckBox::toggled, view_, &render::UniverseView::setFieldVisible);
    form->addRow(field);

    // Potential source
    auto* contributor = new QComboBox(group);
    contributor->addItem("Combined potential", -1);
    for (std::size_t i = 0; i < simulation_.bodies().size(); ++i)
        contributor->addItem(QString::fromStdString(simulation_.bodies()[i].name),
                             static_cast<int>(i));
    connect(contributor, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, contributor](int) {
                view_->setPotentialSource(contributor->currentData().toInt());
            });
    form->addRow("Contributors", contributor);

    auto* radius = new QDoubleSpinBox(group);
    radius->setRange(0.25, 1000.0);
    radius->setValue(8.0);
    radius->setSuffix(" AU");
    connect(radius, qOverload<double>(&QDoubleSpinBox::valueChanged),
            view_, &render::UniverseView::setPotentialRadius);
    form->addRow("Field radius", radius);

    auto* opacity = new QSlider(Qt::Horizontal, group);
    opacity->setRange(5, 95);
    opacity->setValue(55);
    connect(opacity, &QSlider::valueChanged, view_, &render::UniverseView::setPotentialOpacity);
    form->addRow("Field opacity", opacity);

    auto* overview = new QPushButton("↩ Return to Solar System view", group);
    connect(overview, &QPushButton::clicked, view_, &render::UniverseView::returnToSystemView);
    form->addRow(overview);

    controls->addWidget(group);

    // ── Telemetry Recording & Export ──────────────────────────────────────────
    auto* recGroup = new QGroupBox("Telemetry Export", central);
    auto* recLayout = new QVBoxLayout(recGroup);
    recLayout->setSpacing(4);

    recordBtn_ = new QPushButton("🔴 Start Recording", recGroup);
    recordBtn_->setStyleSheet("QPushButton { font-weight: bold; background: #261018; border: 1px solid #702030; color: #ff8090; padding: 4px; }"
                              "QPushButton:hover { background: #3d1422; }");
    connect(recordBtn_, &QPushButton::clicked, this, &MainWindow::toggleTelemetryRecording);
    recLayout->addWidget(recordBtn_);

    exportLiveBtn_ = new QPushButton("💾 Export Live Buffer (CSV/JSON)", recGroup);
    connect(exportLiveBtn_, &QPushButton::clicked, this, &MainWindow::exportLiveTelemetry);
    recLayout->addWidget(exportLiveBtn_);

    recordStatus_ = new QLabel("Recorder: Idle (0 samples)", recGroup);
    recordStatus_->setStyleSheet("color: #7090b0; font-size: 8pt;");
    recLayout->addWidget(recordStatus_);

    controls->addWidget(recGroup);

    // ── Focus body panel ──────────────────────────────────────────────────────
    auto* bodyGroup = new QGroupBox("Focus / Select Body", central);
    auto* bodyLayout = new QVBoxLayout(bodyGroup);
    bodyLayout->setSpacing(2);
    auto* bodyScroll = new QScrollArea(central);
    bodyScroll->setWidget(bodyGroup);
    bodyScroll->setWidgetResizable(true);
    bodyScroll->setMinimumHeight(200);
    bodyScroll->setMaximumHeight(300);
    bodyScroll->setStyleSheet("QScrollArea { border: 1px solid #1e3050; }");

    for (std::size_t i = 0; i < simulation_.bodies().size(); ++i) {
        const auto& b = simulation_.bodies()[i];
        auto* btn = new QPushButton(QString::fromStdString(b.name), bodyGroup);
        if (b.hypothetical)
            btn->setStyleSheet("QPushButton { color:#c864f0; font-weight:bold; }");
        connect(btn, &QPushButton::clicked, this, [this, i] {
            view_->focusBody(static_cast<int>(i));
        });
        bodyLayout->addWidget(btn);
    }
    controls->addWidget(bodyScroll);

    // ── Live telemetry label ──────────────────────────────────────────────────
    telemetry_ = new QLabel(central);
    telemetry_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    telemetry_->setTextFormat(Qt::RichText);
    telemetry_->setMinimumWidth(280);
    controls->addWidget(telemetry_);

    selectionTelemetry_ = new QLabel("Selected body: none", central);
    selectionTelemetry_->setWordWrap(true);
    selectionTelemetry_->setTextFormat(Qt::RichText);
    controls->addWidget(selectionTelemetry_);

    controls->addStretch();
    layout->addLayout(controls);
    setCentralWidget(central);

    // ── Docked panels ─────────────────────────────────────────────────────────
    telPanel_ = new render::TelemetryPanel(this);
    telPanel_->setSimulation(&simulation_);
    addDockWidget(Qt::BottomDockWidgetArea, telPanel_);
    telPanel_->setMinimumHeight(180);

    notesPanel_ = new render::NotesPanel(this);
    notesPanel_->setSimulation(&simulation_);
    addDockWidget(Qt::RightDockWidgetArea, notesPanel_);
    notesPanel_->setMinimumWidth(260);
    notesPanel_->setMaximumWidth(320);

    // ── Refresh timer (250 ms) ────────────────────────────────────────────────
    auto* refresh = new QTimer(this);
    connect(refresh, &QTimer::timeout, this, &MainWindow::updateTelemetry);
    refresh->start(250);
    updateTelemetry();

    statusBar()->showMessage(
        "Drag to rotate · Scroll to zoom · Double-click body to focus · "
        "Orbits colored by eccentricity (blue=circular, red=eccentric)");
}

// ─── Slots ────────────────────────────────────────────────────────────────────

void MainWindow::toggleSimulation() {
    static bool running = true;
    running = !running;
    view_->setRunning(running);
}

void MainWindow::applyPbhMass(double value) {
    try { simulation_.setPbhMassKg(value); }
    catch (const std::exception& e) { statusBar()->showMessage(e.what()); }
}

void MainWindow::applyPbhPreset(int index) {
    simulation_.applyPbhPreset(index);
    massControl_->setValue(simulation_.bodies().back().massKg);
    if (notesPanel_) notesPanel_->updateForPreset(index);
}

void MainWindow::updateSelectedBody(int index) {
    if (index < 0 || index >= static_cast<int>(simulation_.bodies().size())) return;
    const auto& body = simulation_.bodies()[static_cast<std::size_t>(index)];
    if (index == 0) {
        selectionTelemetry_->setText(
            QString("<b>Selected:</b> Sun<br>Mass: %1 kg<br>"
                    "Central reference body — heliocentric orbital elements undefined.")
            .arg(body.massKg, 0, 'e', 6));
        return;
    }
    try {
        const auto el = simulation_.orbitalElements(static_cast<std::size_t>(index));
        const double periAU  = el.semiMajorAxisM * (1.0 - el.eccentricity) / 1.495978707e11;
        const double apoAU   = el.semiMajorAxisM * (1.0 + el.eccentricity) / 1.495978707e11;
        const double periodYr = std::pow(el.semiMajorAxisM / 1.495978707e11, 1.5);
        QString extra;
        if (body.hypothetical) {
            const double rs = physics::Simulation::schwarzschildRadiusM(body.massKg) * 1000.0;
            extra = QString("<br><span style='color:#c864f0;'>Schwarzschild r<sub>s</sub>: %1 mm</span>")
                    .arg(rs, 0, 'f', 2);
        }
        selectionTelemetry_->setText(
            QString("<b>%1</b><br>"
                    "a: %2 AU&emsp;e: %3<br>"
                    "i: %4°&emsp;Ω: %5°&emsp;ω: %6°<br>"
                    "q: %7 AU&emsp;Q: %8 AU<br>"
                    "Period: %9 yr%10")
            .arg(QString::fromStdString(body.name))
            .arg(el.semiMajorAxisM / 1.495978707e11, 0, 'f', 3)
            .arg(el.eccentricity, 0, 'f', 5)
            .arg(el.inclinationRad * 180.0 / std::numbers::pi, 0, 'f', 3)
            .arg(el.longitudeOfAscendingNodeRad * 180.0 / std::numbers::pi, 0, 'f', 3)
            .arg(el.argumentOfPeriapsisRad * 180.0 / std::numbers::pi, 0, 'f', 3)
            .arg(periAU, 0, 'f', 3)
            .arg(apoAU, 0, 'f', 3)
            .arg(periodYr, 0, 'f', 1)
            .arg(extra));
    } catch (const std::exception& e) {
        selectionTelemetry_->setText(QString("<b>%1</b><br><i>%2</i>")
            .arg(QString::fromStdString(body.name))
            .arg(e.what()));
    }
}

void MainWindow::updateTelemetry() {
    // Record + refresh telemetry panel
    if (telPanel_) {
        telPanel_->recordTelemetry();
        telPanel_->refresh();
    }

    if (telPanel_ && telPanel_->isRecording() && recordStatus_) {
        const auto count = telPanel_->sessionRecordCount();
        recordStatus_->setText(QString("🔴 Recording: %1 samples (%2s)")
            .arg(count).arg(count * 0.25, 0, 'f', 1));
    }

    // Top-level live label
    const auto t = simulation_.telemetry();
    try {
        const auto el = simulation_.orbitalElements(3);
        telemetry_->setText(QString(
            "<b>Live Telemetry</b><br>"
            "Sim time: <b>%1</b> yr (%2 days)<br>"
            "Total E: %3 J<br>"
            "Linear p: %4 kg·m/s<br><br>"
            "<b>Earth orbit</b><br>"
            "a = %5 AU, e = %6<br>"
            "i = %7°")
            .arg(t.timeS / (365.25 * 86400.0), 0, 'f', 3)
            .arg(t.timeS / 86400.0, 0, 'f', 1)
            .arg(t.totalEnergyJ, 0, 'e', 5)
            .arg(t.momentumKgMPerS, 0, 'e', 5)
            .arg(el.semiMajorAxisM / 1.495978707e11, 0, 'f', 6)
            .arg(el.eccentricity, 0, 'f', 8)
            .arg(el.inclinationRad * 180.0 / std::numbers::pi, 0, 'f', 4));
    } catch (...) {
        telemetry_->setText(QString("<b>Sim time:</b> %1 yr")
            .arg(t.timeS / (365.25 * 86400.0), 0, 'f', 3));
    }
}

void MainWindow::toggleTelemetryRecording() {
    if (!telPanel_) return;

    if (!telPanel_->isRecording()) {
        telPanel_->startRecording();
        recordBtn_->setText("⏹ Stop & Export");
        recordBtn_->setStyleSheet("QPushButton { font-weight: bold; background: #601824; border: 1px solid #d03045; color: #ffffff; padding: 4px; }"
                                  "QPushButton:hover { background: #782030; }");
        recordStatus_->setText("🔴 Recording active...");
        statusBar()->showMessage("Telemetry recording started. Click 'Stop & Export' to save.", 4000);
    } else {
        telPanel_->stopRecording();
        const std::size_t count = telPanel_->sessionRecordCount();
        recordBtn_->setText("🔴 Start Recording");
        recordBtn_->setStyleSheet("QPushButton { font-weight: bold; background: #261018; border: 1px solid #702030; color: #ff8090; padding: 4px; }"
                                  "QPushButton:hover { background: #3d1422; }");

        if (count == 0) {
            recordStatus_->setText("Recorder: Idle (0 samples)");
            QMessageBox::information(this, "Telemetry Export", "No telemetry data was recorded.");
            return;
        }

        recordStatus_->setText(QString("Stopped (%1 samples recorded)").arg(count));

        const QString defaultName = QString("erebus_telemetry_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

        const QString filePath = QFileDialog::getSaveFileName(
            this,
            "Export Recorded Telemetry",
            defaultName,
            "CSV Files (*.csv);;JSON Files (*.json);;All Files (*.*)");

        if (filePath.isEmpty()) {
            recordStatus_->setText(QString("Ready to export (%1 samples stored)").arg(count));
            statusBar()->showMessage("Export cancelled. Data retained until next recording session.", 4000);
            return;
        }

        const bool ok = telPanel_->exportSession(filePath,
            QString::fromStdString(simulation_.bodies().back().name));
        if (ok) {
            recordStatus_->setText(QString("Exported %1 records successfully").arg(count));
            statusBar()->showMessage(QString("Exported %1 records to %2").arg(count).arg(filePath), 6000);
            QMessageBox::information(this, "Export Complete",
                QString("Successfully exported %1 telemetry records to:\n%2").arg(count).arg(filePath));
        } else {
            recordStatus_->setText("Export failed");
            QMessageBox::critical(this, "Export Error", "Failed to write telemetry file to disk.");
        }
    }
}

void MainWindow::exportLiveTelemetry() {
    if (!telPanel_) return;

    const auto count = telPanel_->recorder().liveBufferSnapshots().size();
    if (count == 0) {
        QMessageBox::information(this, "Live Telemetry Export", "Live telemetry buffer is currently empty.");
        return;
    }

    const QString defaultName = QString("erebus_live_buffer_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        "Export Live Telemetry Buffer",
        defaultName,
        "CSV Files (*.csv);;JSON Files (*.json);;All Files (*.*)");

    if (filePath.isEmpty()) return;

    const bool ok = telPanel_->exportLiveBuffer(filePath,
        QString::fromStdString(simulation_.bodies().back().name));
    if (ok) {
        statusBar()->showMessage(QString("Live telemetry exported (%1 records) to %2").arg(count).arg(filePath), 6000);
        QMessageBox::information(this, "Export Complete",
            QString("Successfully exported %1 live telemetry records to:\n%2").arg(count).arg(filePath));
    } else {
        QMessageBox::critical(this, "Export Error", "Failed to write live telemetry file to disk.");
    }
}

} // namespace erebus

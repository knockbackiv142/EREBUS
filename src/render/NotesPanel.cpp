#include "NotesPanel.h"
#include <QVBoxLayout>
#include <cmath>

namespace erebus::render {

// ─── Preset descriptions ──────────────────────────────────────────────────────

static const char* PRESET_NOTES[3] = {

// 0 — Scholtz & Unwin 2020
R"(
<h3 style="color:#c864f0;">PBH Preset 0 — Scholtz &amp; Unwin 2020</h3>
<p style="color:#90b0d8;">
<b>Reference:</b> J. Scholtz &amp; J. Unwin, <i>Phys. Rev. Lett.</i> <b>125</b>, 051103 (2020).<br>
DOI: <tt>10.1103/PhysRevLett.125.051103</tt>
</p>
<p style="color:#c0d4f0;">
<b>Hypothesis:</b> Planet Nine is a primordial black hole (PBH) of mass
<b>5–10 M<sub>⊕</sub></b> (this preset uses 5 M<sub>⊕</sub>), captured by the early Sun from
the interstellar PBH population. The capture probability is comparable to
rogue-planet-capture models (~10<sup>−6</sup> per perihelion passage).
</p>
<p style="color:#c0d4f0;">
<b>Orbital parameters:</b> a ≈ 450 AU, e ≈ 0.20, i ≈ 20°.<br>
Perihelion ≈ 360 AU. This places the PBH well beyond the Kuiper Belt, in the
region where it can produce the observed clustering of ETNOs
(Extreme Trans-Neptunian Objects) through secular perturbations.
</p>
<p style="color:#c0d4f0;">
<b>Detection channel:</b> Scholtz &amp; Unwin propose searching for a gamma-ray /
X-ray point source moving across the sky. A PBH at 450 AU accumulates a dark-matter
micro-halo; WIMP annihilation within that halo produces a ~GeV luminosity of
<i>L</i> ≈ 10<sup>24</sup>–10<sup>26</sup> erg/s, detectable in principle by Fermi-LAT
or future MeV telescopes. At 450 AU the angular motion is ~0.004 arcsec/yr.
</p>
<p style="color:#c0d4f0;">
<b>OGLE link:</b> OGLE microlensing data show an excess of ~6 short-timescale events
consistent with free-floating objects of 0.5–20 M<sub>⊕</sub>. If a fraction of these
are PBHs rather than ejected planets, the number density is consistent with Solar
capture over 4.5 Gyr.
</p>
<p style="color:#90b0d8;font-style:italic;">
The Schwarzschild radius for 5 M<sub>⊕</sub> ≈ 8.9 mm — the size of a marble.
No reflected light is possible; only gravitational or accretion signatures.
</p>
)",

// 1 — Siraj, Chyba & Tremaine 2025
R"(
<h3 style="color:#c864f0;">PBH Preset 1 — Siraj, Chyba &amp; Tremaine 2025</h3>
<p style="color:#90b0d8;">
<b>Reference:</b> A. Siraj, C. F. Chyba &amp; S. Tremaine, <i>ApJL</i> <b>982</b>, L37 (2025).<br>
DOI: <tt>10.3847/2041-8213/aba119</tt>
</p>
<p style="color:#c0d4f0;">
<b>Method:</b> N-body simulations of 100 000 TNO test particles under the giant planets
plus a Planet X, with observational-bias correction from Pan-STARRS and DES surveys.
Statistical clustering analysed in longitude of perihelion ω̄ at &gt;3σ significance.
</p>
<p style="color:#c0d4f0;">
<b>Best-fit parameters:</b><br>
Mass: <b>4.4 ± 1.1 M<sub>⊕</sub></b><br>
Semi-major axis: <b>a = 290 ± 30 AU</b><br>
Eccentricity: <b>e = 0.29 ± 0.13</b><br>
Inclination: <b>i = 6.8° ± 5.0°</b><br>
Perihelion: q ≈ 206 AU, Aphelion: Q ≈ 374 AU
</p>
<p style="color:#c0d4f0;">
<b>Significance:</b> This is a narrower, closer, slightly less massive prediction than
Batygin &amp; Brown 2016, partly driven by their more careful treatment of survey
detection efficiency. At a = 290 AU, the period is ~4 950 yr.
</p>
<p style="color:#90b0d8;font-style:italic;">
If this object is a PBH: Schwarzschild radius for 4.4 M<sub>⊕</sub> ≈ 7.8 mm.
At 290 AU the apparent magnitude would be ~25–26 from accretion (Bondi)
at the Sun's interstellar medium density — below current survey limits.
</p>
)",

// 2 — Batygin & Brown 2021
R"(
<h3 style="color:#c864f0;">PBH Preset 2 — Batygin &amp; Brown 2021</h3>
<p style="color:#90b0d8;">
<b>Reference:</b> K. Batygin &amp; M. E. Brown, <i>AJ</i> <b>162</b>, 219 (2021).<br>
arXiv: <tt>2108.09868</tt>
</p>
<p style="color:#c0d4f0;">
<b>Original hypothesis (2016):</b> Batygin &amp; Brown, <i>AJ</i> <b>151</b>, 22 (2016)
first proposed Planet Nine based on orbital clustering of 6 ETNOs. That paper
estimated <i>m</i> ~ 10 M<sub>⊕</sub>, <i>a</i> ~ 700 AU, <i>e</i> ~ 0.6.
</p>
<p style="color:#c0d4f0;">
<b>2021 refinement (MCMC + bias correction):</b><br>
Mass: <b>6.2<sup>+2.2</sup><sub>−1.3</sub> M<sub>⊕</sub></b><br>
Semi-major axis: <b>a = 380<sup>+140</sup><sub>−80</sub> AU</b><br>
Eccentricity: <b>e ≈ 0.21</b> (perihelion q ~ 300 AU)<br>
Inclination: <b>i = 16° ± 5°</b>
</p>
<p style="color:#c0d4f0;">
Planet Nine is now expected to be <i>closer</i> (~380 vs 700 AU), <i>less massive</i>
(~6 vs ~10 M<sub>⊕</sub>), and slightly less inclined than the 2016 estimate.
Sky coverage from Pan-STARRS and DES has excluded portions of the original
parameter space; unexcluded sky contains ~40% of the probability density.
</p>
<p style="color:#c0d4f0;">
<b>TNO shepherding mechanism:</b> Secular resonance sweeping drives anti-alignment of
perihelion directions relative to Planet Nine's perihelion. This reproduces the
clustering seen in Sedna, VP113, Goblin, and several other ETNOs with q &gt; 150 AU.
</p>
<p style="color:#90b0d8;font-style:italic;">
If this is a PBH: 6.2 M<sub>⊕</sub> gives a Schwarzschild radius of ~11 mm.
Accretion luminosity at 380 AU from Solar wind: ~10<sup>22</sup> erg/s — far below
detection; the dark-matter halo channel (Scholtz &amp; Unwin) is more promising.
</p>
)"
};

// ─── Constructor ──────────────────────────────────────────────────────────────

NotesPanel::NotesPanel(QWidget* parent)
    : QDockWidget("Scientific Notes", parent)
{
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                QDockWidget::DockWidgetClosable);

    text_ = new QTextEdit;
    text_->setReadOnly(true);
    text_->setStyleSheet("QTextEdit {"
                         "  background: #060a14;"
                         "  color: #c0d4f0;"
                         "  font-family: 'Segoe UI', Verdana, sans-serif;"
                         "  font-size: 9pt;"
                         "  border: none;"
                         "}");
    setWidget(text_);
    rebuild();
}

void NotesPanel::setSimulation(const physics::Simulation* sim) {
    simulation_ = sim;
    rebuild();
}

void NotesPanel::updateForPreset(int preset) {
    activePreset_ = std::clamp(preset, 0, 2);
    rebuild();
}

// ─── rebuild ─────────────────────────────────────────────────────────────────

void NotesPanel::rebuild() {
    QString html;
    html += "<div style='padding:6px;'>";

    // Schwarzschild radius for PBH
    if (simulation_) {
        const auto& bodies = simulation_->bodies();
        for (const auto& b : bodies) {
            if (!b.hypothetical) continue;
            const double rs = physics::Simulation::schwarzschildRadiusM(b.massKg);
            const double rsMm = rs * 1000.0;
            const double earthMass = 5.9722e24;
            html += QString(
                "<p style='color:#80d090;background:#0a1420;padding:4px;border-radius:3px;'>"
                "<b>Active PBH:</b> %1<br>"
                "Mass: <b>%2 M<sub>⊕</sub></b> = %3 kg<br>"
                "Schwarzschild radius: <b>r<sub>s</sub> = %4 mm</b><br>"
                "Current distance from Sun: <b>%5 AU</b>"
                "</p>")
                .arg(QString::fromStdString(b.name))
                .arg(b.massKg / earthMass, 0, 'f', 2)
                .arg(b.massKg, 0, 'e', 3)
                .arg(rsMm, 0, 'f', 2)
                .arg(b.positionM.norm() / 1.495978707e11, 0, 'f', 2);

            // Hill sphere
            try {
                const double rH = simulation_->hillSphereRadiusM(
                    static_cast<std::size_t>(&b - &bodies[0]), 0);
                html += QString(
                    "<p style='color:#90b0d8;'>"
                    "Hill sphere radius: <b>%1 AU</b> (%2 m)</p>")
                    .arg(rH / 1.495978707e11, 0, 'f', 2)
                    .arg(rH, 0, 'e', 3);
            } catch (...) {}
            break;
        }
    }

    // Preset-specific notes
    html += PRESET_NOTES[activePreset_];

    // General context (always shown)
    html += R"(
<hr style="border-color:#1e3050;">
<h3 style="color:#88aadd;">EREBUS Simulation Context</h3>
<p style="color:#a0b8d8;">
<b>Integrator:</b> Velocity-Störmer-Verlet (leapfrog, 2nd order symplectic).
Energy drift is proportional to <i>dt²</i>; use small timesteps for quantitative runs.
</p>
<p style="color:#a0b8d8;">
<b>Post-Newtonian correction:</b> A first-order Schwarzschild (1PN/EIH) term is applied
to Mercury's equation of motion, contributing ~43 arcsec/century of additional
perihelion precession — matching GR prediction.
</p>
<p style="color:#a0b8d8;">
<b>Initial conditions:</b> J2000 mean orbital elements from MPC / JPL Horizons for all
planets. TNO elements (Sedna, VP113, Goblin, Drac) from published discovery papers.
PBH placed at apohelion for each preset per the respective paper's best-fit orbit.
</p>
<p style="color:#a0b8d8;">
<b>Coordinate system:</b> Ecliptic J2000.0 (ICRS-compatible). X-axis toward vernal
equinox, Z-axis toward north ecliptic pole. All bodies included as point masses.
</p>
<p style="color:#607898;">
<i>Batygin &amp; Brown (2016, 2021) | Scholtz &amp; Unwin (2020) |
Siraj, Chyba &amp; Tremaine (2025)</i>
</p>
</div>
)";

    text_->setHtml(html);
}

} // namespace erebus::render

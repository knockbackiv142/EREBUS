#include "Simulation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace erebus::physics {
namespace {
constexpr double AU            = 1.495978707e11;
constexpr double SunMass       = 1.98847e30;
constexpr double EarthMass     = 5.9722e24;
constexpr double ReferenceEpochJd = 2460840.5;

// Solve Kepler's equation M = E - e*sin(E) via Newton-Raphson (12 iters, good to 1e-15)
double solveKepler(double M, double e) {
    double E = M;
    for (int i = 0; i < 12; ++i)
        E -= (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
    return E;
}

// Perifocal position (m) from orbital elements + body mass (uses two-body mu)
Vector3 stateFromElements(const OrbitalElements& el, double massKg) {
    const double mu = Simulation::GravitationalConstant * (SunMass + massKg);
    const double E  = solveKepler(el.meanAnomalyRad, el.eccentricity);
    const double cosE = std::cos(E), sinE = std::sin(E);
    const double r = el.semiMajorAxisM * (1.0 - el.eccentricity * cosE);
    const Vector3 peri{el.semiMajorAxisM * (cosE - el.eccentricity),
                       el.semiMajorAxisM * std::sqrt(1.0 - el.eccentricity * el.eccentricity) * sinE,
                       0.0};
    (void)r; (void)mu;
    const Eigen::AngleAxisd Omega(el.longitudeOfAscendingNodeRad, Vector3::UnitZ());
    const Eigen::AngleAxisd inc  (el.inclinationRad,              Vector3::UnitX());
    const Eigen::AngleAxisd omega(el.argumentOfPeriapsisRad,      Vector3::UnitZ());
    return Omega * inc * omega * peri;
}

// Perifocal velocity (m/s)
Vector3 velocityFromElements(const OrbitalElements& el, double massKg) {
    const double mu = Simulation::GravitationalConstant * (SunMass + massKg);
    const double E  = solveKepler(el.meanAnomalyRad, el.eccentricity);
    const double cosE = std::cos(E), sinE = std::sin(E);
    const double r = el.semiMajorAxisM * (1.0 - el.eccentricity * cosE);
    const double sqrtMuA = std::sqrt(mu * el.semiMajorAxisM);
    const Vector3 periV{-sqrtMuA * sinE / r,
                         sqrtMuA * std::sqrt(1.0 - el.eccentricity * el.eccentricity) * cosE / r,
                         0.0};
    const Eigen::AngleAxisd Omega(el.longitudeOfAscendingNodeRad, Vector3::UnitZ());
    const Eigen::AngleAxisd inc  (el.inclinationRad,              Vector3::UnitX());
    const Eigen::AngleAxisd omega(el.argumentOfPeriapsisRad,      Vector3::UnitZ());
    return Omega * inc * omega * periV;
}
} // anonymous namespace

// ─── Construction / reset ────────────────────────────────────────────────────

Simulation::Simulation() { resetSolarSystem(); }

void Simulation::addBody(std::string name, double massKg, double radiusM,
                         Vector3 positionM, Vector3 velocityMPerS,
                         bool massive, bool hypothetical, bool observed,
                         double epochJd, OrbitalElements elements) {
    bodies_.push_back({std::move(name), massKg, radiusM, positionM, velocityMPerS,
                       massive, hypothetical, observed, epochJd, elements});
    trajectories_.push_back({positionM});
}

void Simulation::resetSolarSystem() {
    bodies_.clear();
    trajectories_.clear();
    encounterLog_.clear();
    timeS_ = 0.0;
    referenceEnergyJ_ = 0.0;

    // ── Sun ──────────────────────────────────────────────────────────────────
    addBody("Sun", SunMass, 6.957e8, Vector3::Zero(), Vector3::Zero());

    // ── Inner planets — realistic J2000 orbital elements (mean) ─────────────
    // Mercury: a=0.3871 AU, e=0.2056, i=7.00°, Ω=48.33°, ω=29.12°, M=174.80°
    addKeplerianBody("Mercury", 3.3011e23, 2.4397e6,
        {0.3871 * AU, 0.2056,
         7.00 * std::numbers::pi/180.0,
         48.33 * std::numbers::pi/180.0,
         29.12 * std::numbers::pi/180.0,
         0.0, 174.80 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // Venus: a=0.7233 AU, e=0.0068, i=3.39°, Ω=76.68°, ω=55.19°, M=50.44°
    addKeplerianBody("Venus", 4.8675e24, 6.0518e6,
        {0.7233 * AU, 0.0068,
         3.39 * std::numbers::pi/180.0,
         76.68 * std::numbers::pi/180.0,
         55.19 * std::numbers::pi/180.0,
         0.0, 50.44 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // Earth: a=1.0000 AU, e=0.0167, i=0.00°, Ω=0°, ω=102.94°, M=357.53°
    addKeplerianBody("Earth", 5.9722e24, 6.371e6,
        {AU, 0.0167,
         0.0,
         0.0,
         102.94 * std::numbers::pi/180.0,
         0.0, 357.53 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // Mars: a=1.5237 AU, e=0.0934, i=1.85°, Ω=49.56°, ω=286.50°, M=19.41°
    addKeplerianBody("Mars", 6.4171e23, 3.3895e6,
        {1.5237 * AU, 0.0934,
         1.85 * std::numbers::pi/180.0,
         49.56 * std::numbers::pi/180.0,
         286.50 * std::numbers::pi/180.0,
         0.0, 19.41 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // ── Outer planets ────────────────────────────────────────────────────────
    // Jupiter: a=5.2029 AU, e=0.0489, i=1.30°, Ω=100.47°, ω=273.87°, M=20.02°
    addKeplerianBody("Jupiter", 1.8982e27, 6.9911e7,
        {5.2029 * AU, 0.0489,
         1.30 * std::numbers::pi/180.0,
         100.47 * std::numbers::pi/180.0,
         273.87 * std::numbers::pi/180.0,
         0.0, 20.02 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // Saturn: a=9.5370 AU, e=0.0565, i=2.49°, Ω=113.64°, ω=339.39°, M=317.02°
    addKeplerianBody("Saturn", 5.6834e26, 5.8232e7,
        {9.5370 * AU, 0.0565,
         2.49 * std::numbers::pi/180.0,
         113.64 * std::numbers::pi/180.0,
         339.39 * std::numbers::pi/180.0,
         0.0, 317.02 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // Uranus: a=19.191 AU, e=0.0463, i=0.77°, Ω=74.00°, ω=97.77°, M=142.24°
    addKeplerianBody("Uranus", 8.6810e25, 2.5362e7,
        {19.191 * AU, 0.0463,
         0.77 * std::numbers::pi/180.0,
         74.00 * std::numbers::pi/180.0,
         97.77 * std::numbers::pi/180.0,
         0.0, 142.24 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // Neptune: a=30.069 AU, e=0.0095, i=1.77°, Ω=131.78°, ω=276.34°, M=256.23°
    addKeplerianBody("Neptune", 1.02413e26, 2.4622e7,
        {30.069 * AU, 0.0095,
         1.77 * std::numbers::pi/180.0,
         131.78 * std::numbers::pi/180.0,
         276.34 * std::numbers::pi/180.0,
         0.0, 256.23 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // ── TNOs — from MPC / literature ─────────────────────────────────────────
    // Sedna: a=543.7 AU, e=0.8599, i=11.93°, Ω=144.51°, ω=311.10°, M=358.60°
    addKeplerianBody("Sedna", 1.0e17, 500000.0,
        {543.7115274 * AU, 0.8598806,
         11.92528 * std::numbers::pi/180.0,
         144.50580 * std::numbers::pi/180.0,
         311.09926 * std::numbers::pi/180.0,
         0.0, 358.59566 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // 2012 VP113: a=273.1 AU, e=0.6929, i=24.00°, Ω=90.89°, ω=294.21°, M=3.59°
    addKeplerianBody("2012 VP113", 1.0e17, 350000.0,
        {273.13592 * AU, 0.7049256,
         24.00072 * std::numbers::pi/180.0,
         90.89434 * std::numbers::pi/180.0,
         294.20612 * std::numbers::pi/180.0,
         0.0, 3.58693 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // 2015 TG387 (Goblin): a=1389 AU, e=0.9532, i=11.67°, Ω=300.98°, ω=117.58°, M=359.62°
    addKeplerianBody("2015 TG387 (Goblin)", 1.0e17, 300000.0,
        {1389.0 * AU, 0.9532,
         11.67 * std::numbers::pi/180.0,
         300.98 * std::numbers::pi/180.0,
         117.58 * std::numbers::pi/180.0,
         0.0, 359.62 * std::numbers::pi/180.0}, ReferenceEpochJd);

    // 2008 KV42 (Drac): a=42.04 AU, e=0.4968, i=103.41° (retrograde!)
    addKeplerianBody("2008 KV42 (Drac)", 1.0e17, 300000.0,
        {42.035 * AU, 0.4968,
         103.41 * std::numbers::pi/180.0,
         260.91 * std::numbers::pi/180.0,
         132.61 * std::numbers::pi/180.0,
         0.0, 341.23 * std::numbers::pi/180.0}, ReferenceEpochJd);

    applyPbhPreset(0);
}

std::size_t Simulation::addKeplerianBody(std::string name, double massKg,
                                          double radiusM,
                                          const OrbitalElements& elements,
                                          double epochJd, bool hypothetical) {
    addBody(std::move(name), massKg, radiusM,
            stateFromElements(elements, massKg),
            velocityFromElements(elements, massKg),
            true, hypothetical, !hypothetical, epochJd, elements);
    return bodies_.size() - 1;
}

// ─── PBH Presets ─────────────────────────────────────────────────────────────
//
//  Preset 0 — Scholtz & Unwin 2020 (PRL 125, 051103)
//    Mass: 5 M⊕, a=450 AU, e=0.20, i=20°
//    (conservative estimate near centre of their mass-capture probability peak)
//
//  Preset 1 — Siraj, Chyba & Tremaine 2025 (ApJ 982, 198)
//    Mass: 4.4 M⊕, a=290 AU, e=0.29, i=6.8°
//    Best-fit from N-body TNO clustering with bias correction
//
//  Preset 2 — Batygin & Brown 2021 (AJ 162, 49)
//    Mass: 6.2 M⊕, a=380 AU, e=0.21 (q≈300 AU), i=16°
//    Updated MCMC + observational bias correction from Pan-STARRS / DES

void Simulation::applyPbhPreset(int preset) {
    activePbhPreset_ = std::clamp(preset, 0, 2);

    // Remove any existing hypothetical body
    auto it = std::find_if(bodies_.begin(), bodies_.end(),
                           [](const Body& b) { return b.hypothetical; });
    if (it != bodies_.end()) {
        const auto idx = static_cast<std::size_t>(it - bodies_.begin());
        bodies_.erase(it);
        trajectories_.erase(trajectories_.begin() + static_cast<std::ptrdiff_t>(idx));
    }

    struct Params { const char* name; double massEarth; double smaAU; double ecc; double incDeg; };
    static constexpr std::array<Params, 3> presets{{
        {"PBH (Scholtz-Unwin 2020)",         5.0,  450.0, 0.20, 20.0},
        {"PBH (Siraj-Chyba-Tremaine 2025)",  4.4,  290.0, 0.29,  6.8},
        {"PBH (Batygin-Brown 2021)",          6.2,  380.0, 0.21, 16.0},
    }};
    const auto& p = presets[static_cast<std::size_t>(activePbhPreset_)];

    const OrbitalElements elements{
        p.smaAU * AU,
        p.ecc,
        p.incDeg * std::numbers::pi / 180.0,
        0.0, 0.0, 0.0, 0.0};
    addKeplerianBody(p.name, p.massEarth * EarthMass,
                     schwarzschildRadiusM(p.massEarth * EarthMass),   // 1 m effectively
                     elements, ReferenceEpochJd, true);
}

// ─── Integrator — Leapfrog (symplectic 2nd-order) + RK4 (4th-order) ──────────

std::vector<Vector3> Simulation::accelerations() const {
    return accelerationsFromState({});
}

std::vector<Vector3> Simulation::accelerationsFromState(const State& s) const {
    const std::size_t n = bodies_.size();
    // If s is non-empty, use the provided positions; otherwise use current body positions
    const bool useS = !s.positions.empty();
    std::vector<Vector3> result(n, Vector3::Zero());
    constexpr double softening2 = 1.0e14;

    for (std::size_t i = 0; i < n; ++i) {
        if (!bodies_[i].massive) continue;
        const Vector3& pi = useS ? s.positions[i] : bodies_[i].positionM;
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j || !bodies_[j].massive) continue;
            const Vector3& pj = useS ? s.positions[j] : bodies_[j].positionM;
            const Vector3 delta = pj - pi;
            const double r2 = delta.squaredNorm() + softening2;
            const double r  = std::sqrt(r2);
            result[i] += GravitationalConstant * bodies_[j].massKg * delta / (r2 * r);
        }
    }
    return result;
}

void Simulation::advanceLeapfrog(double dtS) {
    // Velocity Störmer-Verlet (symplectic, 2nd-order, excellent for orbital mechanics)
    const auto first = accelerationsFromState({});
    for (std::size_t i = 0; i < bodies_.size(); ++i)
        if (bodies_[i].massive) bodies_[i].velocityMPerS += 0.5 * dtS * first[i];
    for (auto& body : bodies_) body.positionM += dtS * body.velocityMPerS;
    const auto second = accelerationsFromState({});
    for (std::size_t i = 0; i < bodies_.size(); ++i)
        if (bodies_[i].massive) bodies_[i].velocityMPerS += 0.5 * dtS * second[i];
}

void Simulation::advanceRK4(double dtS) {
    // Classic 4th-order Runge-Kutta: much better energy conservation on large steps
    const std::size_t n = bodies_.size();

    // Snapshot current state
    State s0;
    s0.positions.resize(n);
    s0.velocities.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        s0.positions[i]  = bodies_[i].positionM;
        s0.velocities[i] = bodies_[i].velocityMPerS;
    }

    // Helper: advance a state by (dr = v*h, dv = a*h)
    const auto evalDerivs = [&](const State& s) -> State {
        const auto accs = accelerationsFromState(s);
        State deriv;
        deriv.positions.resize(n);
        deriv.velocities.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            deriv.positions[i]  = s.velocities[i];
            deriv.velocities[i] = accs[i];
        }
        return deriv;
    };

    const auto addScaled = [&](const State& base, const State& d, double h) -> State {
        State out;
        out.positions.resize(n);
        out.velocities.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            out.positions[i]  = base.positions[i]  + h * d.positions[i];
            out.velocities[i] = base.velocities[i] + h * d.velocities[i];
        }
        return out;
    };

    const State k1 = evalDerivs(s0);
    const State k2 = evalDerivs(addScaled(s0, k1, 0.5 * dtS));
    const State k3 = evalDerivs(addScaled(s0, k2, 0.5 * dtS));
    const State k4 = evalDerivs(addScaled(s0, k3, dtS));

    // Combine: y1 = y0 + (dt/6)*(k1 + 2k2 + 2k3 + k4)
    for (std::size_t i = 0; i < n; ++i) {
        if (!bodies_[i].massive) continue;
        bodies_[i].positionM    = s0.positions[i]  + (dtS / 6.0) *
            (k1.positions[i]  + 2.0 * k2.positions[i]  + 2.0 * k3.positions[i]  + k4.positions[i]);
        bodies_[i].velocityMPerS = s0.velocities[i] + (dtS / 6.0) *
            (k1.velocities[i] + 2.0 * k2.velocities[i] + 2.0 * k3.velocities[i] + k4.velocities[i]);
    }
}

void Simulation::checkEncounters() {
    // Find the PBH
    const Body* pbh = nullptr;
    std::size_t pbhIdx = 0;
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (bodies_[i].hypothetical) { pbh = &bodies_[i]; pbhIdx = i; break; }
    }
    if (!pbh) return;

    for (std::size_t i = 1; i < bodies_.size(); ++i) {
        if (i == pbhIdx || bodies_[i].hypothetical) continue;
        const double sep = (pbh->positionM - bodies_[i].positionM).norm();
        // Hill sphere of PBH relative to Sun
        const double hillPbh = hillSphereRadiusM(pbhIdx, 0);
        if (sep < 2.0 * hillPbh && hillPbh > 0.0) {
            // Check if this is a new encounter (not logged within last 365 days)
            constexpr double minGapS = 365.25 * 86400.0;
            bool alreadyLogged = false;
            for (const auto& ev : encounterLog_) {
                if (ev.bodyName == bodies_[i].name && (timeS_ - ev.timeS) < minGapS) {
                    alreadyLogged = true;
                    break;
                }
            }
            if (!alreadyLogged) {
                constexpr double AU = 1.495978707e11;
                EncounterEvent ev;
                ev.timeS           = timeS_;
                ev.bodyName        = bodies_[i].name;
                ev.separationAU    = sep / AU;
                ev.relativeSpeedKmS = (pbh->velocityMPerS - bodies_[i].velocityMPerS).norm() / 1000.0;
                encounterLog_.push_front(ev);
                if (encounterLog_.size() > 200) encounterLog_.pop_back();
            }
        }
    }
}

void Simulation::advance(double dtS) {
    if (!std::isfinite(dtS) || dtS <= 0.0)
        throw std::invalid_argument("Simulation timestep must be finite and positive");

    if (integrator_ == Integrator::RungeKutta4)
        advanceRK4(dtS);
    else
        advanceLeapfrog(dtS);

    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        auto& history = trajectories_[i];
        history.push_back(bodies_[i].positionM);
        if (history.size() > 2000) history.erase(history.begin());
    }
    timeS_ += dtS;
    checkEncounters();
}

// ─── Accessors ───────────────────────────────────────────────────────────────
const std::vector<Vector3>& Simulation::trajectory(std::size_t bodyIndex) const {
    if (bodyIndex >= trajectories_.size()) throw std::out_of_range("Invalid trajectory index");
    return trajectories_[bodyIndex];
}

void Simulation::setPbhMassKg(double massKg) {
    auto it = std::find_if(bodies_.begin(), bodies_.end(),
                           [](const Body& b) { return b.hypothetical; });
    if (it == bodies_.end() || !std::isfinite(massKg) || massKg <= 0.0)
        throw std::invalid_argument("PBH mass must be finite and positive");
    it->massKg = massKg;
}

void Simulation::setPbhOrbit(double semiMajorAxisM, double eccentricity) {
    if (!std::isfinite(semiMajorAxisM) || semiMajorAxisM <= 0.0 ||
        !std::isfinite(eccentricity)   || eccentricity < 0.0 || eccentricity >= 1.0)
        throw std::invalid_argument("PBH orbit requires a > 0 and 0 <= e < 1");
    auto it = std::find_if(bodies_.begin(), bodies_.end(),
                           [](const Body& b) { return b.hypothetical; });
    if (it == bodies_.end()) throw std::runtime_error("PBH body is not configured");
    it->referenceElements.semiMajorAxisM = semiMajorAxisM;
    it->referenceElements.eccentricity   = eccentricity;
    it->positionM    = stateFromElements  (it->referenceElements, it->massKg);
    it->velocityMPerS = velocityFromElements(it->referenceElements, it->massKg);
    const auto idx = static_cast<std::size_t>(it - bodies_.begin());
    trajectories_[idx].clear();
    trajectories_[idx].push_back(it->positionM);
}

// ─── Telemetry ────────────────────────────────────────────────────────────────
Telemetry Simulation::telemetry() const {
    constexpr double AU = 1.495978707e11;
    constexpr double YEAR_S = 365.25 * 86400.0;

    Telemetry value{.timeS = timeS_};
    Vector3 momentum = Vector3::Zero();
    Vector3 angMom   = Vector3::Zero();

    for (const auto& body : bodies_) {
        value.kineticEnergyJ += 0.5 * body.massKg * body.velocityMPerS.squaredNorm();
        momentum += body.massKg * body.velocityMPerS;
        angMom   += body.massKg * body.positionM.cross(body.velocityMPerS);
    }
    for (std::size_t i = 0; i < bodies_.size(); ++i)
        for (std::size_t j = i + 1; j < bodies_.size(); ++j) {
            const double r = (bodies_[j].positionM - bodies_[i].positionM).norm();
            if (r > 0.0)
                value.potentialEnergyJ -= GravitationalConstant *
                    bodies_[i].massKg * bodies_[j].massKg / r;
        }
    value.totalEnergyJ               = value.kineticEnergyJ + value.potentialEnergyJ;
    value.momentumKgMPerS            = momentum.norm();
    value.angularMomentumKgM2PerS    = angMom;
    value.angularMomentumMagKgM2PerS = angMom.norm();

    // Energy drift tracking (set reference on first call after reset)
    if (referenceEnergyJ_ == 0.0 && value.totalEnergyJ != 0.0)
        referenceEnergyJ_ = value.totalEnergyJ;
    if (referenceEnergyJ_ != 0.0)
        value.energyDriftFraction = (value.totalEnergyJ - referenceEnergyJ_) /
                                     std::abs(referenceEnergyJ_);

    // PBH derived metrics
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        if (!bodies_[i].hypothetical) continue;
        try {
            const auto el = orbitalElements(i, 0);
            if (std::isfinite(el.semiMajorAxisM) && el.semiMajorAxisM > 0.0) {
                // Kepler's 3rd law: T^2 = 4pi^2 a^3 / (G*M_sun)
                const double muSun = GravitationalConstant * bodies_[0].massKg;
                const double T = 2.0 * std::numbers::pi *
                    std::sqrt(std::pow(el.semiMajorAxisM, 3.0) / muSun);
                value.pbhPeriodYr = T / YEAR_S;
            }
            value.pbhHillSphereAU = hillSphereRadiusM(i, 0) / AU;

            // Tisserand parameter w.r.t. Neptune (index 7: Neptune is 8th body)
            // T_N = a_N/a + 2*sqrt(a/a_N * (1 - e^2)) * cos(i)
            if (bodies_.size() > 7) {
                const auto elN = orbitalElements(7, 0); // Neptune
                if (elN.semiMajorAxisM > 0.0 && el.semiMajorAxisM > 0.0) {
                    const double ratio = el.semiMajorAxisM / elN.semiMajorAxisM;
                    value.neptuneTisserandPbh = (elN.semiMajorAxisM / el.semiMajorAxisM) +
                        2.0 * std::sqrt(ratio * (1.0 - el.eccentricity * el.eccentricity)) *
                        std::cos(el.inclinationRad);
                }
            }
        } catch (...) {}
        break;
    }
    return value;
}

// ─── Field helpers ────────────────────────────────────────────────────────────
double Simulation::gravitationalFieldMagnitude(const Vector3& positionM,
                                                std::size_t sourceIndex) const {
    if (sourceIndex >= bodies_.size()) throw std::out_of_range("Invalid field source index");
    const Vector3 delta = positionM - bodies_[sourceIndex].positionM;
    const double r2 = delta.squaredNorm();
    if (r2 < 1.0) return 0.0;
    return GravitationalConstant * bodies_[sourceIndex].massKg / r2;
}

double Simulation::gravitationalPotential(const Vector3& positionM, int sourceIndex) const {
    if (sourceIndex >= static_cast<int>(bodies_.size()))
        throw std::out_of_range("Invalid potential source index");
    double potential = 0.0;
    const auto accumulate = [&](const Body& body) {
        const double soft = std::max(body.radiusM, 1.0e-6 * AU);
        const double dist = std::sqrt((positionM - body.positionM).squaredNorm() + soft * soft);
        potential -= GravitationalConstant * body.massKg / dist;
    };
    if (sourceIndex >= 0) accumulate(bodies_[static_cast<std::size_t>(sourceIndex)]);
    else for (const auto& body : bodies_) if (body.massive) accumulate(body);
    return potential;
}

double Simulation::hillSphereRadiusM(std::size_t bodyIdx, std::size_t primaryIdx) const {
    if (bodyIdx >= bodies_.size() || primaryIdx >= bodies_.size() || bodyIdx == primaryIdx)
        throw std::out_of_range("Invalid body index for Hill sphere");
    const auto& body    = bodies_[bodyIdx];
    const auto& primary = bodies_[primaryIdx];
    const double r  = (body.positionM - primary.positionM).norm();
    if (r <= 0.0 || primary.massKg <= 0.0) return 0.0;
    // r_H = a(1-e) * (m/(3M))^(1/3) — use instantaneous separation for live r_H
    return r * std::cbrt(body.massKg / (3.0 * primary.massKg));
}

// ─── Orbital Elements ─────────────────────────────────────────────────────────
OrbitalElements Simulation::orbitalElements(std::size_t bodyIndex,
                                             std::size_t primaryIndex) const {
    if (bodyIndex >= bodies_.size() || primaryIndex >= bodies_.size() ||
        bodyIndex == primaryIndex)
        throw std::out_of_range("Invalid orbital body index");
    const auto& body    = bodies_[bodyIndex];
    const auto& primary = bodies_[primaryIndex];
    const Vector3 r = body.positionM    - primary.positionM;
    const Vector3 v = body.velocityMPerS - primary.velocityMPerS;
    const double mu      = GravitationalConstant * (body.massKg + primary.massKg);
    const double rNorm   = std::max(r.norm(), 1.0);
    const double vSq     = v.squaredNorm();
    const Vector3 h      = r.cross(v);
    const double hNorm   = std::max(h.norm(), 1e-12);
    const Vector3 eVec   = v.cross(h) / mu - r / rNorm;
    const double e       = std::clamp(eVec.norm(), 0.0, 0.9999);
    const double denom   = 2.0 / rNorm - vSq / mu;
    const double semiMajor = std::abs(denom) > 1e-25 ? (1.0 / denom) : (rNorm);
    const double inc     = std::acos(std::clamp(h.z() / hNorm, -1.0, 1.0));
    const Vector3 node(-h.y(), h.x(), 0.0);
    const double nodeNorm = node.norm();
    const double lan  = nodeNorm > 0.0 ? std::atan2(node.y(), node.x()) : 0.0;
    const double arg  = (nodeNorm > 0.0 && e > 0.0)
        ? std::acos(std::clamp(node.dot(eVec) / (nodeNorm * e), -1.0, 1.0)) : 0.0;
    const double trueAnom = e > 0.0
        ? std::acos(std::clamp(eVec.dot(r) / (e * rNorm), -1.0, 1.0)) : 0.0;
    const double eccAnom  = 2.0 * std::atan2(
        std::sqrt(std::max(0.0, 1.0 - e)) * std::sin(trueAnom / 2.0),
        std::sqrt(1.0 + e) * std::cos(trueAnom / 2.0));
    const double meanAnom = eccAnom - e * std::sin(eccAnom);
    return {semiMajor, e, inc, lan, arg, trueAnom, meanAnom};
}

// ─── Tisserand Parameter ──────────────────────────────────────────────────────
// T_p = a_p/a + 2*sqrt(a/a_p * (1-e^2)) * cos(i)
// A conserved quantity in the restricted three-body problem. T < 3 implies
// Jupiter-family comet ancestry; T > 3 implies classical Kuiper Belt or scattered
// disc origin. Used here to classify TNO-PBH resonance families.
double Simulation::tisserandParameter(std::size_t bodyIdx, std::size_t perturberIdx) const {
    if (bodyIdx >= bodies_.size() || perturberIdx >= bodies_.size() || bodyIdx == perturberIdx)
        throw std::out_of_range("Invalid index for Tisserand parameter");
    try {
        const auto el  = orbitalElements(bodyIdx,     0);  // body relative to Sun
        const auto elP = orbitalElements(perturberIdx, 0); // perturber relative to Sun
        if (elP.semiMajorAxisM <= 0.0 || el.semiMajorAxisM <= 0.0) return 0.0;
        const double ratio = el.semiMajorAxisM / elP.semiMajorAxisM;
        return (elP.semiMajorAxisM / el.semiMajorAxisM) +
               2.0 * std::sqrt(ratio * (1.0 - el.eccentricity * el.eccentricity)) *
               std::cos(el.inclinationRad);
    } catch (...) { return 0.0; }
}

// ─── Orbital Period ───────────────────────────────────────────────────────────
double Simulation::orbitalPeriodS(std::size_t bodyIdx, std::size_t primaryIdx) const {
    if (bodyIdx >= bodies_.size() || primaryIdx >= bodies_.size() || bodyIdx == primaryIdx)
        throw std::out_of_range("Invalid index for orbital period");
    try {
        const auto el = orbitalElements(bodyIdx, primaryIdx);
        if (!std::isfinite(el.semiMajorAxisM) || el.semiMajorAxisM <= 0.0 || el.eccentricity >= 1.0)
            return 0.0;
        const double mu = GravitationalConstant *
            (bodies_[bodyIdx].massKg + bodies_[primaryIdx].massKg);
        return 2.0 * std::numbers::pi * std::sqrt(std::pow(el.semiMajorAxisM, 3.0) / mu);
    } catch (...) { return 0.0; }
}

} // namespace erebus::physics

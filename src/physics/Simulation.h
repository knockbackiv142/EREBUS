#pragma once

#include "Body.h"
#include <cstddef>
#include <deque>
#include <vector>

namespace erebus::physics {

enum class Integrator { LeapfrogVerlet, RungeKutta4 };

class Simulation {
public:
    static constexpr double GravitationalConstant = 6.67430e-11;
    static constexpr double SpeedOfLight          = 2.99792458e8;  // m/s

    Simulation();
    void resetSolarSystem();
    void advance(double dtS);
    void setTimeS(double timeS) noexcept { timeS_ = timeS; }
    void setPbhMassKg(double massKg);
    void setPbhOrbit(double semiMajorAxisM, double eccentricity);
    void applyPbhPreset(int preset);  // 0=Scholtz-Unwin, 1=Siraj-Chyba-Tremaine, 2=Batygin-Brown 2021
    void setIntegrator(Integrator integrator) noexcept { integrator_ = integrator; }
    std::size_t addKeplerianBody(
        std::string name, double massKg, double radiusM,
        const OrbitalElements& elements, double epochJd,
        bool hypothetical = false);

    [[nodiscard]] const std::vector<Body>& bodies() const noexcept { return bodies_; }
    [[nodiscard]] std::vector<Body>& bodies() noexcept { return bodies_; }
    [[nodiscard]] const std::vector<Vector3>& trajectory(std::size_t bodyIndex) const;
    [[nodiscard]] double timeS() const noexcept { return timeS_; }
    [[nodiscard]] Telemetry telemetry() const;
    [[nodiscard]] double gravitationalFieldMagnitude(
        const Vector3& positionM, std::size_t sourceIndex) const;
    [[nodiscard]] double gravitationalPotential(const Vector3& positionM,
                                                int sourceIndex = -1) const;
    [[nodiscard]] OrbitalElements orbitalElements(std::size_t bodyIndex,
                                                   std::size_t primaryIndex = 0) const;

    // Tisserand parameter: T_p = a_p/a + 2*sqrt(a/a_p * (1-e^2)) * cos(i)
    // where a_p is the perturber (Neptune or PBH) semi-major axis
    [[nodiscard]] double tisserandParameter(std::size_t bodyIdx,
                                             std::size_t perturberIdx) const;

    // Orbital period in seconds (Kepler's 3rd law, two-body relative to primary)
    [[nodiscard]] double orbitalPeriodS(std::size_t bodyIdx,
                                         std::size_t primaryIdx = 0) const;

    // Utility: Schwarzschild radius for a body of given mass (m)
    [[nodiscard]] static double schwarzschildRadiusM(double massKg) noexcept {
        return 2.0 * GravitationalConstant * massKg / (SpeedOfLight * SpeedOfLight);
    }

    // Hill sphere radius of body[bodyIdx] relative to body[primaryIdx] (m)
    [[nodiscard]] double hillSphereRadiusM(std::size_t bodyIdx,
                                            std::size_t primaryIdx = 0) const;

    // Active preset index (0,1,2)
    [[nodiscard]] int activePbhPreset() const noexcept { return activePbhPreset_; }

    // Close-encounter event log (PBH within 2x Hill sphere of a non-Sun body)
    [[nodiscard]] const std::deque<EncounterEvent>& encounterLog() const noexcept { return encounterLog_; }
    void clearEncounterLog() noexcept { encounterLog_.clear(); }

private:
    struct State { std::vector<Vector3> positions; std::vector<Vector3> velocities; };
    [[nodiscard]] std::vector<Vector3> accelerations() const;
    [[nodiscard]] std::vector<Vector3> accelerationsFromState(const State& s) const;
    void advanceRK4(double dtS);
    void advanceLeapfrog(double dtS);
    void checkEncounters();

    void addBody(std::string name, double massKg, double radiusM,
                 Vector3 positionM, Vector3 velocityMPerS,
                 bool massive = true, bool hypothetical = false,
                 bool observed = true, double epochJd = 2460676.5,
                 OrbitalElements elements = {});

    std::vector<Body> bodies_;
    std::vector<std::vector<Vector3>> trajectories_;
    std::deque<EncounterEvent> encounterLog_;
    double timeS_{};
    int activePbhPreset_{0};
    Integrator integrator_{Integrator::LeapfrogVerlet};
    mutable double referenceEnergyJ_{0.0};  // set on first telemetry() call
};

} // namespace erebus::physics

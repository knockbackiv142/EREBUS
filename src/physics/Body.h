#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <string>

namespace erebus::physics {

using Vector3 = Eigen::Vector3d;

struct OrbitalElements {
    double semiMajorAxisM{};
    double eccentricity{};
    double inclinationRad{};
    double longitudeOfAscendingNodeRad{};
    double argumentOfPeriapsisRad{};
    double trueAnomalyRad{};
    double meanAnomalyRad{};
};

struct Body {
    std::string name;
    double massKg{};
    double radiusM{};
    Vector3 positionM{Vector3::Zero()};
    Vector3 velocityMPerS{Vector3::Zero()};
    bool massive{true};
    bool hypothetical{false};
    bool observed{true};
    double referenceEpochJd{};
    OrbitalElements referenceElements{};
};

struct Telemetry {
    double timeS{};
    double kineticEnergyJ{};
    double potentialEnergyJ{};
    double totalEnergyJ{};
    double momentumKgMPerS{};         // total linear momentum magnitude
    // Extended conserved quantities
    Vector3 angularMomentumKgM2PerS{Vector3::Zero()}; // total system L
    double angularMomentumMagKgM2PerS{};              // |L|
    double energyDriftFraction{};     // (E - E0) / |E0|, 0 until E0 is set
    // Orbital analysis outputs (populated once per telemetry() call)
    double pbhPeriodYr{};             // PBH orbital period (yr)
    double neptuneTisserandPbh{};     // Tisserand parameter of PBH w.r.t. Neptune
    double pbhHillSphereAU{};         // PBH Hill sphere in AU
};

// Logged close-encounter event (PBH within 2x Hill sphere of a body)
struct EncounterEvent {
    double timeS{};
    std::string bodyName{};
    double separationAU{};
    double relativeSpeedKmS{}; // km/s
};

} // namespace erebus::physics

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
    double momentumKgMPerS{};
};

} // namespace erebus::physics

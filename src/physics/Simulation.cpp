#include "Simulation.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace erebus::physics {
namespace {
constexpr double AU = 1.495978707e11;
constexpr double SunMass = 1.98847e30;
constexpr double EarthMass = 5.9722e24;
constexpr double ReferenceEpochJd = 2460840.5;
Vector3 stateFromElements(const OrbitalElements& elements, double massKg) {
    const double mu = Simulation::GravitationalConstant * (SunMass + massKg);
    const double meanAnomaly = elements.meanAnomalyRad;
    double eccentricAnomaly = meanAnomaly;
    for (int iteration = 0; iteration < 12; ++iteration)
        eccentricAnomaly -= (eccentricAnomaly - elements.eccentricity *
            std::sin(eccentricAnomaly) - meanAnomaly) /
            (1.0 - elements.eccentricity * std::cos(eccentricAnomaly));
    const double cosE = std::cos(eccentricAnomaly);
    const double sinE = std::sin(eccentricAnomaly);
    const double radius = elements.semiMajorAxisM *
        (1.0 - elements.eccentricity * cosE);
    const Vector3 perifocal{
        elements.semiMajorAxisM * (cosE - elements.eccentricity),
        elements.semiMajorAxisM * std::sqrt(1.0 - elements.eccentricity *
            elements.eccentricity) * sinE, 0.0};
    const Vector3 perifocalVelocity{
        -std::sqrt(mu * elements.semiMajorAxisM) * sinE / radius,
        std::sqrt(mu * elements.semiMajorAxisM) *
            std::sqrt(1.0 - elements.eccentricity * elements.eccentricity) *
            cosE / radius, 0.0};
    const Eigen::AngleAxisd rotation(elements.longitudeOfAscendingNodeRad,
                                      Vector3::UnitZ());
    const Eigen::AngleAxisd inclination(elements.inclinationRad, Vector3::UnitX());
    const Eigen::AngleAxisd argument(elements.argumentOfPeriapsisRad,
                                     Vector3::UnitZ());
    return rotation * inclination * argument * perifocal;
}

Vector3 velocityFromElements(const OrbitalElements& elements, double massKg) {
    const double mu = Simulation::GravitationalConstant * (SunMass + massKg);
    double eccentricAnomaly = elements.meanAnomalyRad;
    for (int iteration = 0; iteration < 12; ++iteration)
        eccentricAnomaly -= (eccentricAnomaly - elements.eccentricity *
            std::sin(eccentricAnomaly) - elements.meanAnomalyRad) /
            (1.0 - elements.eccentricity * std::cos(eccentricAnomaly));
    const double radius = elements.semiMajorAxisM *
        (1.0 - elements.eccentricity * std::cos(eccentricAnomaly));
    const Vector3 velocity{
        -std::sqrt(mu * elements.semiMajorAxisM) * std::sin(eccentricAnomaly) / radius,
        std::sqrt(mu * elements.semiMajorAxisM) *
            std::sqrt(1.0 - elements.eccentricity * elements.eccentricity) *
            std::cos(eccentricAnomaly) / radius, 0.0};
    const Eigen::AngleAxisd rotation(elements.longitudeOfAscendingNodeRad,
                                      Vector3::UnitZ());
    const Eigen::AngleAxisd inclination(elements.inclinationRad, Vector3::UnitX());
    const Eigen::AngleAxisd argument(elements.argumentOfPeriapsisRad,
                                     Vector3::UnitZ());
    return rotation * inclination * argument * velocity;
}
}

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
    timeS_ = 0.0;
    addBody("Sun", SunMass, 6.957e8, Vector3::Zero(), Vector3::Zero());
    addBody("Mercury", 3.3011e23, 2.4397e6, {0.387 * AU, 0, 0},
            {0, 47870, 0});
    addBody("Venus", 4.8675e24, 6.0518e6, {0.723 * AU, 0, 0},
            {0, 35020, 0});
    addBody("Earth", 5.9722e24, 6.371e6, {AU, 0, 0}, {0, 29784, 0});
    addBody("Mars", 6.4171e23, 3.3895e6, {1.524 * AU, 0, 0},
            {0, 24077, 0});
    addBody("Jupiter", 1.8982e27, 6.9911e7, {5.204 * AU, 0, 0},
            {0, 13070, 0});
    addBody("Saturn", 5.6834e26, 5.8232e7, {9.537 * AU, 0, 0},
            {0, 9680, 0});
    addBody("Uranus", 8.6810e25, 2.5362e7, {19.191 * AU, 0, 0},
            {0, 6800, 0});
    addBody("Neptune", 1.02413e26, 2.4622e7, {30.07 * AU, 0, 0},
            {0, 5430, 0});
    addKeplerianBody("Sedna", 1.0e17, 500000.0,
        {543.7115274 * AU, 0.8598806, 11.92528 * std::numbers::pi / 180.0,
         144.50580 * std::numbers::pi / 180.0, 311.09926 * std::numbers::pi / 180.0,
         0.0, 358.59566 * std::numbers::pi / 180.0}, ReferenceEpochJd);
    addKeplerianBody("2012 VP113", 1.0e17, 350000.0,
        {273.13592 * AU, 0.7049256, 24.00072 * std::numbers::pi / 180.0,
         90.89434 * std::numbers::pi / 180.0, 294.20612 * std::numbers::pi / 180.0,
         0.0, 3.58693 * std::numbers::pi / 180.0}, ReferenceEpochJd);
    addKeplerianBody("2015 TG387 (Goblin)", 1.0e17, 300000.0,
        {1389.0 * AU, 0.9532, 11.67 * std::numbers::pi / 180.0,
         300.98 * std::numbers::pi / 180.0, 117.58 * std::numbers::pi / 180.0,
         0.0, 359.62 * std::numbers::pi / 180.0}, ReferenceEpochJd);
    addKeplerianBody("2008 KV42 (Drac)", 1.0e17, 300000.0,
        {42.035 * AU, 0.4968, 103.41 * std::numbers::pi / 180.0,
         260.91 * std::numbers::pi / 180.0, 132.61 * std::numbers::pi / 180.0,
         0.0, 341.23 * std::numbers::pi / 180.0}, ReferenceEpochJd);
    applyPbhPreset(false);
}

std::size_t Simulation::addKeplerianBody(std::string name, double massKg,
                                          double radiusM,
                                          const OrbitalElements& elements,
                                          double epochJd, bool hypothetical) {
    addBody(std::move(name), massKg, radiusM, stateFromElements(elements, massKg),
            velocityFromElements(elements, massKg), true, hypothetical,
            !hypothetical, epochJd, elements);
    return bodies_.size() - 1;
}

void Simulation::applyPbhPreset(bool alternative) {
    auto it = std::find_if(bodies_.begin(), bodies_.end(),
                           [](const Body& body) { return body.hypothetical; });
    if (it != bodies_.end()) {
        const auto index = static_cast<std::size_t>(it - bodies_.begin());
        bodies_.erase(it);
        trajectories_.erase(trajectories_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    const OrbitalElements elements{
        (alternative ? 290.0 : 450.0) * AU,
        alternative ? 0.29 : 0.20,
        (alternative ? 6.8 : 20.0) * std::numbers::pi / 180.0,
        0.0, 0.0, 0.0, 0.0};
    addKeplerianBody(alternative ? "PBH (Siraj-Chyba-Tremaine 2025)" :
                                  "PBH (Scholtz-Unwin 2020)",
                     (alternative ? 4.4 : 5.0) * EarthMass, 1.0,
                     elements, ReferenceEpochJd, true);
}

std::vector<Vector3> Simulation::accelerations() const {
    std::vector<Vector3> result(bodies_.size(), Vector3::Zero());
#pragma omp parallel for if(bodies_.size() > 32)
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
        if (!bodies_[i].massive) continue;
        for (std::size_t j = 0; j < bodies_.size(); ++j) {
            if (i == static_cast<int>(j) || !bodies_[j].massive) continue;
            const Vector3 delta = bodies_[j].positionM - bodies_[i].positionM;
            const double distanceSquared = delta.squaredNorm();
            if (distanceSquared < 1.0) continue;
            result[i] += GravitationalConstant * bodies_[j].massKg * delta /
                         (distanceSquared * std::sqrt(distanceSquared));
        }
    }
    return result;
}

void Simulation::advance(double dtS) {
    if (!std::isfinite(dtS) || dtS <= 0.0) {
        throw std::invalid_argument("Simulation timestep must be finite and positive");
    }
    const auto first = accelerations();
    for (std::size_t i = 0; i < bodies_.size(); ++i)
        if (bodies_[i].massive) bodies_[i].velocityMPerS += 0.5 * dtS * first[i];
    for (auto& body : bodies_) body.positionM += dtS * body.velocityMPerS;
    const auto second = accelerations();
    for (std::size_t i = 0; i < bodies_.size(); ++i)
        if (bodies_[i].massive) bodies_[i].velocityMPerS += 0.5 * dtS * second[i];
    for (std::size_t i = 0; i < bodies_.size(); ++i) {
        auto& history = trajectories_[i];
        history.push_back(bodies_[i].positionM);
        if (history.size() > 2000) history.erase(history.begin());
    }
    timeS_ += dtS;
}

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
        !std::isfinite(eccentricity) || eccentricity < 0.0 || eccentricity >= 1.0)
        throw std::invalid_argument("PBH orbit requires a > 0 and 0 <= e < 1");
    auto it = std::find_if(bodies_.begin(), bodies_.end(),
                           [](const Body& b) { return b.hypothetical; });
    if (it == bodies_.end()) throw std::runtime_error("PBH body is not configured");
    it->referenceElements.semiMajorAxisM = semiMajorAxisM;
    it->referenceElements.eccentricity = eccentricity;
    it->positionM = stateFromElements(it->referenceElements, it->massKg);
    it->velocityMPerS = velocityFromElements(it->referenceElements, it->massKg);
    const auto index = static_cast<std::size_t>(it - bodies_.begin());
    trajectories_[index].clear();
    trajectories_[index].push_back(it->positionM);
}

Telemetry Simulation::telemetry() const {
    Telemetry value{.timeS = timeS_};
    Vector3 momentum = Vector3::Zero();
    for (const auto& body : bodies_) {
        value.kineticEnergyJ += 0.5 * body.massKg * body.velocityMPerS.squaredNorm();
        momentum += body.massKg * body.velocityMPerS;
    }

    for (std::size_t i = 0; i < bodies_.size(); ++i)
        for (std::size_t j = i + 1; j < bodies_.size(); ++j) {
            const double r = (bodies_[j].positionM - bodies_[i].positionM).norm();
            if (r > 0.0) value.potentialEnergyJ -= GravitationalConstant *
                bodies_[i].massKg * bodies_[j].massKg / r;
        }
    value.totalEnergyJ = value.kineticEnergyJ + value.potentialEnergyJ;
    value.momentumKgMPerS = momentum.norm();
    return value;
}

double Simulation::gravitationalFieldMagnitude(const Vector3& positionM,
                                                std::size_t sourceIndex) const {
    if (sourceIndex >= bodies_.size()) throw std::out_of_range("Invalid field source index");
    const Vector3 delta = positionM - bodies_[sourceIndex].positionM;
    const double distanceSquared = delta.squaredNorm();
    if (distanceSquared < 1.0) return 0.0;
    return GravitationalConstant * bodies_[sourceIndex].massKg / distanceSquared;
}

double Simulation::gravitationalPotential(const Vector3& positionM,
                                           int sourceIndex) const {
    if (sourceIndex >= static_cast<int>(bodies_.size()))
        throw std::out_of_range("Invalid potential source index");
    double potential = 0.0;
    const auto accumulate = [&](const Body& body) {
        const double softening = std::max(body.radiusM, 1.0e-6 * 1.495978707e11);
        const double distance = std::sqrt((positionM - body.positionM).squaredNorm() +
                                           softening * softening);
        potential -= GravitationalConstant * body.massKg / distance;
    };
    if (sourceIndex >= 0) accumulate(bodies_[static_cast<std::size_t>(sourceIndex)]);
    else for (const auto& body : bodies_) if (body.massive) accumulate(body);
    return potential;
}

OrbitalElements Simulation::orbitalElements(std::size_t bodyIndex,
                                             std::size_t primaryIndex) const {
    if (bodyIndex >= bodies_.size() || primaryIndex >= bodies_.size() ||
        bodyIndex == primaryIndex) throw std::out_of_range("Invalid orbital body index");
    const auto& body = bodies_[bodyIndex];
    const auto& primary = bodies_[primaryIndex];
    const Vector3 r = body.positionM - primary.positionM;
    const Vector3 v = body.velocityMPerS - primary.velocityMPerS;
    const double mu = GravitationalConstant * (body.massKg + primary.massKg);
    const double rNorm = r.norm();
    const double vSquared = v.squaredNorm();
    const Vector3 h = r.cross(v);
    const Vector3 eVector = v.cross(h) / mu - r / rNorm;
    const double e = eVector.norm();
    const double semiMajor = 1.0 / (2.0 / rNorm - vSquared / mu);
    const double inclination = std::acos(std::clamp(h.z() / h.norm(), -1.0, 1.0));
    const Vector3 node(-h.y(), h.x(), 0);
    const double nodeNorm = node.norm();
    const double lan = nodeNorm > 0.0 ? std::atan2(node.y(), node.x()) : 0.0;
    const double arg = nodeNorm > 0.0 && e > 0.0
        ? std::acos(std::clamp(node.dot(eVector) / (nodeNorm * e), -1.0, 1.0)) : 0.0;
    const double trueAnomaly = e > 0.0
        ? std::acos(std::clamp(eVector.dot(r) / (e * rNorm), -1.0, 1.0)) : 0.0;
    const double eccentricAnomaly = 2.0 * std::atan2(
        std::sqrt(1.0 - e) * std::sin(trueAnomaly / 2.0),
        std::sqrt(1.0 + e) * std::cos(trueAnomaly / 2.0));
    const double meanAnomaly = eccentricAnomaly - e * std::sin(eccentricAnomaly);
    return {semiMajor, e, inclination, lan, arg, trueAnomaly, meanAnomaly};
}

} // namespace erebus::physics

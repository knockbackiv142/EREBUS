#include "UniverseView.h"
#include <QKeyEvent>
#include <QLineF>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QVector3D>
#include <QDebug>
#include <array>
#include <functional>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

namespace erebus::render {
namespace {
constexpr double AU = 1.495978707e11;
constexpr float  Pi = static_cast<float>(std::numbers::pi);
}

UniverseView::UniverseView(QWidget* parent) : QOpenGLWidget(parent) {
    timer_.setInterval(28); // ~35 FPS: smooth motion with low iGPU power consumption
    connect(&timer_, &QTimer::timeout, this, &UniverseView::tick);
    timer_.start();
    setFocusPolicy(Qt::StrongFocus);
}

// ─── initializeGL ─────────────────────────────────────────────────────────────

void UniverseView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.002F, 0.004F, 0.012F, 1.0F);

    // ── Sphere/orbit shader (Phong) ──────────────────────────────────────────
    shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        attribute vec3 position;
        attribute vec3 normal;
        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        varying vec3 worldNormal;
        varying vec3 worldPosition;
        void main() {
            vec4 world = model * vec4(position, 1.0);
            worldPosition = world.xyz;
            worldNormal = vec3(model * vec4(normal, 0.0));
            gl_Position = projection * view * world;
        })");
    shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        varying vec3 worldNormal;
        varying vec3 worldPosition;
        uniform vec4 bodyColor;
        uniform vec3 lightPosition;
        void main() {
            vec3 normal = normalize(worldNormal);
            vec3 light  = normalize(lightPosition - worldPosition);
            float diff  = max(dot(normal, light), 0.0);
            float amb   = 0.18;
            gl_FragColor = vec4(bodyColor.rgb * (amb + diff * 0.82), bodyColor.a);
        })");
    if (!shader_.link())
        qWarning() << "Erebus sphere shader link failed:" << shader_.log();

    createSphereMesh();

    orbitVao_.create(); orbitVbo_.create();
    fieldVao_.create(); fieldVbo_.create();

    // ── Field shader (vertex-colored) ───────────────────────────────────────
    fieldShader_.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        attribute vec3 position;
        attribute vec4 color;
        uniform mat4 view;
        uniform mat4 projection;
        varying vec4 vertexColor;
        void main() {
            vertexColor = color;
            gl_Position = projection * view * vec4(position, 1.0);
            gl_PointSize = 5.0;
        })");
    fieldShader_.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        varying vec4 vertexColor;
        void main() { gl_FragColor = vertexColor; })");
    if (!fieldShader_.link())
        qWarning() << "Erebus field shader link failed:" << fieldShader_.log();
}

// ─── Sphere mesh ──────────────────────────────────────────────────────────────

void UniverseView::createSphereMesh() {
    constexpr int rings = 12, segments = 16;
    for (int ring = 0; ring < rings; ++ring) {
        const float v0 = static_cast<float>(ring)     / rings;
        const float v1 = static_cast<float>(ring + 1) / rings;
        const float p0 = Pi * (v0 - 0.5F);
        const float p1 = Pi * (v1 - 0.5F);
        for (int seg = 0; seg < segments; ++seg) {
            const float u0 = 2.0F * Pi * seg       / segments;
            const float u1 = 2.0F * Pi * (seg + 1) / segments;
            const auto vtx = [](float ph, float u) {
                const float cp = std::cos(ph);
                return Vertex{cp*std::cos(u), std::sin(ph), cp*std::sin(u),
                              cp*std::cos(u), std::sin(ph), cp*std::sin(u)};
            };
            const Vertex a=vtx(p0,u0), b=vtx(p1,u0), c=vtx(p1,u1), d=vtx(p0,u1);
            sphereVertices_.insert(sphereVertices_.end(), {a,b,c, a,c,d});
        }
    }
    sphereVao_.create();
    sphereVao_.bind();
    sphereVbo_.create();
    sphereVbo_.bind();
    sphereVbo_.allocate(sphereVertices_.data(),
                        static_cast<int>(sphereVertices_.size() * sizeof(Vertex)));
    shader_.bind();
    shader_.enableAttributeArray("position");
    shader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, sizeof(Vertex));
    shader_.enableAttributeArray("normal");
    shader_.setAttributeBuffer("normal",   GL_FLOAT, 3*sizeof(float), 3, sizeof(Vertex));
    shader_.release();
    sphereVbo_.release();
    sphereVao_.release();
}

void UniverseView::resizeGL(int w, int h) { glViewport(0, 0, w, h); }

// ─── View matrix ─────────────────────────────────────────────────────────────

QMatrix4x4 UniverseView::viewMatrix() const {
    QMatrix4x4 view;
    const float cd = distanceAu_;
    const QVector3D eye(
        cd * std::cos(pitch_) * std::sin(yaw_),
        cd * std::sin(pitch_),
        cd * std::cos(pitch_) * std::cos(yaw_));
    view.lookAt(eye, {0,0,0}, {0,1,0});
    return view;
}

// ─── Orbit color: blue (circular) → red (high eccentricity) ──────────────────

QColor UniverseView::orbitColorForEcc(double ecc, bool hypothetical) {
    if (hypothetical) return QColor(210, 100, 245, 220);
    const double t = std::clamp(ecc / 0.95, 0.0, 1.0);
    // blue (95,155,225) → yellow → red
    int r, g, b;
    if (t < 0.5) {
        const double u = t * 2.0;
        r = static_cast<int>(95  + 155 * u);
        g = static_cast<int>(155 + 90  * u);
        b = static_cast<int>(225 - 100 * u);
    } else {
        const double u = (t - 0.5) * 2.0;
        r = static_cast<int>(250);
        g = static_cast<int>(245 - 200 * u);
        b = static_cast<int>(125 - 100 * u);
    }
    return QColor(r, g, b, 175);
}

// ─── drawSphere ───────────────────────────────────────────────────────────────

void UniverseView::drawSphere(const physics::Body& body, const QMatrix4x4& view,
                               const QMatrix4x4& projection) {
    const auto position = (body.positionM - focusPosition_) / AU;
    float displayRadius = 0.018F;
    if (body.name == "Sun")          displayRadius = 0.10F;
    else if (body.hypothetical)      displayRadius = 0.055F;
    else if (body.name == "Jupiter") displayRadius = 0.050F;
    else if (body.name == "Saturn")  displayRadius = 0.043F;
    else if (body.name == "Uranus" || body.name == "Neptune") displayRadius = 0.036F;
    else if (body.name == "Earth" || body.name == "Venus")    displayRadius = 0.027F;
    else if (body.name == "Mars")    displayRadius = 0.022F;
    else if (body.name == "Mercury") displayRadius = 0.018F;
    else if (body.name.find("Goblin") != std::string::npos ||
             body.name.find("Sedna")  != std::string::npos ||
             body.name.find("VP113")  != std::string::npos ||
             body.name.find("KV42")   != std::string::npos) displayRadius = 0.020F;

    QMatrix4x4 model;
    model.translate(static_cast<float>(position.x()),
                    static_cast<float>(position.z()),
                    static_cast<float>(position.y()));
    model.scale(displayRadius);

    QColor color(145, 175, 220);
    if (body.hypothetical)       color = QColor(10, 10, 10);       // near-black PBH
    else if (body.name == "Sun") color = QColor(255, 190, 45);
    else if (body.name == "Mercury") color = QColor(170, 165, 155);
    else if (body.name == "Venus")   color = QColor(235, 190, 110);
    else if (body.name == "Earth")   color = QColor(75,  145, 235);
    else if (body.name == "Mars")    color = QColor(210, 95,  65);
    else if (body.name == "Jupiter") color = QColor(205, 155, 110);
    else if (body.name == "Saturn")  color = QColor(220, 190, 125);
    else if (body.name == "Uranus")  color = QColor(110, 210, 220);
    else if (body.name == "Neptune") color = QColor(70,  105, 225);
    else if (body.name.find("Drac") != std::string::npos) color = QColor(235, 125, 90);
    else if (body.observed) color = QColor(120, 185, 240);

    shader_.bind();
    shader_.setUniformValue("model",         model);
    shader_.setUniformValue("view",          view);
    shader_.setUniformValue("projection",    projection);
    shader_.setUniformValue("bodyColor",     QVector4D(color.redF(), color.greenF(),
                                                        color.blueF(), 1.0F));
    shader_.setUniformValue("lightPosition", QVector3D(0, 0, 0));
    sphereVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sphereVertices_.size()));
    sphereVao_.release();
    shader_.release();

    // Draw PBH halo after the black sphere
    if (body.hypothetical) drawPbhHalo(body, view, projection);
}

// ─── Saturn rings ─────────────────────────────────────────────────────────────

void UniverseView::drawSaturnRings(const physics::Body& body, const QMatrix4x4& view,
                                   const QMatrix4x4& projection) {
    // Saturn ring disk: inner ~1.11 R_S → outer ~2.27 R_S
    // We render as a flat quad ring (GL_LINES circle strips) in the equatorial plane
    // Saturn's axial tilt ~26.7° relative to ecliptic, simplified here
    const auto pos = (body.positionM - focusPosition_) / AU;
    const float displayR = 0.043F;
    constexpr int N = 48;
    constexpr float innerFrac = 1.4F;
    constexpr float outerFrac = 2.4F;

    orbitVao_.bind();
    orbitVbo_.bind();
    shader_.bind();
    QMatrix4x4 identity;
    shader_.setUniformValue("model",         identity);
    shader_.setUniformValue("view",          view);
    shader_.setUniformValue("projection",    projection);
    shader_.setUniformValue("lightPosition", QVector3D(0,0,0));
    shader_.disableAttributeArray("normal");
    shader_.enableAttributeArray("position");

    glLineWidth(1.5F);
    shader_.setUniformValue("bodyColor", QVector4D(0.78F, 0.72F, 0.55F, 0.50F));

    // Draw 3 clean rings
    const std::array<float, 3> fractions = {innerFrac, (innerFrac + outerFrac) * 0.5F, outerFrac};
    for (float fr : fractions) {
        std::vector<float> ring;
        ring.reserve((N + 1) * 3);
        for (int i = 0; i <= N; ++i) {
            const float ang = 2.0F * Pi * static_cast<float>(i) / N;
            ring.insert(ring.end(), {
                static_cast<float>(pos.x()) + std::cos(ang) * displayR * fr,
                static_cast<float>(pos.z()),
                static_cast<float>(pos.y()) + std::sin(ang) * displayR * fr,
            });
        }
        orbitVbo_.allocate(ring.data(), static_cast<int>(ring.size() * sizeof(float)));
        shader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, 3 * sizeof(float));
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(ring.size() / 3));
    }
    shader_.release();
    orbitVbo_.release();
    orbitVao_.release();
    glLineWidth(1.0F);
}

// ─── PBH halo (magenta ring) ──────────────────────────────────────────────────

void UniverseView::drawPbhHalo(const physics::Body& body, const QMatrix4x4& view,
                                const QMatrix4x4& projection) {
    const auto pos = (body.positionM - focusPosition_) / AU;
    constexpr int N = 36;
    constexpr float haloR = 0.085F;

    std::vector<float> ring;
    ring.reserve((N + 1) * 3);
    for (int i = 0; i <= N; ++i) {
        const float ang = 2.0F * Pi * static_cast<float>(i) / N;
        ring.insert(ring.end(), {
            static_cast<float>(pos.x()) + std::cos(ang) * haloR,
            static_cast<float>(pos.z()),
            static_cast<float>(pos.y()) + std::sin(ang) * haloR,
        });
    }

    orbitVao_.bind();
    orbitVbo_.bind();
    orbitVbo_.allocate(ring.data(), static_cast<int>(ring.size() * sizeof(float)));
    shader_.bind();
    QMatrix4x4 identity;
    shader_.setUniformValue("model",         identity);
    shader_.setUniformValue("view",          view);
    shader_.setUniformValue("projection",    projection);
    shader_.setUniformValue("lightPosition", QVector3D(0,0,0));
    shader_.setUniformValue("bodyColor",     QVector4D(0.85F, 0.35F, 0.95F, 0.55F));
    shader_.disableAttributeArray("normal");
    shader_.enableAttributeArray("position");
    shader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, 3 * sizeof(float));
    glLineWidth(2.0F);
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(ring.size() / 3));
    shader_.release();
    orbitVbo_.release();
    orbitVao_.release();
    glLineWidth(1.0F);
}

// ─── drawOrbit ────────────────────────────────────────────────────────────────

void UniverseView::drawOrbit(const physics::OrbitalElements& elements,
                             const QColor& color, const QMatrix4x4& view,
                             const QMatrix4x4& projection) {
    if (!std::isfinite(elements.semiMajorAxisM) || elements.semiMajorAxisM <= 0.0 ||
        elements.eccentricity >= 1.0) return;

    std::vector<float> vertices;
    constexpr int samples = 96;
    const double semiLatus = elements.semiMajorAxisM *
        (1.0 - elements.eccentricity * elements.eccentricity);
    const Eigen::AngleAxisd Omega(elements.longitudeOfAscendingNodeRad, physics::Vector3::UnitZ());
    const Eigen::AngleAxisd inc  (elements.inclinationRad,              physics::Vector3::UnitX());
    const Eigen::AngleAxisd omega(elements.argumentOfPeriapsisRad,      physics::Vector3::UnitZ());
    const auto transform = Omega * inc * omega;

    for (int s = 0; s <= samples; ++s) {
        const double anomaly = 2.0 * std::numbers::pi * s / samples;
        const double radius  = semiLatus / (1.0 + elements.eccentricity * std::cos(anomaly));
        const physics::Vector3 local{radius*std::cos(anomaly), radius*std::sin(anomaly), 0};
        const auto point = (transform * local - focusPosition_) / AU;
        vertices.insert(vertices.end(), {static_cast<float>(point.x()),
            static_cast<float>(point.z()), static_cast<float>(point.y())});
    }

    orbitVao_.bind();
    orbitVbo_.bind();
    orbitVbo_.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
    shader_.bind();
    QMatrix4x4 identity;
    shader_.setUniformValue("model",         identity);
    shader_.setUniformValue("view",          view);
    shader_.setUniformValue("projection",    projection);
    shader_.setUniformValue("lightPosition", QVector3D(0,0,0));
    shader_.disableAttributeArray("normal");
    shader_.enableAttributeArray("position");
    shader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, 3 * sizeof(float));

    glLineWidth(1.5F);
    shader_.setUniformValue("bodyColor",
        QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF()));
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(vertices.size() / 3));

    shader_.release();
    orbitVbo_.release();
    orbitVao_.release();
    glLineWidth(1.0F);
}

// ─── drawReferenceField (gravitational well grid) ─────────────────────────────

void UniverseView::drawReferenceField(const QMatrix4x4& view,
                                      const QMatrix4x4& projection) {
    if (!simulation_) return;

    const double extent      = std::clamp(static_cast<double>(potentialRadiusAu_), 0.1, 10000.0);
    const double maxDepth    = extent * 0.15;
    const double cutoffRadius = extent * 1.25;

    struct Well { double x, y, z, massKg, radiusAu, weight; };
    std::vector<Well> wells;

    if (potentialSource_ >= 0) {
        if (potentialSource_ < static_cast<int>(simulation_->bodies().size())) {
            const auto& body = simulation_->bodies()[static_cast<std::size_t>(potentialSource_)];
            const physics::Vector3 rel = body.positionM - focusPosition_;
            wells.push_back({rel.x()/AU, rel.y()/AU, rel.z()/AU,
                             body.massKg, std::max(body.radiusM/AU, 1.0e-5), 1.0});
        }
    } else {
        double refMass = 1.0;
        if (focusTarget_ >= 0 && focusTarget_ < static_cast<int>(simulation_->bodies().size())) {
            const auto& fb = simulation_->bodies()[static_cast<std::size_t>(focusTarget_)];
            if (fb.massive && fb.massKg > 0.0) refMass = fb.massKg;
        }
        if (refMass <= 1.0)
            for (const auto& b : simulation_->bodies())
                if (b.massive && b.massKg > refMass) refMass = b.massKg;

        for (const auto& body : simulation_->bodies()) {
            if (!body.massive || body.massKg <= 0.0) continue;
            const physics::Vector3 rel = body.positionM - focusPosition_;
            const double distPlane = std::hypot(rel.x(), rel.y()) / AU;
            if (distPlane > extent * 2.5 && body.massKg < refMass * 0.1) continue;
            const double weight = std::clamp(std::cbrt(body.massKg / refMass), 0.01, 1.0);
            wells.push_back({rel.x()/AU, rel.y()/AU, rel.z()/AU,
                             body.massKg, std::max(body.radiusM/AU, 1.0e-5), weight});
        }
    }
    if (wells.empty()) return;

    const auto depthAt = [&](double xAu, double yAu) {
        double total = 0.0;
        for (const auto& w : wells) {
            const double dx = xAu - w.x, dy = yAu - w.y, dz = w.z;
            const double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist >= cutoffRadius) continue;
            const double soft   = std::max(w.radiusAu, extent * 0.035);
            const double invD   = 1.0 / std::sqrt(dist*dist + soft*soft);
            const double invCut = 1.0 / std::sqrt(cutoffRadius*cutoffRadius + soft*soft);
            const double shape  = (invD - invCut) / (1.0/soft - invCut);
            const double u      = dist / cutoffRadius;
            const double taper  = (1.0 - u*u) * (1.0 - u*u);
            total += maxDepth * w.weight * shape * taper;
        }
        return maxDepth * std::tanh(total / maxDepth);
    };

    const auto colorForDepth = [&](double depth) {
        const float amount = static_cast<float>(std::clamp(depth / maxDepth, 0.0, 1.0));
        float r, g, b;
        if (amount < 0.35F) {
            const float t = amount / 0.35F;
            r = 0.10F + 0.15F*t; g = 0.50F + 0.45F*t; b = 0.95F - 0.25F*t;
        } else if (amount < 0.70F) {
            const float t = (amount - 0.35F) / 0.35F;
            r = 0.25F + 0.70F*t; g = 0.95F - 0.15F*t; b = 0.70F - 0.55F*t;
        } else {
            const float t = (amount - 0.70F) / 0.30F;
            r = 0.95F + 0.05F*t; g = 0.80F - 0.55F*t; b = 0.15F - 0.10F*t;
        }
        const float alpha = potentialOpacity_ * (0.35F + 0.65F * amount);
        return std::array<float,4>{r, g, b, alpha};
    };

    constexpr int RES = 35; // 35x35 = 1225 nodes (4.6x faster than 75x75, ideal for iGPUs)
    std::vector<FieldVertex> gridNodes(RES * RES);

    for (int j = 0; j < RES; ++j) {
        const double x = (-1.0 + 2.0*j / (RES-1)) * extent;
        for (int k = 0; k < RES; ++k) {
            const double y = (-1.0 + 2.0*k / (RES-1)) * extent;
            const double depth = depthAt(x, y);
            const auto col = colorForDepth(depth);
            gridNodes[j*RES+k] = FieldVertex{
                static_cast<float>(x), static_cast<float>(-depth), static_cast<float>(y),
                col[0], col[1], col[2], col[3]};
        }
    }

    std::vector<FieldVertex> vertices;
    vertices.reserve(RES*(RES-1)*4 + 8);
    for (int j = 0; j < RES; ++j)
        for (int k = 0; k < RES-1; ++k) {
            vertices.push_back(gridNodes[j*RES+k]);
            vertices.push_back(gridNodes[j*RES+(k+1)]);
        }
    for (int k = 0; k < RES; ++k)
        for (int j = 0; j < RES-1; ++j) {
            vertices.push_back(gridNodes[j*RES+k]);
            vertices.push_back(gridNodes[(j+1)*RES+k]);
        }

    // Border
    const float bA = potentialOpacity_ * 0.75F;
    const std::array<float,4> bc{0.18F, 0.65F, 1.0F, bA};
    const float extF = static_cast<float>(extent);
    const FieldVertex c0{-extF,0,- extF,bc[0],bc[1],bc[2],bc[3]};
    const FieldVertex c1{ extF,0,-extF,bc[0],bc[1],bc[2],bc[3]};
    const FieldVertex c2{ extF,0, extF,bc[0],bc[1],bc[2],bc[3]};
    const FieldVertex c3{-extF,0, extF,bc[0],bc[1],bc[2],bc[3]};
    vertices.insert(vertices.end(),{c0,c1,c1,c2,c2,c3,c3,c0});

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glLineWidth(1.5F);
    fieldVao_.bind(); fieldVbo_.bind();
    fieldVbo_.allocate(vertices.data(), static_cast<int>(vertices.size()*sizeof(FieldVertex)));
    fieldShader_.bind();
    fieldShader_.setUniformValue("view",       view);
    fieldShader_.setUniformValue("projection", projection);
    fieldShader_.enableAttributeArray("position");
    fieldShader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, sizeof(FieldVertex));
    fieldShader_.enableAttributeArray("color");
    fieldShader_.setAttributeBuffer("color", GL_FLOAT, 3*sizeof(float), 4, sizeof(FieldVertex));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    fieldShader_.release();
    fieldVbo_.release();
    fieldVao_.release();
    glLineWidth(1.0F);
    glDepthMask(GL_TRUE);
}

// ─── Star field (screen-space QPainter dots) ──────────────────────────────────

void UniverseView::drawStarField(QPainter& painter) {
    if (!starsSeeded_) {
        starsSeeded_ = true;
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> distF(0.0F, 1.0F);
        stars_.resize(220);
        for (auto& s : stars_) {
            s.x = distF(rng);
            s.y = distF(rng);
            s.brightness = 0.4F + 0.6F * distF(rng);
        }
    }

    std::vector<QPoint> dimPts;
    std::vector<QPoint> brightPts;
    dimPts.reserve(220);
    brightPts.reserve(60);

    const float w = static_cast<float>(width());
    const float h = static_cast<float>(height());
    const float yawShift = yaw_ * 0.03F / (2.0F * Pi) + 1.0F;

    for (const auto& s : stars_) {
        const float px = std::fmod(s.x + yawShift, 1.0F) * w;
        const float py = s.y * h;
        if (s.brightness > 0.8F) {
            brightPts.emplace_back(static_cast<int>(px), static_cast<int>(py));
        } else {
            dimPts.emplace_back(static_cast<int>(px), static_cast<int>(py));
        }
    }

    painter.save();
    painter.setPen(QPen(QColor(160, 180, 210, 180), 1));
    if (!dimPts.empty()) {
        painter.drawPoints(dimPts.data(), static_cast<int>(dimPts.size()));
    }
    painter.setPen(QPen(QColor(230, 240, 255, 240), 2));
    if (!brightPts.empty()) {
        painter.drawPoints(brightPts.data(), static_cast<int>(brightPts.size()));
    }
    painter.restore();
}

// ─── Axes indicator (mini compass) ────────────────────────────────────────────

void UniverseView::drawAxesIndicator(QPainter& painter) {
    // Draw a small XYZ compass in the bottom-left corner
    const int cx = 36, cy = height() - 36;
    const float scale = 22.0F;

    // Build a simple rotation: project 3D axis tips to 2D
    // Use yaw and pitch from the camera
    const float cPitch = std::cos(pitch_), sPitch = std::sin(pitch_);
    const float cYaw   = std::cos(yaw_),   sYaw   = std::sin(yaw_);

    // Project a 3D unit vector to screen-space (simplified view transform)
    const auto proj = [&](float ax, float ay, float az) -> QPointF {
        // Apply yaw rotation around Y, then pitch rotation around X
        const float xr = ax * cYaw - az * sYaw;
        const float yr = ax * sPitch * sYaw + ay * cPitch + az * sPitch * cYaw;
        const float zr = ax * cPitch * sYaw - ay * sPitch + az * cPitch * cYaw;
        (void)zr;
        return QPointF(cx + xr * scale, cy - yr * scale);
    };

    struct Axis { float x, y, z; const char* label; QColor color; };
    static const std::array<Axis, 3> axes = {{
        {1,0,0, "X", QColor(255,80,80)},
        {0,1,0, "Z", QColor(80,255,80)},  // sim-Z is ecliptic north
        {0,0,1, "Y", QColor(80,120,255)},
    }};

    painter.save();
    QFont f = painter.font(); f.setPointSize(7); f.setBold(true); painter.setFont(f);
    for (const auto& ax : axes) {
        const QPointF tip = proj(ax.x, ax.y, ax.z);
        painter.setPen(QPen(ax.color, 1.5));
        painter.drawLine(QPointF(cx, cy), tip);
        painter.setPen(ax.color.lighter(130));
        painter.drawText(tip + QPointF(3, 3), ax.label);
    }
    // Circle origin
    painter.setPen(QColor(180,200,230,160));
    painter.setBrush(QColor(10,15,30,200));
    painter.drawEllipse(QPoint(cx, cy), 3, 3);
    painter.restore();
}

// ─── paintGL ──────────────────────────────────────────────────────────────────

void UniverseView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!simulation_) return;

    QMatrix4x4 projection;
    projection.perspective(45.0F, static_cast<float>(width()) / std::max(1, height()),
                           0.01F, 100000.0F);
    const QMatrix4x4 view = viewMatrix();

    // Orbits (colored by eccentricity)
    if (trajectoriesVisible_) {
        for (std::size_t i = 1; i < simulation_->bodies().size(); ++i) {
            const auto& body = simulation_->bodies()[i];
            try {
                const auto el    = simulation_->orbitalElements(i);
                const QColor col = orbitColorForEcc(el.eccentricity, body.hypothetical);
                drawOrbit(el, col, view, projection);
            } catch (...) {}
        }
    }

    // Bodies + Saturn rings
    for (const auto& body : simulation_->bodies()) {
        drawSphere(body, view, projection);
        if (body.name == "Saturn") drawSaturnRings(body, view, projection);
    }

    if (fieldVisible_) drawReferenceField(view, projection);

    // ── 2D overlay (QPainter) ─────────────────────────────────────────────────
    QPainter labels(this);
    labels.setRenderHint(QPainter::Antialiasing);

    // Star field (before body labels so stars are behind)
    if (starsVisible_) drawStarField(labels);

    // Body labels
    for (std::size_t i = 0; i < simulation_->bodies().size(); ++i) {
        const auto& body = simulation_->bodies()[i];
        const QPointF point = projectForPicking(body.positionM);
        const bool selected = static_cast<int>(i) == selectedBody_;
        labels.setPen(body.hypothetical ? QColor(210,100,245)
                     : (selected ? Qt::white : QColor(215,225,240,220)));
        QFont font = labels.font();
        font.setPointSize(selected ? 11 : 9);
        font.setBold(selected || body.hypothetical);
        labels.setFont(font);
        labels.drawText(point + QPointF(9, -9), QString::fromStdString(body.name));
    }

    drawAxesIndicator(labels);
    labels.end();
}

// ─── tick ─────────────────────────────────────────────────────────────────────

void UniverseView::tick() {
    if (running_ && simulation_) simulation_->advance(timeStepS_ * speedMultiplier_);
    if (simulation_ && focusTarget_ >= 0 &&
        focusTarget_ < static_cast<int>(simulation_->bodies().size())) {
        const auto target = simulation_->bodies()[static_cast<std::size_t>(focusTarget_)].positionM;
        focusPosition_ += 0.12 * (target - focusPosition_);
    }
    update();
}

// ─── Picking / projection ─────────────────────────────────────────────────────

QPointF UniverseView::projectForPicking(const physics::Vector3& position) const {
    QMatrix4x4 projection;
    projection.perspective(45.0F, static_cast<float>(width()) / std::max(1, height()),
                           0.01F, 100000.0F);
    const auto relative = (position - focusPosition_) / AU;
    const QVector4D world(static_cast<float>(relative.x()),
                          static_cast<float>(relative.z()),
                          static_cast<float>(relative.y()), 1.0F);
    const QVector4D clip = projection * viewMatrix() * world;
    if (clip.w() <= 0.0F) return {-10000.0, -10000.0};
    const QVector3D ndc = clip.toVector3D() / clip.w();
    return QPointF((ndc.x() + 1.0F) * 0.5F * width(),
                   (1.0F - ndc.y()) * 0.5F * height());
}

// ─── Mouse / keyboard ─────────────────────────────────────────────────────────

void UniverseView::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->pos();
    setFocus(Qt::MouseFocusReason);
    if (!simulation_) return;
    double closest = 22.0;
    selectedBody_ = -1;
    for (std::size_t i = 0; i < simulation_->bodies().size(); ++i) {
        const double d = QLineF(event->position(),
            projectForPicking(simulation_->bodies()[i].positionM)).length();
        if (d < closest) { closest = d; selectedBody_ = static_cast<int>(i); }
    }
    if (selectedBody_ >= 0) emit bodySelected(selectedBody_);
    update();
}

void UniverseView::mouseDoubleClickEvent(QMouseEvent* event) {
    mousePressEvent(event);
    if (selectedBody_ >= 0) focusBody(selectedBody_);
    update();
}

void UniverseView::focusBody(int index) {
    if (!simulation_ || index < 0 ||
        index >= static_cast<int>(simulation_->bodies().size())) return;
    selectedBody_ = index;
    focusTarget_  = index;
    focusPosition_ = simulation_->bodies()[static_cast<std::size_t>(index)].positionM;
    distanceAu_ = 8.0F;
    emit bodySelected(index);
    update();
}

void UniverseView::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        yaw_   += static_cast<float>((event->position().x() - lastMouse_.x()) * 0.01);
        pitch_  = std::clamp(pitch_ + static_cast<float>(
            (event->position().y() - lastMouse_.y()) * 0.01), -1.5F, 1.5F);
        lastMouse_ = event->pos();
        update();
    }
}

void UniverseView::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0)
        distanceAu_ = std::clamp(distanceAu_ * std::pow(0.82F, static_cast<float>(steps)),
                                  0.05F, 100000.0F);
    event->accept();
    update();
}

void UniverseView::keyPressEvent(QKeyEvent* event) {
    if      (event->key() == Qt::Key_Escape) returnToSystemView();
    else if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
        distanceAu_ = std::max(0.05F, distanceAu_ * 0.82F);
    else if (event->key() == Qt::Key_Minus)
        distanceAu_ = std::min(100000.0F, distanceAu_ * 1.22F);
    else QOpenGLWidget::keyPressEvent(event);
    update();
}

} // namespace erebus::render

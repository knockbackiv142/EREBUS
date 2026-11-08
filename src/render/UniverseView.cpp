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

namespace erebus::render {
namespace {
constexpr double AU = 1.495978707e11;
constexpr float Pi = static_cast<float>(std::numbers::pi);
}

UniverseView::UniverseView(QWidget* parent) : QOpenGLWidget(parent) {
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &UniverseView::tick);
    timer_.start();
    setFocusPolicy(Qt::StrongFocus);
}

void UniverseView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.002F, 0.004F, 0.012F, 1.0F);
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
            vec3 light = normalize(lightPosition - worldPosition);
            float diffuse = max(dot(normal, light), 0.0);
            float ambient = 0.18;
            gl_FragColor = vec4(bodyColor.rgb * (ambient + diffuse * 0.82), bodyColor.a);
        })");
    if (!shader_.link())
        qWarning() << "Erebus OpenGL shader link failed:" << shader_.log();
    createSphereMesh();
    orbitVao_.create();
    orbitVbo_.create();
    fieldVao_.create();
    fieldVbo_.create();
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
        void main() {
            gl_FragColor = vertexColor;
        })");
    if (!fieldShader_.link())
        qWarning() << "Erebus field shader link failed:" << fieldShader_.log();
}

void UniverseView::createSphereMesh() {
    constexpr int rings = 20;
    constexpr int segments = 32;
    for (int ring = 0; ring < rings; ++ring) {
        const float v0 = static_cast<float>(ring) / rings;
        const float v1 = static_cast<float>(ring + 1) / rings;
        const float p0 = Pi * (v0 - 0.5F);
        const float p1 = Pi * (v1 - 0.5F);
        for (int segment = 0; segment < segments; ++segment) {
            const float u0 = 2.0F * Pi * segment / segments;
            const float u1 = 2.0F * Pi * (segment + 1) / segments;
            const auto vertex = [](float p, float u) {
                const float cp = std::cos(p);
                return Vertex{cp * std::cos(u), std::sin(p), cp * std::sin(u),
                              cp * std::cos(u), std::sin(p), cp * std::sin(u)};
            };
            const Vertex a = vertex(p0, u0);
            const Vertex b = vertex(p1, u0);
            const Vertex c = vertex(p1, u1);
            const Vertex d = vertex(p0, u1);
            sphereVertices_.insert(sphereVertices_.end(), {a, b, c, a, c, d});
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
    shader_.setAttributeBuffer("normal", GL_FLOAT, 3 * sizeof(float), 3, sizeof(Vertex));
    shader_.release();
    sphereVbo_.release();
    sphereVao_.release();
}

void UniverseView::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
}

QMatrix4x4 UniverseView::viewMatrix() const {
    QMatrix4x4 view;
    const QVector3D target(0.0F, 0.0F, 0.0F);
    const float cameraDistance = distanceAu_;
    const QVector3D eye = target + QVector3D(
        cameraDistance * std::cos(pitch_) * std::sin(yaw_),
        cameraDistance * std::sin(pitch_),
        cameraDistance * std::cos(pitch_) * std::cos(yaw_));
    view.lookAt(eye, target, QVector3D(0, 1, 0));
    return view;
}

void UniverseView::drawSphere(const physics::Body& body, const QMatrix4x4& view,
                              const QMatrix4x4& projection) {
    const auto position = (body.positionM - focusPosition_) / AU;
    float displayRadius = 0.018F;
    if (body.name == "Sun") displayRadius = 0.10F;
    else if (body.hypothetical) displayRadius = 0.055F;
    else if (body.name == "Jupiter") displayRadius = 0.050F;
    else if (body.name == "Saturn") displayRadius = 0.043F;
    else if (body.name == "Uranus" || body.name == "Neptune") displayRadius = 0.036F;
    else if (body.name == "Earth" || body.name == "Venus") displayRadius = 0.027F;
    else if (body.name == "Mars") displayRadius = 0.022F;
    else if (body.name == "Mercury") displayRadius = 0.018F;
    else if (body.name.find("Goblin") != std::string::npos ||
             body.name.find("Sedna") != std::string::npos ||
             body.name.find("VP113") != std::string::npos ||
             body.name.find("KV42") != std::string::npos) displayRadius = 0.020F;
    QMatrix4x4 model;
    model.translate(static_cast<float>(position.x()),
                    static_cast<float>(position.z()),
                    static_cast<float>(position.y()));
    model.scale(displayRadius);
    QColor color(145, 175, 220);
    if (body.hypothetical) color = QColor(205, 90, 245);
    else if (body.name == "Sun") color = QColor(255, 190, 45);
    else if (body.name == "Mercury") color = QColor(170, 165, 155);
    else if (body.name == "Venus") color = QColor(235, 190, 110);
    else if (body.name == "Earth") color = QColor(75, 145, 235);
    else if (body.name == "Mars") color = QColor(210, 95, 65);
    else if (body.name == "Jupiter") color = QColor(205, 155, 110);
    else if (body.name == "Saturn") color = QColor(220, 190, 125);
    else if (body.name == "Uranus") color = QColor(110, 210, 220);
    else if (body.name == "Neptune") color = QColor(70, 105, 225);
    else if (body.name.find("Drac") != std::string::npos) color = QColor(235, 125, 90);
    else if (body.observed) color = QColor(120, 185, 240);
    shader_.bind();
    shader_.setUniformValue("model", model);
    shader_.setUniformValue("view", view);
    shader_.setUniformValue("projection", projection);
    shader_.setUniformValue("bodyColor", QVector4D(color.redF(), color.greenF(),
                                                     color.blueF(), 1.0F));
    shader_.setUniformValue("lightPosition", QVector3D(0, 0, 0));
    sphereVao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sphereVertices_.size()));
    sphereVao_.release();
    shader_.release();
}

void UniverseView::drawOrbit(const physics::OrbitalElements& elements,
                             const QColor& color, const QMatrix4x4& view,
                             const QMatrix4x4& projection) {
    if (!std::isfinite(elements.semiMajorAxisM) || elements.semiMajorAxisM <= 0.0 ||
        elements.eccentricity >= 1.0) return;
    std::vector<float> vertices;
    constexpr int samples = 256;
    const double semiLatus = elements.semiMajorAxisM *
        (1.0 - elements.eccentricity * elements.eccentricity);
    const Eigen::AngleAxisd rotation(elements.longitudeOfAscendingNodeRad,
                                      physics::Vector3::UnitZ());
    const Eigen::AngleAxisd inclination(elements.inclinationRad,
                                        physics::Vector3::UnitX());
    const Eigen::AngleAxisd argument(elements.argumentOfPeriapsisRad,
                                     physics::Vector3::UnitZ());
    const auto transform = rotation * inclination * argument;
    for (int sample = 0; sample <= samples; ++sample) {
        const double anomaly = 2.0 * std::numbers::pi * sample / samples;
        const double radius = semiLatus / (1.0 + elements.eccentricity * std::cos(anomaly));
        const physics::Vector3 local{radius * std::cos(anomaly), radius * std::sin(anomaly), 0};
        const auto point = (transform * local - focusPosition_) / AU;
        vertices.insert(vertices.end(), {static_cast<float>(point.x()),
            static_cast<float>(point.z()), static_cast<float>(point.y())});
    }
    orbitVao_.bind();
    orbitVbo_.bind();
    orbitVbo_.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(float)));
    shader_.bind();
    QMatrix4x4 model;
    shader_.setUniformValue("model", model);
    shader_.setUniformValue("view", view);
    shader_.setUniformValue("projection", projection);
    shader_.setUniformValue("bodyColor", QVector4D(color.redF(), color.greenF(),
                                                     color.blueF(), color.alphaF()));
    shader_.setUniformValue("lightPosition", QVector3D(0, 0, 0));
    shader_.disableAttributeArray("normal");
    shader_.enableAttributeArray("position");
    shader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, 3 * sizeof(float));
    glLineWidth(5.0F);
    shader_.setUniformValue("bodyColor", QVector4D(color.redF(), color.greenF(),
                                                     color.blueF(), 0.10F));
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(vertices.size() / 3));
    glLineWidth(2.0F);
    shader_.setUniformValue("bodyColor", QVector4D(color.redF(), color.greenF(),
                                                     color.blueF(), color.alphaF()));
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(vertices.size() / 3));
    shader_.release();
    orbitVbo_.release();
    orbitVao_.release();
}

void UniverseView::drawReferenceField(const QMatrix4x4& view,
                                      const QMatrix4x4& projection) {
    if (!simulation_) return;

    const double extent = std::clamp(static_cast<double>(potentialRadiusAu_), 0.1, 10000.0);
    const double maxDepth = extent * 0.15;
    const double cutoffRadius = extent * 1.25;

    struct Well {
        double x;
        double y;
        double z;
        double massKg;
        double radiusAu;
        double weight;
    };

    std::vector<Well> wells;

    if (potentialSource_ >= 0) {
        if (potentialSource_ < static_cast<int>(simulation_->bodies().size())) {
            const auto& body = simulation_->bodies()[static_cast<std::size_t>(potentialSource_)];
            const physics::Vector3 rel = body.positionM - focusPosition_;
            wells.push_back({
                rel.x() / AU,
                rel.y() / AU,
                rel.z() / AU,
                body.massKg,
                std::max(body.radiusM / AU, 1.0e-5),
                1.0
            });
        }
    } else {
        // Combined potential of all massive bodies
        double refMass = 1.0;
        if (focusTarget_ >= 0 && focusTarget_ < static_cast<int>(simulation_->bodies().size())) {
            const auto& focusBody = simulation_->bodies()[static_cast<std::size_t>(focusTarget_)];
            if (focusBody.massive && focusBody.massKg > 0.0) {
                refMass = focusBody.massKg;
            }
        }
        if (refMass <= 1.0) {
            for (const auto& body : simulation_->bodies()) {
                if (body.massive && body.massKg > refMass) {
                    refMass = body.massKg;
                }
            }
        }

        for (std::size_t index = 0; index < simulation_->bodies().size(); ++index) {
            const auto& body = simulation_->bodies()[index];
            if (!body.massive || body.massKg <= 0.0) continue;
            const physics::Vector3 rel = body.positionM - focusPosition_;
            const double distPlane = std::hypot(rel.x(), rel.y()) / AU;
            if (distPlane > extent * 2.5 && body.massKg < refMass * 0.1) continue;

            const double weight = std::clamp(std::cbrt(body.massKg / refMass), 0.01, 1.0);
            wells.push_back({
                rel.x() / AU,
                rel.y() / AU,
                rel.z() / AU,
                body.massKg,
                std::max(body.radiusM / AU, 1.0e-5),
                weight
            });
        }
    }

    if (wells.empty()) return;

    static int diagnosticsRemaining = 3;
    if (diagnosticsRemaining > 0) {
        qWarning() << "Erebus gravitational field active - wells:" << static_cast<int>(wells.size())
                   << "extent AU:" << extent
                   << "max depth AU:" << maxDepth;
        --diagnosticsRemaining;
    }

    const auto depthAt = [&](double xAu, double yAu) {
        double totalWellDepth = 0.0;
        for (const auto& well : wells) {
            const double dx = xAu - well.x;
            const double dy = yAu - well.y;
            const double dz = well.z;
            const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist >= cutoffRadius) continue;

            const double softening = std::max(well.radiusAu, extent * 0.035);
            const double invDist = 1.0 / std::sqrt(dist * dist + softening * softening);
            const double invCut = 1.0 / std::sqrt(cutoffRadius * cutoffRadius + softening * softening);
            const double shape = (invDist - invCut) / (1.0 / softening - invCut);

            const double u = dist / cutoffRadius;
            const double taper = (1.0 - u * u) * (1.0 - u * u);

            totalWellDepth += maxDepth * well.weight * shape * taper;
        }
        return maxDepth * std::tanh(totalWellDepth / maxDepth);
    };

    const auto colorForDepth = [&](double depth) {
        const float amount = static_cast<float>(
            std::clamp(depth / maxDepth, 0.0, 1.0));
        float r, g, b;
        if (amount < 0.35F) {
            const float t = amount / 0.35F;
            r = 0.10F + 0.15F * t;
            g = 0.50F + 0.45F * t;
            b = 0.95F - 0.25F * t;
        } else if (amount < 0.70F) {
            const float t = (amount - 0.35F) / 0.35F;
            r = 0.25F + 0.70F * t;
            g = 0.95F - 0.15F * t;
            b = 0.70F - 0.55F * t;
        } else {
            const float t = (amount - 0.70F) / 0.30F;
            r = 0.95F + 0.05F * t;
            g = 0.80F - 0.55F * t;
            b = 0.15F - 0.10F * t;
        }
        const float alpha = potentialOpacity_ * (0.35F + 0.65F * amount);
        return std::array<float, 4>{r, g, b, alpha};
    };

    constexpr int gridResolution = 75;
    std::vector<FieldVertex> gridNodes(gridResolution * gridResolution);

    for (int j = 0; j < gridResolution; ++j) {
        const double x = (-1.0 + 2.0 * j / (gridResolution - 1)) * extent;
        for (int k = 0; k < gridResolution; ++k) {
            const double y = (-1.0 + 2.0 * k / (gridResolution - 1)) * extent;
            const double depth = depthAt(x, y);
            const auto color = colorForDepth(depth);
            gridNodes[j * gridResolution + k] = FieldVertex{
                static_cast<float>(x),
                static_cast<float>(-depth),
                static_cast<float>(y),
                color[0], color[1], color[2], color[3]
            };
        }
    }

    std::vector<FieldVertex> vertices;
    vertices.reserve(gridResolution * (gridResolution - 1) * 4 + 8);

    // Lines along Y (varying k for each fixed j)
    for (int j = 0; j < gridResolution; ++j) {
        for (int k = 0; k < gridResolution - 1; ++k) {
            vertices.push_back(gridNodes[j * gridResolution + k]);
            vertices.push_back(gridNodes[j * gridResolution + (k + 1)]);
        }
    }

    // Lines along X (varying j for each fixed k)
    for (int k = 0; k < gridResolution; ++k) {
        for (int j = 0; j < gridResolution - 1; ++j) {
            vertices.push_back(gridNodes[j * gridResolution + k]);
            vertices.push_back(gridNodes[(j + 1) * gridResolution + k]);
        }
    }

    // Outer framing boundary
    const float borderAlpha = potentialOpacity_ * 0.75F;
    const std::array<float, 4> borderColor{0.18F, 0.65F, 1.0F, borderAlpha};
    const float extF = static_cast<float>(extent);
    const FieldVertex c0{-extF, 0.0F, -extF, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    const FieldVertex c1{ extF, 0.0F, -extF, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    const FieldVertex c2{ extF, 0.0F,  extF, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
    const FieldVertex c3{-extF, 0.0F,  extF, borderColor[0], borderColor[1], borderColor[2], borderColor[3]};

    vertices.push_back(c0); vertices.push_back(c1);
    vertices.push_back(c1); vertices.push_back(c2);
    vertices.push_back(c2); vertices.push_back(c3);
    vertices.push_back(c3); vertices.push_back(c0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glLineWidth(1.5F);

    fieldVao_.bind();
    fieldVbo_.bind();
    fieldVbo_.allocate(vertices.data(),
                       static_cast<int>(vertices.size() * sizeof(FieldVertex)));
    fieldShader_.bind();
    fieldShader_.setUniformValue("view", view);
    fieldShader_.setUniformValue("projection", projection);
    fieldShader_.enableAttributeArray("position");
    fieldShader_.setAttributeBuffer("position", GL_FLOAT, 0, 3,
                                     sizeof(FieldVertex));
    fieldShader_.enableAttributeArray("color");
    fieldShader_.setAttributeBuffer("color", GL_FLOAT, 3 * sizeof(float), 4,
                                     sizeof(FieldVertex));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    fieldShader_.release();
    fieldVbo_.release();
    fieldVao_.release();
    glLineWidth(1.0F);
    glDepthMask(GL_TRUE);
}

void UniverseView::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!simulation_) return;
    QMatrix4x4 projection;
    projection.perspective(45.0F, static_cast<float>(width()) / std::max(1, height()),
                           0.01F, 100000.0F);
    const QMatrix4x4 view = viewMatrix();
    if (trajectoriesVisible_) {
        for (std::size_t index = 1; index < simulation_->bodies().size(); ++index) {
            const auto& body = simulation_->bodies()[index];
            const auto elements = simulation_->orbitalElements(index);
            drawOrbit(elements, body.hypothetical ? QColor(210, 100, 245, 220) :
                QColor(95, 155, 225, 175), view, projection);
        }
    }
    for (const auto& body : simulation_->bodies()) drawSphere(body, view, projection);
    if (fieldVisible_) drawReferenceField(view, projection);
    QPainter labels(this);
    labels.setRenderHint(QPainter::Antialiasing);
    for (std::size_t index = 0; index < simulation_->bodies().size(); ++index) {
        const auto& body = simulation_->bodies()[index];
        const QPointF point = projectForPicking(body.positionM);
        const bool selected = static_cast<int>(index) == selectedBody_;
        labels.setPen(selected ? Qt::white : QColor(215, 225, 240, 220));
        QFont font = labels.font();
        font.setPointSize(selected ? 11 : 9);
        font.setBold(selected);
        labels.setFont(font);
        labels.drawText(point + QPointF(9, -9), QString::fromStdString(body.name));
    }
    labels.end();
}

void UniverseView::tick() {
    if (running_ && simulation_) simulation_->advance(timeStepS_ * speedMultiplier_);
    if (simulation_ && focusTarget_ >= 0 &&
        focusTarget_ < static_cast<int>(simulation_->bodies().size())) {
        const auto target = simulation_->bodies()[static_cast<std::size_t>(focusTarget_)].positionM;
        focusPosition_ += 0.12 * (target - focusPosition_);
    }
    update();
}

QPointF UniverseView::projectForPicking(const physics::Vector3& position) const {
    QMatrix4x4 projection;
    projection.perspective(45.0F, static_cast<float>(width()) / std::max(1, height()),
                           0.01F, 100000.0F);
    const auto relative = (position - focusPosition_) / AU;
    const QVector4D world(static_cast<float>(relative.x()),
                          static_cast<float>(relative.z()),
                          static_cast<float>(relative.y()), 1.0F);
    const QVector4D clip = projection * viewMatrix() * world;
    if (clip.w() <= 0.0F) return QPointF(-10000.0, -10000.0);
    const QVector3D ndc = clip.toVector3D() / clip.w();
    return QPointF((ndc.x() + 1.0F) * 0.5F * width(),
                   (1.0F - ndc.y()) * 0.5F * height());
}

void UniverseView::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->pos();
    setFocus(Qt::MouseFocusReason);
    if (!simulation_) return;
    double closest = 22.0;
    selectedBody_ = -1;
    for (std::size_t i = 0; i < simulation_->bodies().size(); ++i) {
        const double distance = QLineF(event->position(),
            projectForPicking(simulation_->bodies()[i].positionM)).length();
        if (distance < closest) {
            closest = distance;
            selectedBody_ = static_cast<int>(i);
        }
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
    focusTarget_ = index;
    focusPosition_ = simulation_->bodies()[static_cast<std::size_t>(index)].positionM;
    distanceAu_ = 8.0F;
    emit bodySelected(index);
    update();
}

void UniverseView::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        yaw_ += static_cast<float>((event->position().x() - lastMouse_.x()) * 0.01);
        pitch_ = std::clamp(pitch_ + static_cast<float>(
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
    if (event->key() == Qt::Key_Escape) returnToSystemView();
    else if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
        distanceAu_ = std::max(0.05F, distanceAu_ * 0.82F);
    else if (event->key() == Qt::Key_Minus)
        distanceAu_ = std::min(100000.0F, distanceAu_ * 1.22F);
    else QOpenGLWidget::keyPressEvent(event);
    update();
}

} // namespace erebus::render

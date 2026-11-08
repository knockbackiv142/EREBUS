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

void UniverseView::drawField(std::size_t sourceIndex, const QMatrix4x4& view,
                             const QMatrix4x4& projection) {
    if (!simulation_ || sourceIndex >= simulation_->bodies().size()) return;
    const auto& source = simulation_->bodies()[sourceIndex];
    const double sourceAu = source.positionM.norm() / AU;
    const double extentAu = source.hypothetical || sourceAu > 40.0 ? 20.0 : 4.0;
    const int samples = 31;
    std::vector<FieldVertex> vertices;
    vertices.reserve((samples - 1) * (samples - 1) * 4);
    const double minimum = simulation_->gravitationalFieldMagnitude(
        source.positionM + physics::Vector3{extentAu * AU, 0, 0}, sourceIndex);
    const double maximum = simulation_->gravitationalFieldMagnitude(
        source.positionM + physics::Vector3{0.02 * AU, 0, 0}, sourceIndex);
    const auto makeVertex = [&](double localX, double localY) {
        const physics::Vector3 point = source.positionM +
            physics::Vector3{localX * AU, localY * AU, 0};
        const double field = simulation_->gravitationalFieldMagnitude(point, sourceIndex);
        const double normalized = std::clamp(
            (std::log10(std::max(field, minimum)) - std::log10(minimum)) /
            std::max(1.0, std::log10(maximum) - std::log10(minimum)), 0.0, 1.0);
        return FieldVertex{
            static_cast<float>((point.x() - focusPosition_.x()) / AU),
            static_cast<float>((point.z() - focusPosition_.z()) / AU),
            static_cast<float>((point.y() - focusPosition_.y()) / AU),
            static_cast<float>(normalized),
            static_cast<float>(0.15 + 0.75 * (1.0 - normalized)),
            static_cast<float>(1.0 - normalized), 0.48F};
    };
    for (int y = 0; y < samples - 1; ++y) {
        for (int x = 0; x < samples - 1; ++x) {
            const double x0 = (static_cast<double>(x) / (samples - 1) - 0.5) * 2.0 * extentAu;
            const double x1 = (static_cast<double>(x + 1) / (samples - 1) - 0.5) * 2.0 * extentAu;
            const double y0 = (static_cast<double>(y) / (samples - 1) - 0.5) * 2.0 * extentAu;
            const double y1 = (static_cast<double>(y + 1) / (samples - 1) - 0.5) * 2.0 * extentAu;
            vertices.push_back(makeVertex(x0, y0));
            vertices.push_back(makeVertex(x1, y0));
            vertices.push_back(makeVertex(x1, y1));
            vertices.push_back(makeVertex(x0, y1));
        }
    }
    glDisable(GL_DEPTH_TEST);
    fieldVao_.bind();
    fieldVbo_.bind();
    fieldVbo_.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(FieldVertex)));
    fieldShader_.bind();
    fieldShader_.setUniformValue("view", view);
    fieldShader_.setUniformValue("projection", projection);
    fieldShader_.enableAttributeArray("position");
    fieldShader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, sizeof(FieldVertex));
    fieldShader_.enableAttributeArray("color");
    fieldShader_.setAttributeBuffer("color", GL_FLOAT, 3 * sizeof(float), 4, sizeof(FieldVertex));
    glDrawArrays(GL_QUADS, 0, static_cast<GLsizei>(vertices.size()));
    fieldShader_.release();
    fieldVbo_.release();
    fieldVao_.release();
    glEnable(GL_DEPTH_TEST);
}

void UniverseView::drawFieldOverlay(QPainter& painter) {
    if (!simulation_ || selectedBody_ < 0 ||
        selectedBody_ >= static_cast<int>(simulation_->bodies().size())) return;
    const auto& source = simulation_->bodies()[static_cast<std::size_t>(selectedBody_)];
    const double extentAu = source.hypothetical || source.positionM.norm() / AU > 40.0 ? 20.0 : 4.0;
    constexpr int samples = 25;
    const double minimum = simulation_->gravitationalFieldMagnitude(
        source.positionM + physics::Vector3{extentAu * AU, 0, 0},
        static_cast<std::size_t>(selectedBody_));
    const double maximum = simulation_->gravitationalFieldMagnitude(
        source.positionM + physics::Vector3{0.02 * AU, 0, 0},
        static_cast<std::size_t>(selectedBody_));
    const auto fieldColor = [&](double xAu, double yAu) {
        const auto point = source.positionM + physics::Vector3{xAu * AU, yAu * AU, 0};
        const double field = simulation_->gravitationalFieldMagnitude(
            point, static_cast<std::size_t>(selectedBody_));
        const double normalized = std::clamp(
            (std::log10(std::max(field, minimum)) - std::log10(minimum)) /
            std::max(1.0, std::log10(maximum) - std::log10(minimum)), 0.0, 1.0);
        const int red = static_cast<int>(255.0 * normalized);
        const int blue = static_cast<int>(255.0 * (1.0 - normalized));
        return QColor(red, 25, blue, 145);
    };
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < samples - 1; ++y) {
        for (int x = 0; x < samples - 1; ++x) {
            const double x0 = (static_cast<double>(x) / (samples - 1) - 0.5) * 2.0 * extentAu;
            const double x1 = (static_cast<double>(x + 1) / (samples - 1) - 0.5) * 2.0 * extentAu;
            const double y0 = (static_cast<double>(y) / (samples - 1) - 0.5) * 2.0 * extentAu;
            const double y1 = (static_cast<double>(y + 1) / (samples - 1) - 0.5) * 2.0 * extentAu;
            const auto corner = [&](double xAu, double yAu) {
                return projectForPicking(source.positionM +
                    physics::Vector3{xAu * AU, yAu * AU, 0});
            };
            const double centerX = (x0 + x1) * 0.5;
            const double centerY = (y0 + y1) * 0.5;
            painter.setBrush(fieldColor(centerX, centerY));
            painter.drawPolygon(QPolygonF{corner(x0, y0), corner(x1, y0),
                                          corner(x1, y1), corner(x0, y1)});
        }
    }
}

void UniverseView::drawPotentialField(const QMatrix4x4& view,
                                      const QMatrix4x4& projection) {
    if (!simulation_) return;
    constexpr int sampleCount = 22;
    const int sampleStride = sampleCount + 1;
    const int samplePlane = sampleStride * sampleStride;
    const auto sample = [&](const physics::Vector3& point) {
        double potential = 0.0;
        for (std::size_t index = 0; index < simulation_->bodies().size(); ++index) {
            if (potentialSource_ >= 0 &&
                index != static_cast<std::size_t>(potentialSource_)) continue;
            const auto& body = simulation_->bodies()[index];
            if (!body.massive) continue;
            const double distance = (point - body.positionM).norm();
            const double maskRadius = std::max(body.radiusM, 1.0e-6 * AU);
            if (distance <= maskRadius) return std::numeric_limits<double>::quiet_NaN();
            potential -= physics::Simulation::GravitationalConstant *
                body.massKg / distance;
        }
        return potential;
    };
    std::vector<double> values(static_cast<std::size_t>(sampleStride * samplePlane));
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    const auto pointAt = [&](int x, int y, int z) {
        const double scale = 2.0 * potentialRadiusAu_ / sampleCount;
        return focusPosition_ + physics::Vector3{
            (-potentialRadiusAu_ + x * scale) * AU,
            (-potentialRadiusAu_ + y * scale) * AU,
            (-potentialRadiusAu_ + z * scale) * AU};
    };
    const auto valueAt = [&](int x, int y, int z) -> double& {
        return values[static_cast<std::size_t>(z * samplePlane + y * sampleStride + x)];
    };
    for (int z = 0; z <= sampleCount; ++z) {
        for (int y = 0; y <= sampleCount; ++y) {
            for (int x = 0; x <= sampleCount; ++x) {
                const double value = sample(pointAt(x, y, z));
                valueAt(x, y, z) = value;
                if (std::isfinite(value)) {
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                }
            }
        }
    }
    if (!std::isfinite(minimum) || !(maximum > minimum)) return;
    const double range = maximum - minimum;
    std::vector<double> levels;
    for (int level = 1; level <= potentialLevelCount_; ++level) {
        const double value = minimum + range * potentialLevelSpacing_ * level;
        if (value < maximum) levels.push_back(value);
    }
    if (levels.empty()) levels.push_back(minimum + range * 0.5);
    const auto colorFor = [&](std::size_t level) {
        static constexpr std::array<std::array<float, 3>, 6> colors{{
            {{0.20F, 0.55F, 1.00F}}, {{0.25F, 0.85F, 0.85F}},
            {{0.35F, 1.00F, 0.55F}}, {{1.00F, 0.80F, 0.25F}},
            {{1.00F, 0.45F, 0.25F}}, {{0.85F, 0.35F, 1.00F}}
        }};
        const auto& color = colors[level % colors.size()];
        return std::array<float, 4>{color[0], color[1], color[2],
                                    potentialOpacity_};
    };
    std::vector<FieldVertex> vertices;
    const auto addPoint = [&](const physics::Vector3& point,
                              const std::array<float, 4>& color) {
        vertices.push_back({static_cast<float>((point.x() - focusPosition_.x()) / AU),
            static_cast<float>((point.z() - focusPosition_.z()) / AU),
            static_cast<float>((point.y() - focusPosition_.y()) / AU),
            color[0], color[1], color[2], color[3]});
    };
    static constexpr int tetrahedra[6][4] = {
        {0, 5, 1, 6}, {0, 5, 6, 4}, {0, 1, 5, 4},
        {1, 3, 5, 6}, {1, 3, 6, 2}, {3, 7, 5, 6}};
    static constexpr int edges[6][2] = {
        {0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}};
    for (std::size_t levelIndex = 0; levelIndex < levels.size(); ++levelIndex) {
        const double level = levels[levelIndex];
        for (int z = 0; z < sampleCount; ++z) {
            for (int y = 0; y < sampleCount; ++y) {
                for (int x = 0; x < sampleCount; ++x) {
                    const int cube[8][3] = {
                        {x, y, z}, {x + 1, y, z}, {x, y + 1, z}, {x + 1, y + 1, z},
                        {x, y, z + 1}, {x + 1, y, z + 1},
                        {x, y + 1, z + 1}, {x + 1, y + 1, z + 1}};
                    for (const auto& tetra : tetrahedra) {
                        std::array<physics::Vector3, 4> points;
                        std::array<double, 4> scalar{};
                        bool valid = true;
                        for (int corner = 0; corner < 4; ++corner) {
                            const auto& coordinate = cube[tetra[corner]];
                            points[corner] = pointAt(coordinate[0], coordinate[1],
                                                     coordinate[2]);
                            scalar[corner] = valueAt(coordinate[0], coordinate[1],
                                                    coordinate[2]);
                            valid = valid && std::isfinite(scalar[corner]);
                        }
                        if (!valid) continue;
                        std::array<physics::Vector3, 6> intersections;
                        int intersectionCount = 0;
                        for (const auto& edge : edges) {
                            const double a = scalar[edge[0]];
                            const double b = scalar[edge[1]];
                            if ((a < level) == (b < level) || a == b) continue;
                            const double fraction = (level - a) / (b - a);
                            intersections[intersectionCount++] =
                                points[edge[0]] + fraction *
                                (points[edge[1]] - points[edge[0]]);
                        }
                        const auto color = colorFor(levelIndex);
                        if (intersectionCount == 3) {
                            addPoint(intersections[0], color);
                            addPoint(intersections[1], color);
                            addPoint(intersections[2], color);
                        } else if (intersectionCount == 4) {
                            addPoint(intersections[0], color);
                            addPoint(intersections[1], color);
                            addPoint(intersections[2], color);
                            addPoint(intersections[0], color);
                            addPoint(intersections[2], color);
                            addPoint(intersections[3], color);
                        }
                    }
                }
            }
        }
    }
    if (vertices.empty()) {
        qWarning() << "Erebus potential field produced no isosurface triangles"
                   << "range" << minimum << maximum
                   << "levels" << static_cast<int>(levels.size());
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    fieldVao_.bind();
    fieldVbo_.bind();
    fieldVbo_.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(FieldVertex)));
    fieldShader_.bind();
    fieldShader_.setUniformValue("view", view);
    fieldShader_.setUniformValue("projection", projection);
    fieldShader_.enableAttributeArray("position");
    fieldShader_.setAttributeBuffer("position", GL_FLOAT, 0, 3, sizeof(FieldVertex));
    fieldShader_.enableAttributeArray("color");
    fieldShader_.setAttributeBuffer("color", GL_FLOAT, 3 * sizeof(float), 4, sizeof(FieldVertex));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    fieldShader_.release();
    fieldVbo_.release();
    fieldVao_.release();
    glDepthMask(GL_TRUE);
}

void UniverseView::drawReferenceField(const QMatrix4x4& view,
                                      const QMatrix4x4& projection) {
    if (!simulation_) return;
    // One fixed world lattice covers the complete supported 1000 AU window.
    // The radius below only clips this lattice; it never changes line
    // positions or resolution.
    constexpr int gridCount = 4001;
    constexpr double latticeExtentAu = 1000.0;
    constexpr double latticeSpacingAu =
        2.0 * latticeExtentAu / (gridCount - 1);
    const double extent = potentialRadiusAu_;
    // Keep the visual analogy shallow. The potential is physical; this
    // displacement is only a readable rendering scale and must not hide the
    // bodies below the reference plane.
    // Scale only the display displacement with the visible extent. The
    // potential contribution remains dimensionless and mass-based, so changing
    // the field radius changes framing rather than gravitational strength.
    const double maxDepth = std::clamp(extent * 0.08, 0.08, 0.65);
    struct Well {
        physics::Vector3 position;
        double mass;
        double softeningAu;
    };
    std::vector<Well> wells;
    for (std::size_t index = 0; index < simulation_->bodies().size(); ++index) {
        if (potentialSource_ >= 0 &&
            index != static_cast<std::size_t>(potentialSource_)) continue;
        const auto& body = simulation_->bodies()[index];
        if (!body.massive || body.massKg <= 0.0) continue;
        const physics::Vector3 relative = body.positionM - focusPosition_;
        if (std::hypot(relative.x(), relative.y()) / AU <= extent * 1.5)
            wells.push_back({relative, body.massKg,
                             std::max({body.radiusM / AU,
                                       latticeSpacingAu,
                                       1.0e-4})});
    }
    if (wells.empty()) {
        for (std::size_t index = 0; index < simulation_->bodies().size(); ++index) {
            if (potentialSource_ >= 0 &&
                index != static_cast<std::size_t>(potentialSource_)) continue;
            const auto& body = simulation_->bodies()[index];
            if (body.massive && body.massKg > 0.0)
                wells.push_back({body.positionM - focusPosition_, body.massKg,
                                 std::max({body.radiusM / AU,
                                           latticeSpacingAu,
                                           1.0e-4})});
        }
    }
    if (wells.empty()) return;
    const auto reference = std::max_element(wells.begin(), wells.end(),
        [](const Well& a, const Well& b) { return a.mass < b.mass; });
    const double referenceMass = std::max(reference->mass, 1.0);
    const auto wellPotentialAt = [&](double xAu, double yAu) {
        double normalizedPotential = 0.0;
        for (const auto& well : wells) {
            const double dx = (xAu - well.position.x() / AU) / extent;
            const double dy = (yAu - well.position.y() / AU) / extent;
            const double dz = (well.position.z() / AU) / extent;
            const double softening = well.softeningAu / extent;
            const double distanceNormalized = std::sqrt(
                dx * dx + dy * dy + dz * dz + softening * softening);
            normalizedPotential -= (well.mass / referenceMass) /
                std::max(distanceNormalized, 1.0e-6);
        }
        return normalizedPotential;
    };
    const double boundaryPotential = std::max({
        wellPotentialAt(-extent, -extent),
        wellPotentialAt(-extent, extent),
        wellPotentialAt(extent, -extent),
        wellPotentialAt(extent, extent)});
    const double fieldScale = std::max(1.0e-6,
        std::abs(boundaryPotential) * 0.35);
    const auto colorForDepth = [&](double depth) {
        const float amount = static_cast<float>(
            std::clamp(depth / maxDepth, 0.0, 1.0));
        return std::array<float, 4>{
            0.12F + 0.85F * amount,
            0.48F + 0.35F * (1.0F - amount),
            1.0F - 0.70F * amount,
            potentialOpacity_ * (0.55F + 0.45F * amount)};
    };
    const auto depthAt = [&](double xAu, double yAu) {
        const double excessPotential = std::max(
            0.0, boundaryPotential - wellPotentialAt(xAu, yAu));
        const double normalized = std::clamp(
            excessPotential / fieldScale, 0.0, 1.0);
        return maxDepth * (1.0 - std::exp(-2.2 * normalized));
    };
    static int diagnosticsRemaining = 3;
    if (diagnosticsRemaining > 0) {
        qWarning() << "Erebus field wells:" << static_cast<int>(wells.size())
                   << "center depth AU:" << depthAt(0.0, 0.0)
                   << "edge depth AU:" << depthAt(extent, extent)
                   << "max depth AU:" << maxDepth;
        --diagnosticsRemaining;
    }
    std::vector<FieldVertex> vertices;
    const auto addPoint = [&](double xAu, double yAu, double depthAu) {
        const physics::Vector3 point = focusPosition_ +
            physics::Vector3{xAu * AU, yAu * AU, -depthAu * AU};
        const auto color = colorForDepth(depthAu);
        vertices.push_back({static_cast<float>((point.x() - focusPosition_.x()) / AU),
            static_cast<float>((point.z() - focusPosition_.z()) / AU),
            static_cast<float>((point.y() - focusPosition_.y()) / AU),
            color[0], color[1], color[2], color[3]});
    };
    const int firstLine = std::max(0, static_cast<int>(
        std::ceil((latticeExtentAu - extent) / latticeSpacingAu)));
    const int lastLine = std::min(gridCount - 1, static_cast<int>(
        std::floor((latticeExtentAu + extent) / latticeSpacingAu)));
    const int firstSegment = std::max(0, static_cast<int>(
        std::floor((latticeExtentAu - extent) / latticeSpacingAu)));
    const int lastSegment = std::min(gridCount - 2, static_cast<int>(
        std::ceil((latticeExtentAu + extent) / latticeSpacingAu)));
    for (int line = firstLine; line <= lastLine; ++line) {
        const double fixed = -latticeExtentAu +
            2.0 * latticeExtentAu * line / (gridCount - 1);
        for (int segment = firstSegment; segment <= lastSegment; ++segment) {
            const double a = -latticeExtentAu +
                2.0 * latticeExtentAu * segment / (gridCount - 1);
            const double b = -latticeExtentAu +
                2.0 * latticeExtentAu * (segment + 1) /
                (gridCount - 1);
            const double clippedA = std::clamp(a, -extent, extent);
            const double clippedB = std::clamp(b, -extent, extent);
            if (std::abs(fixed) <= extent && clippedA < clippedB) {
                addPoint(clippedA, fixed, depthAt(clippedA, fixed));
                addPoint(clippedB, fixed, depthAt(clippedB, fixed));
                addPoint(fixed, clippedA, depthAt(fixed, clippedA));
                addPoint(fixed, clippedB, depthAt(fixed, clippedB));
            }
        }
    }
    if (vertices.empty()) {
        const double center = 0.0;
        addPoint(-extent, center, depthAt(-extent, center));
        addPoint(extent, center, depthAt(extent, center));
        addPoint(center, -extent, depthAt(center, -extent));
        addPoint(center, extent, depthAt(center, extent));
    }
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
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

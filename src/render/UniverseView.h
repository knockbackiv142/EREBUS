#pragma once

#include "../physics/Simulation.h"
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QTimer>
#include <vector>

class QPainter;

namespace erebus::render {

class UniverseView final : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit UniverseView(QWidget* parent = nullptr);
    void setSimulation(physics::Simulation* simulation) noexcept { simulation_ = simulation; }
    void setRunning(bool running) noexcept { running_ = running; }
    void setTimeStep(double seconds) noexcept { timeStepS_ = seconds; }
    void setSpeedMultiplier(double multiplier) noexcept { speedMultiplier_ = multiplier; }
    void setReferenceGridVisible(bool visible) noexcept { gridVisible_ = visible; update(); }
    void setTrajectoriesVisible(bool visible) noexcept { trajectoriesVisible_ = visible; }
    void setFieldVisible(bool visible) noexcept { fieldVisible_ = visible; update(); }
    void setPotentialSource(int sourceIndex) noexcept {
        potentialSource_ = sourceIndex;
        update();
    }
    void setPotentialRadius(double radiusAu) noexcept {
        potentialRadiusAu_ = static_cast<float>(radiusAu);
        update();
    }
    void setPotentialOpacity(int opacity) noexcept {
        potentialOpacity_ = static_cast<float>(opacity) / 100.0F;
        update();
    }
    [[nodiscard]] int selectedBody() const noexcept { return selectedBody_; }
    void focusBody(int index);
    void returnToSystemView() noexcept {
        focusTarget_ = -1;
        focusPosition_.setZero();
        distanceAu_ = 80.0F;
        update();
    }

signals:
    void bodySelected(int index);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void tick();

private:
    struct Vertex {
        float x, y, z;
        float nx, ny, nz;
    };
    struct FieldVertex {
        float x, y, z;
        float r, g, b, a;
    };

    void createSphereMesh();
    void drawSphere(const physics::Body& body, const QMatrix4x4& view,
                    const QMatrix4x4& projection);
    void drawOrbit(const physics::OrbitalElements& elements,
                   const QColor& color, const QMatrix4x4& view,
                   const QMatrix4x4& projection);
    void drawReferenceField(const QMatrix4x4& view, const QMatrix4x4& projection);
    [[nodiscard]] QMatrix4x4 viewMatrix() const;
    [[nodiscard]] QPointF projectForPicking(const physics::Vector3& position) const;

    physics::Simulation* simulation_{};
    QTimer timer_;
    QOpenGLShaderProgram shader_;
    QOpenGLVertexArrayObject sphereVao_;
    QOpenGLBuffer sphereVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject orbitVao_;
    QOpenGLBuffer orbitVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLShaderProgram fieldShader_;
    QOpenGLVertexArrayObject fieldVao_;
    QOpenGLBuffer fieldVbo_{QOpenGLBuffer::VertexBuffer};
    std::vector<Vertex> sphereVertices_;
    bool running_{true};
    bool gridVisible_{true};
    bool trajectoriesVisible_{true};
    double timeStepS_{86400.0};
    double speedMultiplier_{1.0};
    float distanceAu_{65.0F};
    float yaw_{0.0F};
    float pitch_{0.55F};
    QPoint lastMouse_;
    int selectedBody_{-1};
    int focusTarget_{-1};
    bool fieldVisible_{false};
    int potentialSource_{-1};
    float potentialRadiusAu_{8.0F};
    float potentialOpacity_{0.55F};
    physics::Vector3 focusPosition_{physics::Vector3::Zero()};
};

} // namespace erebus::render

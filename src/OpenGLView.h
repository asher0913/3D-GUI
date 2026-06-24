#pragma once

#include "ModelTypes.h"

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

class OpenGLView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit OpenGLView(QWidget* parent = nullptr);

    void setModels(const QVector<ModelInstance>* models);
    void setSelectedIndex(int index);
    void setBuildPlateSize(double widthMm, double depthMm);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void drawMesh(const QVector<QVector3D>& vertices,
                  const QVector<QVector3D>& normals,
                  const QMatrix4x4& model,
                  const QColor& color,
                  GLenum primitive = GL_TRIANGLES);
    void drawBackground();
    void drawBuildPlate();

    const QVector<ModelInstance>* m_models = nullptr;
    int m_selectedIndex = -1;
    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLShaderProgram* m_bgProgram = nullptr;
    QPoint m_lastMousePos;
    float m_yaw = -35.0f;
    float m_pitch = 58.0f;
    float m_distance = 220.0f;
    QVector3D m_pan = QVector3D(0.0f, 0.0f, 0.0f);
    double m_plateWidthMm = 96.0;
    double m_plateDepthMm = 54.0;
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
};

#pragma once

#include "StlMesh.h"

#include <QColor>
#include <QMatrix4x4>
#include <QString>
#include <QVector>
#include <QVector3D>

struct Transform {
    QVector3D translation = QVector3D(0.0f, 0.0f, 0.0f);
    QVector3D rotationDeg = QVector3D(0.0f, 0.0f, 0.0f);
    float scale = 1.0f;

    QMatrix4x4 matrix() const
    {
        QMatrix4x4 m;
        m.translate(translation);
        m.rotate(rotationDeg.z(), 0.0f, 0.0f, 1.0f);
        m.rotate(rotationDeg.y(), 0.0f, 1.0f, 0.0f);
        m.rotate(rotationDeg.x(), 1.0f, 0.0f, 0.0f);
        m.scale(scale);
        return m;
    }
};

struct ModelInstance {
    QString filePath;
    QString name;
    StlMesh mesh;
    Transform transform;
    int materialIndex = 0;
    QColor color = QColor(44, 160, 220);
    bool visible = true;
};

// Exposure parameters that can differ per material.
struct MaterialExposure {
    double bottomExposureTime = 7.0;
    int bottomExposureCurrent = 15;
    double standardExposureTime = 2.5;
    int standardExposureCurrent = 15;
};

struct SliceSettings {
    int outputWidth = 1920;
    int outputHeight = 1080;
    double pixelSizeMm = 0.05;
    double layerHeightMm = 0.05;
    int materialCount = 2;

    int bottomLayers = 3;
    // One entry per material; size is kept in sync with materialCount.
    QVector<MaterialExposure> materials;

    QString pythonPath = QStringLiteral("python3");
    QString mergeScriptPath;
    QString outputDir;
};

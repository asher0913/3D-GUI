#pragma once

#include "PresetLibrary.h"
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

// Exposure parameters that can differ per material slot.
// The UI works in "light strength"; the machine current is derived from strength
// via the selected machine's strength/current map at export time.
struct MaterialExposure {
    QString presetId;  // chosen material preset for this slot (optional)
    double bottomExposureTime = 7.0;
    double bottomExposureStrength = 0.0;
    double standardExposureTime = 2.5;
    double standardExposureStrength = 0.0;
};

struct SliceSettings {
    int outputWidth = 3840;
    int outputHeight = 2160;
    double pixelSizeMm = 0.05;
    double layerHeightMm = 0.05;
    int materialCount = 3;

    // Selected machine; selectedMachine carries the strength->current map so the
    // background worker can convert without touching the UI.
    QString selectedMachineName;
    MachinePreset selectedMachine;

    int bottomLayers = 3;
    // One entry per material slot; size is kept in sync with materialCount.
    QVector<MaterialExposure> materials;

    // Advanced options (written verbatim into config.yaml for the backend).
    bool groove = false;
    bool slide0 = false;
    bool slide1 = false;
    bool slide2 = false;

    // Backend command-line tool. In production this is an executable; in dev mode
    // it may be a `.py` script, in which case pythonPath is used to run it.
    QString mergeToolPath;
    QString pythonPath = QStringLiteral("python3");
    QString outputDir;
};

#pragma once

#include <QColor>
#include <QString>
#include <QVector>

#include <cmath>

// One printer/machine preset loaded from the YAML preset library.
struct MachinePreset {
    QString id;
    QString displayName;
    int projectorWidth = 3840;
    int projectorHeight = 2160;
    double pixelWidthMm = 0.05;
    double pixelHeightMm = 0.05;
    int materialNumLimit = 3;
    QVector<int> currentMap;      // machine current points
    QVector<double> strengthMap;  // light-strength points, parallel to currentMap

    bool hasMap() const
    {
        return !currentMap.isEmpty() && currentMap.size() == strengthMap.size();
    }

    // Light strength -> machine current. Linear interpolation between the two
    // bracketing strength points; clamped to the end points outside the range.
    // Result is rounded to an integer (the backend / GCode uses integer current).
    int currentFromStrength(double strength, bool* outOfRange = nullptr) const
    {
        if (outOfRange) {
            *outOfRange = false;
        }
        if (!hasMap()) {
            return static_cast<int>(std::lround(strength));
        }
        const int n = strengthMap.size();
        if (strength <= strengthMap.first()) {
            if (outOfRange && strength < strengthMap.first()) {
                *outOfRange = true;
            }
            return currentMap.first();
        }
        if (strength >= strengthMap.last()) {
            if (outOfRange && strength > strengthMap.last()) {
                *outOfRange = true;
            }
            return currentMap.last();
        }
        for (int i = 0; i + 1 < n; ++i) {
            const double s0 = strengthMap[i];
            const double s1 = strengthMap[i + 1];
            if (strength >= s0 && strength <= s1) {
                const double t = (s1 == s0) ? 0.0 : (strength - s0) / (s1 - s0);
                const double cur = currentMap[i] + t * (currentMap[i + 1] - currentMap[i]);
                return static_cast<int>(std::lround(cur));
            }
        }
        return currentMap.last();
    }

    // Machine current -> light strength. Used to show a strength value for legacy
    // materials that only provide a current.
    double strengthFromCurrent(double current) const
    {
        if (!hasMap()) {
            return current;
        }
        const int n = currentMap.size();
        if (current <= currentMap.first()) {
            return strengthMap.first();
        }
        if (current >= currentMap.last()) {
            return strengthMap.last();
        }
        for (int i = 0; i + 1 < n; ++i) {
            const double c0 = currentMap[i];
            const double c1 = currentMap[i + 1];
            if (current >= c0 && current <= c1) {
                const double t = (c1 == c0) ? 0.0 : (current - c0) / (c1 - c0);
                return strengthMap[i] + t * (strengthMap[i + 1] - strengthMap[i]);
            }
        }
        return strengthMap.last();
    }
};

// One material preset loaded from the YAML preset library.
struct MaterialPreset {
    QString id;
    QString displayName;
    QColor color;  // invalid QColor() if the config does not specify one
    double bottomExposureTime = 7.0;
    double standardExposureTime = 2.5;

    // If the config gave strength directly, hasStrength is true and these hold it.
    bool hasStrength = false;
    double bottomExposureStrength = 0.0;
    double standardExposureStrength = 0.0;

    // Legacy: config that only gives current.
    int legacyBottomExposureCurrent = 15;
    int legacyStandardExposureCurrent = 15;
};

struct PresetLibrary {
    QString sourcePath;
    QVector<MachinePreset> machines;
    QVector<MaterialPreset> materials;

    bool isValid() const { return !machines.isEmpty(); }

    const MachinePreset* machineById(const QString& id) const
    {
        for (const MachinePreset& m : machines) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    }

    const MaterialPreset* materialById(const QString& id) const
    {
        for (const MaterialPreset& m : materials) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    }
};

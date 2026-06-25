#include "PresetLoader.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <cmath>

namespace {

QString unquote(const QString& raw)
{
    QString v = raw.trimmed();
    if (v.size() >= 2 && ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith('\'') && v.endsWith('\'')))) {
        v = v.mid(1, v.size() - 2);
    }
    return v;
}

QStringList splitList(const QString& value)
{
    QStringList out;
    const auto parts = value.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) {
            out << t;
        }
    }
    return out;
}

QVector<double> parseDoubleList(const QString& value)
{
    QVector<double> out;
    const auto parts = value.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const double d = part.trimmed().toDouble(&ok);
        if (ok) {
            out << d;
        }
    }
    return out;
}

QVector<int> parseIntList(const QString& value)
{
    QVector<int> out;
    const auto parts = value.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const double d = part.trimmed().toDouble(&ok);
        if (ok) {
            out << static_cast<int>(std::lround(d));
        }
    }
    return out;
}

int leadingWhitespace(const QString& line)
{
    int n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) {
        ++n;
    }
    return n;
}

} // namespace

bool PresetLoader::load(const QString& path, PresetLibrary* outLibrary, QString* errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法打开配置文件: %1").arg(path);
        }
        return false;
    }
    QTextStream in(&file);
    in.setCodec("UTF-8");
    const QStringList lines = in.readAll().split('\n');
    file.close();

    PresetLibrary lib;
    lib.sourcePath = path;

    // Pass 1: discover the machine and material names so we can recognise blocks.
    QStringList machineNames;
    QStringList materialNames;
    for (const QString& raw : lines) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        if (leadingWhitespace(raw) != 0) {
            continue;
        }
        const int colon = trimmed.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QString key = trimmed.left(colon).trimmed();
        const QString value = trimmed.mid(colon + 1).trimmed();
        if (key == QStringLiteral("device_name_list")) {
            machineNames = splitList(value);
        } else if (key == QStringLiteral("material_name_list")) {
            materialNames = splitList(value);
        }
    }

    for (const QString& name : machineNames) {
        MachinePreset m;
        m.id = name;
        m.displayName = name;
        lib.machines.append(m);
    }
    for (const QString& name : materialNames) {
        MaterialPreset m;
        m.id = name;
        m.displayName = name;
        lib.materials.append(m);
    }

    auto machinePtr = [&lib](const QString& id) -> MachinePreset* {
        for (MachinePreset& m : lib.machines) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    };
    auto materialPtr = [&lib](const QString& id) -> MaterialPreset* {
        for (MaterialPreset& m : lib.materials) {
            if (m.id == id) {
                return &m;
            }
        }
        return nullptr;
    };

    // Pass 2: fill in block fields.
    enum class Mode { None, Machine, Material };
    Mode mode = Mode::None;
    MachinePreset* machine = nullptr;
    MaterialPreset* material = nullptr;

    for (const QString& raw : lines) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        const int colon = trimmed.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QString key = trimmed.left(colon).trimmed();
        const QString value = trimmed.mid(colon + 1).trimmed();
        const bool topLevel = leadingWhitespace(raw) == 0;

        if (topLevel) {
            if (key == QStringLiteral("device_name_list") || key == QStringLiteral("material_name_list")) {
                mode = Mode::None;
                continue;
            }
            if (machineNames.contains(key)) {
                machine = machinePtr(key);
                mode = Mode::Machine;
                continue;
            }
            if (materialNames.contains(key)) {
                material = materialPtr(key);
                mode = Mode::Material;
                continue;
            }
            mode = Mode::None;
            continue;
        }

        // Nested field.
        if (mode == Mode::Machine && machine) {
            if (key == QStringLiteral("projector_width")) {
                machine->projectorWidth = value.toInt();
            } else if (key == QStringLiteral("projector_height")) {
                machine->projectorHeight = value.toInt();
            } else if (key == QStringLiteral("pixel_width")) {
                machine->pixelWidthMm = value.toDouble();
            } else if (key == QStringLiteral("pixel_height")) {
                machine->pixelHeightMm = value.toDouble();
            } else if (key == QStringLiteral("current_map")) {
                machine->currentMap = parseIntList(value);
            } else if (key == QStringLiteral("strength_map")) {
                machine->strengthMap = parseDoubleList(value);
            } else if (key == QStringLiteral("material_num_limit")) {
                machine->materialNumLimit = value.toInt();
            } else if (key == QStringLiteral("name") || key == QStringLiteral("display_name")) {
                machine->displayName = unquote(value);
            }
        } else if (mode == Mode::Material && material) {
            if (key == QStringLiteral("color")) {
                const QColor c(unquote(value));
                if (c.isValid()) {
                    material->color = c;
                }
            } else if (key == QStringLiteral("bottom_exposure_time")) {
                material->bottomExposureTime = value.toDouble();
            } else if (key == QStringLiteral("standard_exposure_time")) {
                material->standardExposureTime = value.toDouble();
            } else if (key == QStringLiteral("bottom_exposure_strength")) {
                material->bottomExposureStrength = value.toDouble();
                material->hasStrength = true;
            } else if (key == QStringLiteral("standard_exposure_strength")) {
                material->standardExposureStrength = value.toDouble();
                material->hasStrength = true;
            } else if (key == QStringLiteral("bottom_exposure_current")) {
                material->legacyBottomExposureCurrent = static_cast<int>(std::lround(value.toDouble()));
            } else if (key == QStringLiteral("standard_exposure_current")) {
                material->legacyStandardExposureCurrent = static_cast<int>(std::lround(value.toDouble()));
            } else if (key == QStringLiteral("name") || key == QStringLiteral("display_name")) {
                material->displayName = unquote(value);
            }
        }
    }

    if (!lib.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置文件中未找到任何机器 (device_name_list)。");
        }
        return false;
    }

    if (outLibrary) {
        *outLibrary = lib;
    }
    return true;
}

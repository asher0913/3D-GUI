#include "ConfigWriter.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

namespace {

QString yamlBool(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString yamlPath(const QString& path)
{
    QString normalized = QDir::fromNativeSeparators(QDir(path).absolutePath());
    normalized.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(normalized);
}

QString yamlScalar(QString value, bool forceQuote)
{
    value = value.trimmed();
    if (forceQuote) {
        value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
        value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QStringLiteral("\"%1\"").arg(value);
    }
    if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("true");
    }
    if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("false");
    }
    bool numberOk = false;
    value.toDouble(&numberOk);
    if (numberOk) {
        return value;
    }
    if (value.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

void writeAdvancedConfigParams(QTextStream& out, const QMap<QString, QString>& values)
{
    for (const AdvancedConfigParam& param : defaultAdvancedConfigParams()) {
        const QString value = values.value(param.key, param.defaultValue);
        out << param.key << ": " << yamlScalar(value, param.quoteValue) << "\n";
    }
}

} // namespace

bool ConfigWriter::writeYaml(const SliceSettings& settings,
                             const QStringList& materialDirs,
                             const QString& configPath,
                             QString* errorMessage)
{
    QFile file(configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    if (!settings.selectedMachineName.isEmpty()) {
        out << "machine_name: \"" << settings.selectedMachineName << "\"\n";
    }
    out << "layer_height: " << settings.layerHeightMm << "\n";
    out << "pixel_size_mm: " << settings.pixelSizeMm << "\n";
    out << "output_width: " << settings.outputWidth << "\n";
    out << "output_height: " << settings.outputHeight << "\n";
    out << "material_num: " << settings.materialCount << "\n";

    for (int i = 0; i < settings.materialCount; ++i) {
        const QString dir = i < materialDirs.size() ? materialDirs[i] : QString();
        out << "img_path_" << i << ": " << yamlPath(dir) << "\n";
    }

    out << "bottom_layers: " << settings.bottomLayers << "\n";
    for (int i = 0; i < settings.materialCount; ++i) {
        const MaterialExposure exposure = i < settings.materials.size()
            ? settings.materials[i]
            : MaterialExposure();
        // The UI works in light strength; convert to the integer machine current
        // the backend / GCode expects, using the selected machine's map.
        const int bottomCurrent = settings.selectedMachine.currentFromStrength(exposure.bottomExposureStrength);
        const int standardCurrent = settings.selectedMachine.currentFromStrength(exposure.standardExposureStrength);

        out << "bottom_exposure_time_" << i << ": " << exposure.bottomExposureTime << "\n";
        out << "bottom_exposure_current_" << i << ": " << bottomCurrent << "\n";
        out << "standard_exposure_time_" << i << ": " << exposure.standardExposureTime << "\n";
        out << "standard_exposure_current_" << i << ": " << standardCurrent << "\n";
        // Also record the light strength for debugging / round-trip.
        out << "bottom_exposure_strength_" << i << ": " << exposure.bottomExposureStrength << "\n";
        out << "standard_exposure_strength_" << i << ": " << exposure.standardExposureStrength << "\n";
        if (!exposure.presetId.isEmpty()) {
            out << "material_preset_" << i << ": " << yamlScalar(exposure.presetId, true) << "\n";
        }
    }

    // Regular options come from the UI.
    out << "groove: " << yamlBool(settings.groove) << "\n";
    out << "slide_0: " << yamlBool(settings.slide0) << "\n";
    out << "slide_1: " << yamlBool(settings.slide1) << "\n";
    out << "slide_2: " << yamlBool(settings.slide2) << "\n";

    // Machine/GCode advanced parameters come from the editable table.
    writeAdvancedConfigParams(out, settings.advancedParams);

    for (int src = 0; src < settings.materialCount; ++src) {
        for (int dst = 0; dst < settings.materialCount; ++dst) {
            if (src == dst) {
                continue;
            }
            out << "m" << src << "Tom" << dst << ": " << yamlBool(true) << "\n";
            out << "m" << src << "Tom" << dst << "_current: 18\n";
            out << "m" << src << "Tom" << dst << "_time: 2.0\n";
        }
    }

    return true;
}

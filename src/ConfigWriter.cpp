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

void writeMachineDefaults(QTextStream& out)
{
    out << "back_distance: 5\n";
    out << "max_height: 165\n";
    out << "min_height: -50\n";
    out << "z_acc_h: 100.0\n";
    out << "z_dec_h: -100.0\n";
    out << "z_speed_h: 450.0\n";
    out << "z_acc_l: 50.0\n";
    out << "z_dec_l: -50.0\n";
    out << "z_speed_l: 100.0\n";
    out << "r_acc: 60.0\n";
    out << "r_dec: -60.0\n";
    out << "r_speed: 120.0\n";
    out << "clean_tank: 3\n";
    out << "clean_tank_height: 20\n";
    out << "clean_height: 25\n";
    out << "clean_height_coefficient: 0.95\n";
    out << "clean_times: 3\n";
    out << "clean_time: 5.0\n";
    out << "clean_distance: 10\n";
    out << "dry_tank: 4\n";
    out << "dry_height: 60\n";
    out << "dry_time_bottom: 210\n";
    out << "dry_time_standard: 200\n";
    out << "pre_z_height: 10\n";
    out << "drop_time_bottom: 20\n";
    out << "drop_layers_bottom: 20\n";
    out << "drop_time_standard: 10\n";
    out << "ASS: false\n";
    out << "ASS_times: 100\n";
    out << "ASS_volume: 470\n";
    out << "AMS: false\n";
    out << "AMS0_layers: \"-1 -1\"\n";
    out << "AMS1_layers: \"-1 -1\"\n";
    out << "AMS2_layers: \"-1 -1\"\n";
    out << "AMS_tank_offset: 2.5\n";
    out << "AMS_volume: 30\n";
    out << "plate_rotate_height: 140\n";
    out << "glass_rotate_height: -50\n";
    out << "lapse: false\n";
    out << "lapse_height: 160\n";
    out << "lapse_tank: 0\n";
    out << "groove: false\n";
    out << "alpha: false\n";
    out << "beta: false\n";
    out << "block_size: 16\n";
    out << "wait_after_exposure: 0.5\n";
    out << "wait_before_exposure: 0.5\n";
    out << "finished_clean: false\n";
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
        out << "bottom_exposure_time_" << i << ": " << exposure.bottomExposureTime << "\n";
        out << "bottom_exposure_current_" << i << ": " << exposure.bottomExposureCurrent << "\n";
        out << "standard_exposure_time_" << i << ": " << exposure.standardExposureTime << "\n";
        out << "standard_exposure_current_" << i << ": " << exposure.standardExposureCurrent << "\n";
    }

    writeMachineDefaults(out);

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

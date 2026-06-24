#pragma once

#include "ModelTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct SliceExportStats {
    int layerCount = 0;
    QVector<int> materialImageCounts;
};

class SliceExporter {
public:
    static bool exportSlices(const QVector<ModelInstance>& models,
                             const SliceSettings& settings,
                             const QString& outputRoot,
                             QStringList* materialDirs,
                             SliceExportStats* stats,
                             QString* errorMessage);
};

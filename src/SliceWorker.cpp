#include "SliceWorker.h"

#include "ConfigWriter.h"
#include "SliceExporter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

SliceWorker::SliceWorker(QVector<ModelInstance> models, SliceSettings settings, QObject* parent)
    : QObject(parent)
    , m_models(std::move(models))
    , m_settings(std::move(settings))
{
}

void SliceWorker::process()
{
    const QString outputDir = m_settings.outputDir;

    emit logMessage(QStringLiteral("Exporting STL slices..."));

    QString error;
    QStringList materialDirs;
    SliceExportStats stats;
    if (!SliceExporter::exportSlices(m_models, m_settings, outputDir, &materialDirs, &stats, &error)) {
        emit finished(false, QStringLiteral("Slice export failed: %1").arg(error), outputDir);
        return;
    }
    emit logMessage(QStringLiteral("Wrote %1 layers per material").arg(stats.layerCount));

    const QString configPath = QDir(outputDir).absoluteFilePath(QStringLiteral("config.yaml"));
    if (!ConfigWriter::writeYaml(m_settings, materialDirs, configPath, &error)) {
        emit finished(false, QStringLiteral("Config failed: %1").arg(error), outputDir);
        return;
    }
    emit logMessage(QStringLiteral("Wrote config: %1").arg(configPath));

    const QString mergedDir = QDir(outputDir).absoluteFilePath(QStringLiteral("merged"));
    if (!QDir().mkpath(mergedDir)) {
        emit finished(false, QStringLiteral("Cannot create merged folder: %1").arg(mergedDir), outputDir);
        return;
    }

    if (!QFileInfo::exists(m_settings.mergeScriptPath)) {
        emit finished(false, QStringLiteral("Cannot find merge script: %1").arg(m_settings.mergeScriptPath), outputDir);
        return;
    }

    emit logMessage(QStringLiteral("Running merge script..."));

    QProcess process;
    process.setProgram(m_settings.pythonPath);
    process.setArguments(QStringList()
                         << m_settings.mergeScriptPath
                         << QStringLiteral("--config") << configPath
                         << QStringLiteral("--output") << mergedDir);
    process.start();
    if (!process.waitForStarted()) {
        emit finished(false, QStringLiteral("Python failed: %1").arg(process.errorString()), outputDir);
        return;
    }
    process.waitForFinished(-1);

    const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (!stdoutText.isEmpty()) {
        emit logMessage(stdoutText);
    }
    if (!stderrText.isEmpty()) {
        emit logMessage(stderrText);
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        emit finished(false, stderrText.isEmpty() ? stdoutText : stderrText, outputDir);
        return;
    }

    emit finished(true, mergedDir, mergedDir);
}

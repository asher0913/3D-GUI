#include "MainWindow.h"

#include "SliceWorker.h"
#include "ui_MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QMessageBox>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidget>
#include <QThread>
#include <QTimer>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->glView->setModels(&m_models);
    const QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    ui->outputPathEdit->setText(QDir(homeDir).absoluteFilePath(QStringLiteral("MultiMaterialSlicerOutput")));
    ui->pythonPathEdit->setText(defaultPythonPath());
    ui->scriptPathEdit->setText(defaultScriptPath());

    ui->importButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    ui->removeButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    ui->browseOutputButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    ui->browseScriptButton->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    ui->runWorkflowButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    ui->projectorWidthSpin->setMinimum(1);
    ui->projectorHeightSpin->setMinimum(1);
    updateMaterialCombo();
    rebuildMaterialTable();
    loadSelectedModelToControls();

    connect(ui->importButton, &QPushButton::clicked, this, &MainWindow::importModels);
    connect(ui->removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedModel);
    connect(ui->modelList, &QListWidget::currentRowChanged, this, &MainWindow::selectedModelChanged);
    connect(ui->materialCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::materialCountChanged);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::browseOutputDir);
    connect(ui->browseScriptButton, &QPushButton::clicked, this, &MainWindow::browseMergeScript);
    connect(ui->runWorkflowButton, &QPushButton::clicked, this, &MainWindow::runWorkflow);

    connect(ui->materialCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::selectedModelEdited);
    connect(ui->posXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::selectedModelEdited);
    connect(ui->posYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::selectedModelEdited);
    connect(ui->posZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::selectedModelEdited);
    connect(ui->rotXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::selectedModelEdited);
    connect(ui->rotYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::selectedModelEdited);
    connect(ui->rotZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::selectedModelEdited);
    connect(ui->scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::selectedModelEdited);

    auto updatePlate = [this]() {
        SliceSettings settings = collectSettings();
        ui->glView->setBuildPlateSize(settings.outputWidth * settings.pixelSizeMm,
                                      settings.outputHeight * settings.pixelSizeMm);
    };
    connect(ui->projectorWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, updatePlate);
    connect(ui->projectorHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, updatePlate);
    connect(ui->pixelSizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updatePlate);
    updatePlate();

    statusBar()->showMessage(QStringLiteral("Ready"));

    QStringList startupStlFiles;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].endsWith(QStringLiteral(".stl"), Qt::CaseInsensitive)) {
            startupStlFiles.append(args[i]);
        } else if (args[i] == QStringLiteral("--selftest")) {
            m_selfTest = true;
        }
    }
    if (!startupStlFiles.isEmpty()) {
        const bool selfTest = m_selfTest;
        QTimer::singleShot(0, this, [this, startupStlFiles, selfTest]() {
            importModelFiles(startupStlFiles);
            if (selfTest) {
                runSelfTest();
            }
        });
    }
}

// Headless verification of the real export path: configures distinct per-material
// exposures, assigns each model to its own material, then triggers the exact same
// runWorkflow() the GUI button uses (background thread + SliceWorker + workerFinished).
void MainWindow::runSelfTest()
{
    if (m_models.size() < 2) {
        qWarning("selftest: need at least 2 models");
        QCoreApplication::exit(20);
        return;
    }

    ui->materialCountSpin->setValue(2);          // rebuilds the exposure table
    m_models[0].materialIndex = 0;
    m_models[1].materialIndex = 1;
    m_models[0].color = materialColor(0);
    m_models[1].color = materialColor(1);

    // Distinct exposure values so the resulting run.gcode can be checked.
    QTableWidget* table = ui->materialTable;
    if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(0, 1))) s->setValue(8.0);
    if (auto* s = qobject_cast<QSpinBox*>(table->cellWidget(0, 2)))       s->setValue(20);
    if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(0, 3))) s->setValue(3.0);
    if (auto* s = qobject_cast<QSpinBox*>(table->cellWidget(0, 4)))       s->setValue(21);
    if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(1, 1))) s->setValue(5.5);
    if (auto* s = qobject_cast<QSpinBox*>(table->cellWidget(1, 2)))       s->setValue(12);
    if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(1, 3))) s->setValue(1.8);
    if (auto* s = qobject_cast<QSpinBox*>(table->cellWidget(1, 4)))       s->setValue(13);

    ui->outputPathEdit->setText(QStringLiteral("/tmp/selftest_out"));

    refreshModelList();
    qInfo("selftest: configured 2 materials, triggering runWorkflow()");
    runWorkflow();
}

MainWindow::~MainWindow()
{
    // If the user quits while an export is still running, stop the worker thread
    // cleanly instead of letting it be destroyed mid-run.
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete ui;
}

QString MainWindow::defaultScriptPath() const
{
    const QString fromCwd = QDir::current().absoluteFilePath(QStringLiteral("slice_1080p.py"));
    if (QFileInfo::exists(fromCwd)) {
        return fromCwd;
    }
    return QCoreApplication::applicationDirPath() + QStringLiteral("/slice_1080p.py");
}

QString MainWindow::defaultPythonPath() const
{
#ifdef Q_OS_WIN
    const QString bundledPython = QCoreApplication::applicationDirPath() + QStringLiteral("/python/python.exe");
    if (QFileInfo::exists(bundledPython)) {
        return bundledPython;
    }
    return QStringLiteral("python");
#else
    const QStringList candidates {
        QStringLiteral("/opt/homebrew/opt/python@3.12/bin/python3.12"),
        QStringLiteral("/opt/homebrew/bin/python3"),
        QStringLiteral("/usr/local/bin/python3"),
        QStringLiteral("/usr/bin/python3"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QStringLiteral("python3");
#endif
}

QColor MainWindow::materialColor(int materialIndex) const
{
    static const QVector<QColor> colors {
        QColor(44, 160, 220),
        QColor(226, 78, 116),
        QColor(236, 170, 58),
        QColor(75, 174, 117),
        QColor(138, 108, 220),
        QColor(230, 112, 64),
        QColor(74, 190, 190),
        QColor(190, 190, 96),
    };
    return colors[materialIndex % colors.size()];
}

void MainWindow::updateMaterialCombo()
{
    const int count = ui->materialCountSpin->value();
    const QSignalBlocker blocker(ui->materialCombo);
    ui->materialCombo->clear();
    for (int i = 0; i < count; ++i) {
        ui->materialCombo->addItem(QStringLiteral("Material %1").arg(i), i);
    }
}

int MainWindow::selectedModelIndex() const
{
    const int row = ui->modelList->currentRow();
    if (row < 0 || row >= m_models.size()) {
        return -1;
    }
    return row;
}

void MainWindow::importModels()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("Import STL"),
        QDir::homePath(),
        QStringLiteral("STL files (*.stl)"));

    if (paths.isEmpty()) {
        return;
    }

    importModelFiles(paths);
}

int MainWindow::importModelFiles(const QStringList& paths)
{
    int imported = 0;
    for (const QString& path : paths) {
        QString error;
        StlMesh mesh;
        if (!mesh.load(path, &error)) {
            appendLog(QStringLiteral("Failed to load %1: %2").arg(path, error));
            continue;
        }

        mesh.normalizeToBuildPlateOrigin();

        ModelInstance model;
        model.filePath = path;
        model.name = QFileInfo(path).completeBaseName();
        model.mesh = mesh;
        model.materialIndex = 0;
        model.color = materialColor(model.materialIndex);
        m_models.append(model);
        ++imported;
    }

    refreshModelList();
    if (imported > 0) {
        ui->modelList->setCurrentRow(m_models.size() - 1);
        loadSelectedModelToControls();
        appendLog(QStringLiteral("Imported %1 STL model(s)").arg(imported));
    }
    ui->glView->update();
    return imported;
}

void MainWindow::removeSelectedModel()
{
    const int index = selectedModelIndex();
    if (index < 0) {
        return;
    }
    const QString name = m_models[index].name;
    m_models.removeAt(index);
    refreshModelList();
    ui->modelList->setCurrentRow(std::min(index, m_models.size() - 1));
    appendLog(QStringLiteral("Removed %1").arg(name));
    ui->glView->update();
}

void MainWindow::selectedModelChanged()
{
    loadSelectedModelToControls();
}

void MainWindow::selectedModelEdited()
{
    if (m_loadingSelection) {
        return;
    }

    const int index = selectedModelIndex();
    if (index < 0) {
        return;
    }

    ModelInstance& model = m_models[index];
    model.materialIndex = std::max(0, ui->materialCombo->currentIndex());
    model.color = materialColor(model.materialIndex);
    model.transform.translation = QVector3D(
        static_cast<float>(ui->posXSpin->value()),
        static_cast<float>(ui->posYSpin->value()),
        static_cast<float>(ui->posZSpin->value()));
    model.transform.rotationDeg = QVector3D(
        static_cast<float>(ui->rotXSpin->value()),
        static_cast<float>(ui->rotYSpin->value()),
        static_cast<float>(ui->rotZSpin->value()));
    model.transform.scale = static_cast<float>(ui->scaleSpin->value());

    refreshModelList();
    ui->modelList->setCurrentRow(index);
    ui->glView->update();
}

void MainWindow::materialCountChanged()
{
    updateMaterialCombo();
    rebuildMaterialTable();
    const int maxMaterial = ui->materialCountSpin->value() - 1;
    for (ModelInstance& model : m_models) {
        model.materialIndex = std::min(model.materialIndex, maxMaterial);
        model.color = materialColor(model.materialIndex);
    }
    refreshModelList();
    loadSelectedModelToControls();
    ui->glView->update();
}

void MainWindow::rebuildMaterialTable()
{
    QTableWidget* table = ui->materialTable;
    const int count = ui->materialCountSpin->value();

    // Preserve any values the user already entered before resizing the table.
    QVector<MaterialExposure> previous;
    previous.reserve(table->rowCount());
    for (int row = 0; row < table->rowCount(); ++row) {
        MaterialExposure exposure;
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 1))) {
            exposure.bottomExposureTime = spin->value();
        }
        if (auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(row, 2))) {
            exposure.bottomExposureCurrent = spin->value();
        }
        if (auto* spin = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 3))) {
            exposure.standardExposureTime = spin->value();
        }
        if (auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(row, 4))) {
            exposure.standardExposureCurrent = spin->value();
        }
        previous.append(exposure);
    }

    table->clear();
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(QStringList()
                                     << QStringLiteral("材料")
                                     << QStringLiteral("底层时间 s")
                                     << QStringLiteral("底层电流")
                                     << QStringLiteral("普通时间 s")
                                     << QStringLiteral("普通电流"));
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setRowCount(count);

    for (int row = 0; row < count; ++row) {
        const MaterialExposure exposure = row < previous.size() ? previous[row] : MaterialExposure();

        auto* label = new QTableWidgetItem(QStringLiteral("Material %1").arg(row));
        label->setFlags(Qt::ItemIsEnabled);
        table->setItem(row, 0, label);

        auto* bottomTime = new QDoubleSpinBox(table);
        bottomTime->setDecimals(2);
        bottomTime->setMaximum(999.0);
        bottomTime->setValue(exposure.bottomExposureTime);
        table->setCellWidget(row, 1, bottomTime);

        auto* bottomCurrent = new QSpinBox(table);
        bottomCurrent->setMaximum(255);
        bottomCurrent->setValue(exposure.bottomExposureCurrent);
        table->setCellWidget(row, 2, bottomCurrent);

        auto* standardTime = new QDoubleSpinBox(table);
        standardTime->setDecimals(2);
        standardTime->setMaximum(999.0);
        standardTime->setValue(exposure.standardExposureTime);
        table->setCellWidget(row, 3, standardTime);

        auto* standardCurrent = new QSpinBox(table);
        standardCurrent->setMaximum(255);
        standardCurrent->setValue(exposure.standardExposureCurrent);
        table->setCellWidget(row, 4, standardCurrent);
    }
}

void MainWindow::browseOutputDir()
{
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select output folder"),
        ui->outputPathEdit->text().isEmpty() ? QDir::currentPath() : ui->outputPathEdit->text());
    if (!path.isEmpty()) {
        ui->outputPathEdit->setText(path);
    }
}

void MainWindow::browseMergeScript()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select merge script"),
        QFileInfo(ui->scriptPathEdit->text()).absolutePath(),
        QStringLiteral("Python files (*.py)"));
    if (!path.isEmpty()) {
        ui->scriptPathEdit->setText(path);
    }
}

SliceSettings MainWindow::collectSettings() const
{
    SliceSettings settings;
    settings.outputWidth = ui->projectorWidthSpin->value();
    settings.outputHeight = ui->projectorHeightSpin->value();
    settings.pixelSizeMm = ui->pixelSizeSpin->value();
    settings.layerHeightMm = ui->layerHeightSpin->value();
    settings.materialCount = ui->materialCountSpin->value();
    settings.bottomLayers = ui->bottomLayersSpin->value();

    QTableWidget* table = ui->materialTable;
    settings.materials.reserve(settings.materialCount);
    for (int i = 0; i < settings.materialCount; ++i) {
        MaterialExposure exposure;
        if (i < table->rowCount()) {
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(table->cellWidget(i, 1))) {
                exposure.bottomExposureTime = spin->value();
            }
            if (auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(i, 2))) {
                exposure.bottomExposureCurrent = spin->value();
            }
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(table->cellWidget(i, 3))) {
                exposure.standardExposureTime = spin->value();
            }
            if (auto* spin = qobject_cast<QSpinBox*>(table->cellWidget(i, 4))) {
                exposure.standardExposureCurrent = spin->value();
            }
        }
        settings.materials.append(exposure);
    }

    settings.outputDir = ui->outputPathEdit->text();
    settings.pythonPath = ui->pythonPathEdit->text().trimmed();
    settings.mergeScriptPath = ui->scriptPathEdit->text();
    return settings;
}

void MainWindow::refreshModelList()
{
    const int current = ui->modelList->currentRow();
    const QSignalBlocker blocker(ui->modelList);
    ui->modelList->clear();
    for (const ModelInstance& model : m_models) {
        ui->modelList->addItem(QStringLiteral("%1  [M%2]").arg(model.name).arg(model.materialIndex));
    }
    if (!m_models.isEmpty()) {
        ui->modelList->setCurrentRow(std::max(0, std::min(current, m_models.size() - 1)));
    }
}

void MainWindow::loadSelectedModelToControls()
{
    const int index = selectedModelIndex();
    ui->transformGroup->setEnabled(index >= 0);
    ui->glView->setSelectedIndex(index);

    m_loadingSelection = true;
    if (index >= 0) {
        const ModelInstance& model = m_models[index];
        ui->materialCombo->setCurrentIndex(std::min(model.materialIndex, ui->materialCombo->count() - 1));
        ui->posXSpin->setValue(model.transform.translation.x());
        ui->posYSpin->setValue(model.transform.translation.y());
        ui->posZSpin->setValue(model.transform.translation.z());
        ui->rotXSpin->setValue(model.transform.rotationDeg.x());
        ui->rotYSpin->setValue(model.transform.rotationDeg.y());
        ui->rotZSpin->setValue(model.transform.rotationDeg.z());
        ui->scaleSpin->setValue(model.transform.scale);
    } else {
        ui->materialCombo->setCurrentIndex(-1);
        ui->posXSpin->setValue(0.0);
        ui->posYSpin->setValue(0.0);
        ui->posZSpin->setValue(0.0);
        ui->rotXSpin->setValue(0.0);
        ui->rotYSpin->setValue(0.0);
        ui->rotZSpin->setValue(0.0);
        ui->scaleSpin->setValue(1.0);
    }
    m_loadingSelection = false;
}

void MainWindow::appendLog(const QString& text)
{
    const QString prefix = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(prefix, text));
}

void MainWindow::setRunningState(bool running)
{
    ui->runWorkflowButton->setEnabled(!running);
    ui->importButton->setEnabled(!running);
    ui->removeButton->setEnabled(!running);
    if (running) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
}

void MainWindow::runWorkflow()
{
    if (m_workerThread) {
        return; // already running
    }

    if (m_models.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No models"), QStringLiteral("Please import at least one STL model."));
        return;
    }

    SliceSettings settings = collectSettings();
    if (settings.outputDir.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No output"), QStringLiteral("Please choose an output folder."));
        return;
    }
    if (settings.pythonPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No Python"), QStringLiteral("Please set the Python executable."));
        return;
    }
    if (!QFileInfo::exists(settings.mergeScriptPath)) {
        QMessageBox::warning(this, QStringLiteral("Missing script"), QStringLiteral("Cannot find slice_1080p.py."));
        return;
    }

    setRunningState(true);
    statusBar()->showMessage(QStringLiteral("Running..."));

    // The worker owns copies of the models and settings, so the main thread can keep
    // mutating its own data while the export runs in the background.
    auto* thread = new QThread(this);
    auto* worker = new SliceWorker(m_models, settings);
    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &SliceWorker::process);
    connect(worker, &SliceWorker::logMessage, this, &MainWindow::appendLog);
    connect(worker, &SliceWorker::finished, this, &MainWindow::workerFinished);
    connect(worker, &SliceWorker::finished, thread, &QThread::quit);
    connect(worker, &SliceWorker::finished, worker, &SliceWorker::deleteLater);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    m_workerThread = thread;
    thread->start();
}

void MainWindow::workerFinished(bool success, const QString& summary, const QString& outputDir)
{
    QThread* finishedThread = m_workerThread;
    m_workerThread = nullptr;
    setRunningState(false);

    if (m_selfTest) {
        if (success) {
            qInfo("selftest: SUCCESS output=%s", qUtf8Printable(outputDir));
        } else {
            qWarning("selftest: FAILED %s", qUtf8Printable(summary));
        }
        // Let the worker thread's event loop finish before tearing down, so the
        // QThread isn't destroyed while still running.
        if (finishedThread) {
            finishedThread->quit();
            finishedThread->wait();
        }
        QCoreApplication::exit(success ? 0 : 1);
        return;
    }

    if (success) {
        appendLog(QStringLiteral("Done. Output: %1").arg(outputDir));
        statusBar()->showMessage(QStringLiteral("Done"));
        QMessageBox::information(this, QStringLiteral("Complete"),
                                 QStringLiteral("Slices, config.yaml and run.gcode were generated."));
    } else {
        appendLog(QStringLiteral("Failed: %1").arg(summary));
        statusBar()->showMessage(QStringLiteral("Failed"));
        QMessageBox::critical(this, QStringLiteral("Workflow failed"), summary);
    }
}

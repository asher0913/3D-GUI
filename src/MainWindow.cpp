#include "MainWindow.h"

#include "PresetLoader.h"
#include "SliceWorker.h"
#include "ui_MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
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

#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Parse command-line arguments first so config handling knows about self-test.
    QStringList startupStlFiles;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].endsWith(QStringLiteral(".stl"), Qt::CaseInsensitive)) {
            startupStlFiles.append(args[i]);
        } else if (args[i] == QStringLiteral("--selftest")) {
            m_selfTest = true;
        }
    }

    ui->glView->setModels(&m_models);
    const QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    ui->outputPathEdit->setText(QDir(homeDir).absoluteFilePath(QStringLiteral("MultiMaterialSlicerOutput")));

    ui->importButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    ui->copyButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    ui->removeButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    ui->browseOutputButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    ui->runWorkflowButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    ui->projectorWidthSpin->setMinimum(1);
    ui->projectorHeightSpin->setMinimum(1);

    // Advanced options collapse with the group's check state.
    ui->advancedContent->setVisible(ui->advancedGroup->isChecked());
    connect(ui->advancedGroup, &QGroupBox::toggled, ui->advancedContent, &QWidget::setVisible);

    // Connections.
    connect(ui->importButton, &QPushButton::clicked, this, &MainWindow::importModels);
    connect(ui->copyButton, &QPushButton::clicked, this, &MainWindow::duplicateSelectedModel);
    connect(ui->removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedModel);
    connect(ui->modelList, &QListWidget::currentRowChanged, this, &MainWindow::selectedModelChanged);
    connect(ui->materialCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::materialCountChanged);
    connect(ui->machineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::machineChanged);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::browseOutputDir);
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
        ui->glView->setBuildPlateSize(ui->projectorWidthSpin->value() * ui->pixelSizeSpin->value(),
                                      ui->projectorHeightSpin->value() * ui->pixelSizeSpin->value());
    };
    connect(ui->projectorWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, updatePlate);
    connect(ui->projectorHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, updatePlate);
    connect(ui->pixelSizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updatePlate);

    // Load the machine/material preset library.
    bool ok = loadPresetLibrary();
    if (!ok && !m_selfTest) {
        ok = promptForConfig();
    }

    if (ok) {
        m_configReady = true;
        populateMachineCombo();
        if (!m_presets.machines.isEmpty()) {
            applyMachineToUi(m_presets.machines.first());
        }
        setConfigDependentEnabled(true);
        appendLog(QStringLiteral("已加载配置: %1").arg(m_presets.sourcePath));
        appendLog(QStringLiteral("机器: %1 个，材料: %2 个")
                      .arg(m_presets.machines.size())
                      .arg(m_presets.materials.size()));
    } else {
        m_configReady = false;
        updateMaterialCombo();
        rebuildMaterialTable();
        setConfigDependentEnabled(false);
        appendLog(QStringLiteral("缺少机器与材料配置文件，软件无法正常使用。请重新启动并选择配置文件。"));
        statusBar()->showMessage(QStringLiteral("缺少配置文件，功能受限"));
    }

    loadSelectedModelToControls();
    updatePlate();

    if (m_configReady) {
        statusBar()->showMessage(QStringLiteral("Ready"));
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

MainWindow::~MainWindow()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete ui;
}

// ---------------------------------------------------------------------------
// Preset library
// ---------------------------------------------------------------------------

QStringList MainWindow::candidateConfigPaths() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();
    // On macOS the bundle stores resources in Contents/Resources (one level up
    // from Contents/MacOS where the executable lives).
    const QString resourcesDir = QDir(appDir).absoluteFilePath(QStringLiteral("../Resources"));

    QStringList paths;
    paths << QDir(resourcesDir).absoluteFilePath(QStringLiteral("machine_material_presets.yaml"));
    paths << QDir(appDir).absoluteFilePath(QStringLiteral("machine_material_presets.yaml"));
    paths << QDir(appDir).absoluteFilePath(QStringLiteral("machine_material_presets.yml"));
    paths << QDir(appDir).absoluteFilePath(QStringLiteral("config/machine_material_presets.yaml"));

    // Any .yaml / .yml sitting next to the executable or in Resources.
    const QStringList nameFilters { QStringLiteral("*.yaml"), QStringLiteral("*.yml") };
    for (const QString& f : QDir(resourcesDir).entryList(nameFilters, QDir::Files, QDir::Name)) {
        paths << QDir(resourcesDir).absoluteFilePath(f);
    }
    for (const QString& f : QDir(appDir).entryList(nameFilters, QDir::Files, QDir::Name)) {
        paths << QDir(appDir).absoluteFilePath(f);
    }

    // Development locations (running from the source tree / build dir).
    paths << QDir(cwd).absoluteFilePath(QStringLiteral("config/machine_material_presets.yaml"));
    paths << QDir(cwd).absoluteFilePath(QStringLiteral("machine_material_presets.yaml"));
    paths << QDir(cwd).absoluteFilePath(QStringLiteral("配置参数示例.txt"));
    // Walk up a few levels from the executable to find a project-root config.
    QDir up(appDir);
    for (int i = 0; i < 5; ++i) {
        if (!up.cdUp()) {
            break;
        }
        paths << up.absoluteFilePath(QStringLiteral("config/machine_material_presets.yaml"));
        paths << up.absoluteFilePath(QStringLiteral("配置参数示例.txt"));
    }

    paths.removeDuplicates();
    return paths;
}

bool MainWindow::loadPresetLibrary()
{
    for (const QString& path : candidateConfigPaths()) {
        if (!QFileInfo::exists(path)) {
            continue;
        }
        PresetLibrary lib;
        QString error;
        if (PresetLoader::load(path, &lib, &error)) {
            m_presets = lib;
            return true;
        }
    }
    return false;
}

bool MainWindow::promptForConfig()
{
    QMessageBox::warning(this,
                         QStringLiteral("缺少配置文件"),
                         QStringLiteral("未找到机器与材料配置文件，软件无法继续正常使用。\n请选择一个配置文件（.yaml / .yml / .txt）。"));
    while (true) {
        const QString path = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择机器/材料配置文件"),
            QDir::homePath(),
            QStringLiteral("配置文件 (*.yaml *.yml *.txt);;所有文件 (*)"));
        if (path.isEmpty()) {
            return false;  // user cancelled
        }
        PresetLibrary lib;
        QString error;
        if (PresetLoader::load(path, &lib, &error)) {
            m_presets = lib;
            return true;
        }
        QMessageBox::critical(this, QStringLiteral("配置解析失败"),
                              QStringLiteral("无法解析所选配置文件：\n%1").arg(error));
    }
}

void MainWindow::populateMachineCombo()
{
    const QSignalBlocker blocker(ui->machineCombo);
    ui->machineCombo->clear();
    for (const MachinePreset& m : m_presets.machines) {
        ui->machineCombo->addItem(m.displayName.isEmpty() ? m.id : m.displayName, m.id);
    }
    if (!m_presets.machines.isEmpty()) {
        ui->machineCombo->setCurrentIndex(0);
    }
}

const MachinePreset* MainWindow::currentMachine() const
{
    const int index = ui->machineCombo->currentIndex();
    if (index < 0 || index >= m_presets.machines.size()) {
        return nullptr;
    }
    return &m_presets.machines[index];
}

void MainWindow::applyMachineToUi(const MachinePreset& machine)
{
    {
        const QSignalBlocker b1(ui->materialCountSpin);
        const QSignalBlocker b2(ui->projectorWidthSpin);
        const QSignalBlocker b3(ui->projectorHeightSpin);
        const QSignalBlocker b4(ui->pixelSizeSpin);

        ui->projectorWidthSpin->setValue(machine.projectorWidth);
        ui->projectorHeightSpin->setValue(machine.projectorHeight);
        ui->pixelSizeSpin->setValue(machine.pixelWidthMm);

        const int limit = std::max(1, machine.materialNumLimit);
        ui->materialCountSpin->setMaximum(limit);
        if (ui->materialCountSpin->value() > limit) {
            ui->materialCountSpin->setValue(limit);
            appendLog(QStringLiteral("材料数量超过机器上限 %1，已自动降到上限。").arg(limit));
        }
    }

    updateMaterialCombo();
    rebuildMaterialTable();

    const int maxMaterial = ui->materialCountSpin->value() - 1;
    for (ModelInstance& model : m_models) {
        model.materialIndex = std::min(model.materialIndex, std::max(0, maxMaterial));
        model.color = materialColorForSlot(model.materialIndex);
    }
    refreshModelList();
    ui->glView->setBuildPlateSize(ui->projectorWidthSpin->value() * ui->pixelSizeSpin->value(),
                                  ui->projectorHeightSpin->value() * ui->pixelSizeSpin->value());
    ui->glView->update();
}

void MainWindow::setConfigDependentEnabled(bool enabled)
{
    ui->importButton->setEnabled(enabled);
    ui->copyButton->setEnabled(false);  // also needs a selection
    ui->removeButton->setEnabled(false);
    ui->runWorkflowButton->setEnabled(enabled);
    ui->machineCombo->setEnabled(enabled);
    ui->materialCountSpin->setEnabled(enabled);
    ui->materialTable->setEnabled(enabled);
    ui->transformGroup->setEnabled(false);  // needs a selection
    ui->advancedGroup->setEnabled(enabled);
    ui->printSettingsGroup->setEnabled(enabled);
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

QString MainWindow::defaultMergeToolPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QStringList candidates {
        appDir + QStringLiteral("/slice_merge_tool.exe"),
        appDir + QStringLiteral("/slice_1080p_tool.exe"),
        appDir + QStringLiteral("/slice_1080p.py"),
    };
#else
    const QStringList candidates {
        appDir + QStringLiteral("/slice_merge_tool"),
        appDir + QStringLiteral("/../Resources/slice_merge_tool"),
        appDir + QStringLiteral("/slice_1080p_tool"),
        appDir + QStringLiteral("/../Resources/slice_1080p.py"),
        appDir + QStringLiteral("/slice_1080p.py"),
        QDir::current().absoluteFilePath(QStringLiteral("slice_1080p.py")),
    };
#endif
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) {
            return c;
        }
    }
    return candidates.first();
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

QColor MainWindow::materialColorForSlot(int slot) const
{
    QTableWidget* table = ui->materialTable;
    if (slot >= 0 && slot < table->rowCount()) {
        if (auto* combo = qobject_cast<QComboBox*>(table->cellWidget(slot, 0))) {
            const QString presetId = combo->currentData().toString();
            if (const MaterialPreset* p = m_presets.materialById(presetId)) {
                if (p->color.isValid()) {
                    return p->color;
                }
            }
        }
    }
    return materialColor(std::max(0, slot));
}

// ---------------------------------------------------------------------------
// Material combo (per-model material slot) and table
// ---------------------------------------------------------------------------

void MainWindow::updateMaterialCombo()
{
    const int count = ui->materialCountSpin->value();
    const QSignalBlocker blocker(ui->materialCombo);
    const int previous = ui->materialCombo->currentIndex();
    ui->materialCombo->clear();
    QTableWidget* table = ui->materialTable;
    for (int i = 0; i < count; ++i) {
        QString presetName;
        if (i < table->rowCount()) {
            if (auto* combo = qobject_cast<QComboBox*>(table->cellWidget(i, 0))) {
                presetName = combo->currentText();
            }
        }
        const QString label = presetName.isEmpty()
            ? QStringLiteral("材料%1").arg(i + 1)
            : QStringLiteral("材料%1 - %2").arg(i + 1).arg(presetName);
        ui->materialCombo->addItem(label, i);
    }
    if (previous >= 0 && previous < count) {
        ui->materialCombo->setCurrentIndex(previous);
    }
}

void MainWindow::populateMaterialPresetCombo(QComboBox* combo) const
{
    combo->clear();
    for (const MaterialPreset& m : m_presets.materials) {
        combo->addItem(m.displayName.isEmpty() ? m.id : m.displayName, m.id);
    }
}

void MainWindow::rebuildMaterialTable()
{
    QTableWidget* table = ui->materialTable;
    const int count = ui->materialCountSpin->value();

    // Preserve existing values (preset id + four numbers) before resizing.
    struct RowData {
        QString presetId;
        double bottomTime = 7.0;
        double bottomStrength = 0.0;
        double standardTime = 2.5;
        double standardStrength = 0.0;
    };
    QVector<RowData> previous;
    previous.reserve(table->rowCount());
    for (int row = 0; row < table->rowCount(); ++row) {
        RowData d;
        if (auto* c = qobject_cast<QComboBox*>(table->cellWidget(row, 0))) d.presetId = c->currentData().toString();
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 1))) d.bottomTime = s->value();
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 2))) d.bottomStrength = s->value();
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 3))) d.standardTime = s->value();
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 4))) d.standardStrength = s->value();
        previous.append(d);
    }

    m_rebuildingTable = true;
    table->clear();
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(QStringList()
                                     << QStringLiteral("材料预设")
                                     << QStringLiteral("底层时间 s")
                                     << QStringLiteral("底层光强")
                                     << QStringLiteral("普通时间 s")
                                     << QStringLiteral("普通光强"));
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setRowCount(count);

    auto makeDouble = [table](double value, double maxValue) {
        auto* s = new QDoubleSpinBox(table);
        s->setDecimals(2);
        s->setMaximum(maxValue);
        s->setValue(value);
        return s;
    };

    for (int row = 0; row < count; ++row) {
        const bool hasPrev = row < previous.size();
        const RowData d = hasPrev ? previous[row] : RowData();

        auto* combo = new QComboBox(table);
        populateMaterialPresetCombo(combo);
        // Default selection: keep previous preset, else map slot -> preset by index.
        int presetIndex = -1;
        if (!d.presetId.isEmpty()) {
            presetIndex = combo->findData(d.presetId);
        }
        if (presetIndex < 0 && !m_presets.materials.isEmpty()) {
            presetIndex = row % m_presets.materials.size();
        }
        if (presetIndex >= 0) {
            combo->setCurrentIndex(presetIndex);
        }
        table->setCellWidget(row, 0, combo);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, row]() { applyMaterialPresetToRow(row); });

        table->setCellWidget(row, 1, makeDouble(d.bottomTime, 999.0));
        table->setCellWidget(row, 2, makeDouble(d.bottomStrength, 1000.0));
        table->setCellWidget(row, 3, makeDouble(d.standardTime, 999.0));
        table->setCellWidget(row, 4, makeDouble(d.standardStrength, 1000.0));

        // If there was no preserved value, seed the row from its material preset.
        if (!hasPrev) {
            applyMaterialPresetToRow(row);
        }
    }

    m_rebuildingTable = false;
}

void MainWindow::applyMaterialPresetToRow(int row)
{
    QTableWidget* table = ui->materialTable;
    if (row < 0 || row >= table->rowCount()) {
        return;
    }
    auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, 0));
    if (!combo) {
        return;
    }
    const QString presetId = combo->currentData().toString();
    if (const MaterialPreset* p = m_presets.materialById(presetId)) {
        const MachinePreset* machine = currentMachine();
        double bottomStrength = p->bottomExposureStrength;
        double standardStrength = p->standardExposureStrength;
        if (!p->hasStrength && machine) {
            bottomStrength = machine->strengthFromCurrent(p->legacyBottomExposureCurrent);
            standardStrength = machine->strengthFromCurrent(p->legacyStandardExposureCurrent);
        }
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 1))) s->setValue(p->bottomExposureTime);
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 2))) s->setValue(bottomStrength);
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 3))) s->setValue(p->standardExposureTime);
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 4))) s->setValue(standardStrength);
    }

    if (m_rebuildingTable) {
        return;  // colours/labels refreshed after the whole table is built
    }

    // Refresh model colours that use this slot, plus the per-model material combo labels.
    for (ModelInstance& model : m_models) {
        if (model.materialIndex == row) {
            model.color = materialColorForSlot(row);
        }
    }
    updateMaterialCombo();
    refreshModelList();
    ui->glView->update();
}

// ---------------------------------------------------------------------------
// Model management
// ---------------------------------------------------------------------------

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
        model.color = materialColorForSlot(0);
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

void MainWindow::duplicateSelectedModel()
{
    const int index = selectedModelIndex();
    if (index < 0) {
        return;
    }
    ModelInstance copy = m_models[index];  // deep copy (mesh/transform/material/color)

    const QString base = m_models[index].name;
    auto nameExists = [this](const QString& name) {
        for (const ModelInstance& m : m_models) {
            if (m.name == name) {
                return true;
            }
        }
        return false;
    };
    QString candidate = base + QStringLiteral(" 副本");
    int n = 2;
    while (nameExists(candidate)) {
        candidate = base + QStringLiteral(" 副本 %1").arg(n++);
    }
    copy.name = candidate;

    m_models.append(copy);
    refreshModelList();
    ui->modelList->setCurrentRow(m_models.size() - 1);
    loadSelectedModelToControls();
    appendLog(QStringLiteral("已复制模型: %1").arg(candidate));
    ui->glView->update();
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
    loadSelectedModelToControls();
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
    model.color = materialColorForSlot(model.materialIndex);
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
    updateMaterialCombo();  // labels now include preset names
    const int maxMaterial = ui->materialCountSpin->value() - 1;
    for (ModelInstance& model : m_models) {
        model.materialIndex = std::min(model.materialIndex, std::max(0, maxMaterial));
        model.color = materialColorForSlot(model.materialIndex);
    }
    refreshModelList();
    loadSelectedModelToControls();
    ui->glView->update();
}

void MainWindow::machineChanged()
{
    const MachinePreset* machine = currentMachine();
    if (!machine) {
        return;
    }
    appendLog(QStringLiteral("已选择机器: %1 (%2x%3, 材料上限 %4)")
                  .arg(machine->displayName)
                  .arg(machine->projectorWidth)
                  .arg(machine->projectorHeight)
                  .arg(machine->materialNumLimit));
    applyMachineToUi(*machine);
    loadSelectedModelToControls();
}

// ---------------------------------------------------------------------------
// Output / backend paths
// ---------------------------------------------------------------------------

void MainWindow::browseOutputDir()
{
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择输出目录"),
        ui->outputPathEdit->text().isEmpty() ? QDir::currentPath() : ui->outputPathEdit->text());
    if (!path.isEmpty()) {
        ui->outputPathEdit->setText(path);
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

    if (const MachinePreset* machine = currentMachine()) {
        settings.selectedMachine = *machine;
        settings.selectedMachineName = machine->id;
    }

    QTableWidget* table = ui->materialTable;
    settings.materials.reserve(settings.materialCount);
    for (int i = 0; i < settings.materialCount; ++i) {
        MaterialExposure exposure;
        if (i < table->rowCount()) {
            if (auto* c = qobject_cast<QComboBox*>(table->cellWidget(i, 0))) exposure.presetId = c->currentData().toString();
            if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(i, 1))) exposure.bottomExposureTime = s->value();
            if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(i, 2))) exposure.bottomExposureStrength = s->value();
            if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(i, 3))) exposure.standardExposureTime = s->value();
            if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(i, 4))) exposure.standardExposureStrength = s->value();
        }
        settings.materials.append(exposure);
    }

    settings.groove = ui->grooveCheck->isChecked();
    settings.slide0 = ui->slide0Check->isChecked();
    settings.slide1 = ui->slide1Check->isChecked();
    settings.slide2 = ui->slide2Check->isChecked();

    settings.outputDir = ui->outputPathEdit->text();
    // The backend is embedded in the app (Contents/MacOS/slice_merge_tool, next to
    // the exe on Windows); it is auto-detected so the customer never configures it.
    settings.mergeToolPath = defaultMergeToolPath();
    settings.pythonPath = defaultPythonPath();
    return settings;
}

// ---------------------------------------------------------------------------
// List / selection plumbing
// ---------------------------------------------------------------------------

void MainWindow::refreshModelList()
{
    const int current = ui->modelList->currentRow();
    const QSignalBlocker blocker(ui->modelList);
    ui->modelList->clear();
    for (const ModelInstance& model : m_models) {
        ui->modelList->addItem(QStringLiteral("%1  [材料%2]").arg(model.name).arg(model.materialIndex + 1));
    }
    if (!m_models.isEmpty()) {
        ui->modelList->setCurrentRow(std::max(0, std::min(current, m_models.size() - 1)));
    }
}

void MainWindow::loadSelectedModelToControls()
{
    const int index = selectedModelIndex();
    const bool hasSelection = index >= 0;
    ui->transformGroup->setEnabled(hasSelection && m_configReady);
    ui->copyButton->setEnabled(hasSelection && m_configReady);
    ui->removeButton->setEnabled(hasSelection && m_configReady);
    ui->glView->setSelectedIndex(index);

    m_loadingSelection = true;
    if (hasSelection) {
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
    ui->copyButton->setEnabled(!running && selectedModelIndex() >= 0);
    ui->removeButton->setEnabled(!running && selectedModelIndex() >= 0);
    if (running) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
}

// ---------------------------------------------------------------------------
// Workflow
// ---------------------------------------------------------------------------

void MainWindow::runWorkflow()
{
    if (m_workerThread) {
        return;
    }
    if (!m_configReady) {
        QMessageBox::warning(this, QStringLiteral("缺少配置"),
                             QStringLiteral("尚未加载机器与材料配置文件，无法切片。"));
        return;
    }
    if (m_models.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("没有模型"), QStringLiteral("请先导入至少一个 STL 模型。"));
        return;
    }

    SliceSettings settings = collectSettings();
    if (settings.outputDir.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("没有输出目录"), QStringLiteral("请选择输出目录。"));
        return;
    }
    if (settings.mergeToolPath.isEmpty() || !QFileInfo::exists(settings.mergeToolPath)) {
        QMessageBox::warning(this, QStringLiteral("缺少后端工具"),
                             QStringLiteral("找不到后端工具，请在“后端工具”一栏选择可执行程序或命令行工具。"));
        return;
    }

    setRunningState(true);
    statusBar()->showMessage(QStringLiteral("Running..."));

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
        QMessageBox::information(this, QStringLiteral("完成"),
                                 QStringLiteral("已生成切片、config.yaml 与 run.gcode。"));
    } else {
        appendLog(QStringLiteral("Failed: %1").arg(summary));
        statusBar()->showMessage(QStringLiteral("Failed"));
        QMessageBox::critical(this, QStringLiteral("工作流失败"), summary);
    }
}

// ---------------------------------------------------------------------------
// Self-test (headless verification of the full export path)
// ---------------------------------------------------------------------------

void MainWindow::runSelfTest()
{
    if (m_models.size() < 2) {
        qWarning("selftest: need at least 2 models");
        QCoreApplication::exit(20);
        return;
    }

    ui->materialCountSpin->setValue(2);  // rebuilds the exposure table
    // Override to a small resolution / coarse layers so the self-test runs fast;
    // the strength->current conversion still uses the selected machine's real map.
    ui->projectorWidthSpin->setValue(400);
    ui->projectorHeightSpin->setValue(300);
    ui->pixelSizeSpin->setValue(0.1);
    ui->layerHeightSpin->setValue(0.2);
    // Exercise the advanced options so they show up in config.yaml.
    ui->advancedGroup->setChecked(true);
    ui->grooveCheck->setChecked(true);
    ui->slide0Check->setChecked(true);

    m_models[0].materialIndex = 0;
    m_models[1].materialIndex = 1;
    m_models[0].color = materialColorForSlot(0);
    m_models[1].color = materialColorForSlot(1);

    // Distinct light strengths so the converted currents differ per material.
    QTableWidget* table = ui->materialTable;
    auto setCell = [table](int row, int col, double v) {
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, col))) {
            s->setValue(v);
        }
    };
    setCell(0, 1, 8.0);  setCell(0, 2, 12.1);  // material 0
    setCell(0, 3, 3.0);  setCell(0, 4, 7.2);
    setCell(1, 1, 5.5);  setCell(1, 2, 3.6);   // material 1
    setCell(1, 3, 1.8);  setCell(1, 4, 0.3);

    ui->outputPathEdit->setText(QStringLiteral("/tmp/selftest_out"));

    refreshModelList();
    qInfo("selftest: configured 2 materials, triggering runWorkflow()");
    runWorkflow();
}

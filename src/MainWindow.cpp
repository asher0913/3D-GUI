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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

constexpr int ItemKindRole = Qt::UserRole + 1;
constexpr int ModelIndexRole = Qt::UserRole + 2;
constexpr int AssemblyIdRole = Qt::UserRole + 3;
constexpr int ItemKindModel = 1;
constexpr int ItemKindAssembly = 2;

QString stripInlineComment(const QString& text)
{
    bool inSingle = false;
    bool inDouble = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (ch == '\'' && !inDouble) {
            inSingle = !inSingle;
        } else if (ch == '"' && !inSingle) {
            inDouble = !inDouble;
        } else if (ch == '#' && !inSingle && !inDouble) {
            return text.left(i).trimmed();
        }
    }
    return text.trimmed();
}

QString unquoteConfigValue(QString value)
{
    value = stripInlineComment(value);
    if (value.size() >= 2
        && ((value.startsWith('"') && value.endsWith('"'))
            || (value.startsWith('\'') && value.endsWith('\'')))) {
        value = value.mid(1, value.size() - 2);
    }
    return value.trimmed();
}

bool readFlatConfigYaml(const QString& path, QMap<QString, QString>* values, QString* errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法打开 config 文件: %1").arg(file.errorString());
        }
        return false;
    }

    QTextStream in(&file);
    in.setCodec("UTF-8");
    int parsed = 0;
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        if (raw.startsWith(' ') || raw.startsWith('\t')) {
            continue;  // this importer intentionally handles the flat config.yaml format
        }
        const int colon = trimmed.indexOf(':');
        if (colon < 0) {
            continue;
        }
        const QString key = trimmed.left(colon).trimmed();
        const QString value = unquoteConfigValue(trimmed.mid(colon + 1));
        if (!key.isEmpty()) {
            values->insert(key, value);
            ++parsed;
        }
    }

    if (parsed == 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("文件中没有解析到 key: value 形式的配置项。");
        }
        return false;
    }
    return true;
}

bool configBool(const QMap<QString, QString>& values, const QString& key, bool fallback)
{
    if (!values.contains(key)) {
        return fallback;
    }
    const QString v = values.value(key).trimmed();
    if (v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || v == QStringLiteral("1")
        || v.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0
        || v.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (v.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0
        || v == QStringLiteral("0")
        || v.compare(QStringLiteral("no"), Qt::CaseInsensitive) == 0
        || v.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    return fallback;
}

bool setSpinIfPresent(const QMap<QString, QString>& values, const QString& key, QSpinBox* spin)
{
    if (!values.contains(key) || !spin) {
        return false;
    }
    bool ok = false;
    const int value = values.value(key).toInt(&ok);
    if (ok) {
        spin->setValue(value);
    }
    return ok;
}

bool setDoubleSpinIfPresent(const QMap<QString, QString>& values, const QString& key, QDoubleSpinBox* spin)
{
    if (!values.contains(key) || !spin) {
        return false;
    }
    bool ok = false;
    const double value = values.value(key).toDouble(&ok);
    if (ok) {
        spin->setValue(value);
    }
    return ok;
}

QString firstConfigValue(const QMap<QString, QString>& values, const QStringList& keys)
{
    for (const QString& key : keys) {
        if (values.contains(key)) {
            return values.value(key);
        }
    }
    return QString();
}

Transform transformFromUi(const Ui::MainWindow* ui)
{
    Transform transform;
    transform.translation = QVector3D(
        static_cast<float>(ui->posXSpin->value()),
        static_cast<float>(ui->posYSpin->value()),
        static_cast<float>(ui->posZSpin->value()));
    transform.rotationDeg = QVector3D(
        static_cast<float>(ui->rotXSpin->value()),
        static_cast<float>(ui->rotYSpin->value()),
        static_cast<float>(ui->rotZSpin->value()));
    transform.scale = static_cast<float>(ui->scaleSpin->value());
    return transform;
}

void normalizeMeshesTogether(QVector<ModelInstance>* models)
{
    if (!models || models->isEmpty()) {
        return;
    }

    QVector3D minBounds(std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max());
    QVector3D maxBounds(-std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max(),
                        -std::numeric_limits<float>::max());

    for (const ModelInstance& model : *models) {
        minBounds.setX(std::min(minBounds.x(), model.mesh.minBounds().x()));
        minBounds.setY(std::min(minBounds.y(), model.mesh.minBounds().y()));
        minBounds.setZ(std::min(minBounds.z(), model.mesh.minBounds().z()));
        maxBounds.setX(std::max(maxBounds.x(), model.mesh.maxBounds().x()));
        maxBounds.setY(std::max(maxBounds.y(), model.mesh.maxBounds().y()));
        maxBounds.setZ(std::max(maxBounds.z(), model.mesh.maxBounds().z()));
    }

    const QVector3D offset(
        -(minBounds.x() + maxBounds.x()) * 0.5f,
        -(minBounds.y() + maxBounds.y()) * 0.5f,
        -minBounds.z());
    for (ModelInstance& model : *models) {
        model.mesh.translate(offset);
    }
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Parse command-line arguments first so config handling knows about self-test.
    QStringList startupStlFiles;
    QStringList startupStepFiles;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i].endsWith(QStringLiteral(".stl"), Qt::CaseInsensitive)) {
            startupStlFiles.append(args[i]);
        } else if (args[i].endsWith(QStringLiteral(".step"), Qt::CaseInsensitive)
                   || args[i].endsWith(QStringLiteral(".stp"), Qt::CaseInsensitive)) {
            startupStepFiles.append(args[i]);
        } else if (args[i] == QStringLiteral("--selftest")) {
            m_selfTest = true;
        }
    }

    ui->glView->setModels(&m_models);
    const QString homeDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    ui->outputPathEdit->setText(QDir(homeDir).absoluteFilePath(QStringLiteral("MultiMaterialSlicerOutput")));

    ui->importButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    ui->importStepButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    ui->copyButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    ui->removeButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    ui->browseOutputButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    ui->importConfigButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    ui->runWorkflowButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));

    ui->modelList->setColumnCount(2);
    ui->modelList->setHeaderLabels(QStringList() << QStringLiteral("名称") << QStringLiteral("材料"));
    ui->modelList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->modelList->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    ui->projectorWidthSpin->setMinimum(1);
    ui->projectorHeightSpin->setMinimum(1);
    initializeAdvancedParamTable();

    // Regular options stay expanded so the operator can always see groove/slide controls.
    ui->advancedContent->setVisible(true);

    // Connections.
    connect(ui->importButton, &QPushButton::clicked, this, &MainWindow::importModels);
    connect(ui->importStepButton, &QPushButton::clicked, this, &MainWindow::importStepAssembly);
    connect(ui->copyButton, &QPushButton::clicked, this, &MainWindow::duplicateSelectedModel);
    connect(ui->removeButton, &QPushButton::clicked, this, &MainWindow::removeSelectedModel);
    connect(ui->modelList, &QTreeWidget::currentItemChanged, this, &MainWindow::selectedModelChanged);
    connect(ui->materialCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::materialCountChanged);
    connect(ui->machineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::machineChanged);
    connect(ui->browseOutputButton, &QPushButton::clicked, this, &MainWindow::browseOutputDir);
    connect(ui->importConfigButton, &QPushButton::clicked, this, &MainWindow::importConfigYaml);
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

    if (!startupStlFiles.isEmpty() || !startupStepFiles.isEmpty()) {
        const bool selfTest = m_selfTest;
        QTimer::singleShot(0, this, [this, startupStlFiles, startupStepFiles, selfTest]() {
            importModelFiles(startupStlFiles);
            for (const QString& path : startupStepFiles) {
                importStepAssemblyFile(path);
            }
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
    ui->importStepButton->setEnabled(enabled);
    ui->copyButton->setEnabled(false);  // also needs a selection
    ui->removeButton->setEnabled(false);
    ui->runWorkflowButton->setEnabled(enabled);
    ui->machineCombo->setEnabled(enabled);
    ui->materialCountSpin->setEnabled(enabled);
    ui->materialTable->setEnabled(enabled);
    ui->transformGroup->setEnabled(false);  // needs a selection
    ui->advancedGroup->setEnabled(enabled);
    ui->advancedParamsGroup->setEnabled(enabled);
    ui->printSettingsGroup->setEnabled(enabled);
    ui->importConfigButton->setEnabled(enabled);
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

QString MainWindow::defaultStepToolPath() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QStringList candidates {
        appDir + QStringLiteral("/step_to_stl_parts.exe"),
        appDir + QStringLiteral("/step_to_stl_parts.py"),
        QDir::current().absoluteFilePath(QStringLiteral("tools/step_to_stl_parts.py")),
    };
#else
    const QStringList candidates {
        appDir + QStringLiteral("/step_to_stl_parts"),
        appDir + QStringLiteral("/../Resources/step_to_stl_parts"),
        appDir + QStringLiteral("/../Resources/step_to_stl_parts.py"),
        appDir + QStringLiteral("/step_to_stl_parts.py"),
        QDir::current().absoluteFilePath(QStringLiteral("tools/step_to_stl_parts.py")),
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
// Advanced config parameters and config.yaml import
// ---------------------------------------------------------------------------

void MainWindow::initializeAdvancedParamTable()
{
    QTableWidget* table = ui->advancedParamTable;
    table->clear();
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels(QStringList()
                                     << QStringLiteral("参数")
                                     << QStringLiteral("值"));
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setRowCount(defaultAdvancedConfigParams().size());

    int row = 0;
    for (const AdvancedConfigParam& param : defaultAdvancedConfigParams()) {
        auto* keyItem = new QTableWidgetItem(param.key);
        keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, keyItem);
        table->setItem(row, 1, new QTableWidgetItem(param.defaultValue));
        ++row;
    }
}

QMap<QString, QString> MainWindow::collectAdvancedParamsFromTable() const
{
    QMap<QString, QString> values;
    const QTableWidget* table = ui->advancedParamTable;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* keyItem = table->item(row, 0);
        const QTableWidgetItem* valueItem = table->item(row, 1);
        if (!keyItem) {
            continue;
        }
        const QString key = keyItem->text().trimmed();
        if (key.isEmpty()) {
            continue;
        }
        values.insert(key, valueItem ? valueItem->text().trimmed() : QString());
    }
    return values;
}

void MainWindow::applyAdvancedParamsToTable(const QMap<QString, QString>& values)
{
    QTableWidget* table = ui->advancedParamTable;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* keyItem = table->item(row, 0);
        if (!keyItem) {
            continue;
        }
        const QString key = keyItem->text().trimmed();
        if (!values.contains(key)) {
            continue;
        }
        QTableWidgetItem* valueItem = table->item(row, 1);
        if (!valueItem) {
            valueItem = new QTableWidgetItem();
            table->setItem(row, 1, valueItem);
        }
        valueItem->setText(values.value(key));
    }
}

bool MainWindow::applyConfigYamlFile(const QString& path, QString* errorMessage)
{
    QMap<QString, QString> values;
    if (!readFlatConfigYaml(path, &values, errorMessage)) {
        return false;
    }

    if (values.contains(QStringLiteral("machine_name"))) {
        const QString machineName = values.value(QStringLiteral("machine_name"));
        int index = ui->machineCombo->findData(machineName);
        if (index < 0) {
            index = ui->machineCombo->findText(machineName);
        }
        if (index >= 0) {
            ui->machineCombo->setCurrentIndex(index);
        }
    }

    const QString materialNum = firstConfigValue(values, { QStringLiteral("material_num"), QStringLiteral("material_count") });
    if (!materialNum.isEmpty()) {
        bool ok = false;
        const int count = materialNum.toInt(&ok);
        if (ok) {
            ui->materialCountSpin->setValue(std::max(ui->materialCountSpin->minimum(),
                                                     std::min(count, ui->materialCountSpin->maximum())));
        }
    }

    setSpinIfPresent(values, QStringLiteral("output_width"), ui->projectorWidthSpin)
        || setSpinIfPresent(values, QStringLiteral("res_width"), ui->projectorWidthSpin)
        || setSpinIfPresent(values, QStringLiteral("projector_width"), ui->projectorWidthSpin);
    setSpinIfPresent(values, QStringLiteral("output_height"), ui->projectorHeightSpin)
        || setSpinIfPresent(values, QStringLiteral("res_height"), ui->projectorHeightSpin)
        || setSpinIfPresent(values, QStringLiteral("projector_height"), ui->projectorHeightSpin);
    setDoubleSpinIfPresent(values, QStringLiteral("pixel_size_mm"), ui->pixelSizeSpin);
    setDoubleSpinIfPresent(values, QStringLiteral("layer_height"), ui->layerHeightSpin);
    setSpinIfPresent(values, QStringLiteral("bottom_layers"), ui->bottomLayersSpin);

    if (values.contains(QStringLiteral("output_dir"))) {
        ui->outputPathEdit->setText(values.value(QStringLiteral("output_dir")));
    }

    QTableWidget* table = ui->materialTable;
    const MachinePreset* machine = currentMachine();
    for (int row = 0; row < table->rowCount(); ++row) {
        const QString presetKey = QStringLiteral("material_preset_%1").arg(row);
        if (values.contains(presetKey)) {
            if (auto* combo = qobject_cast<QComboBox*>(table->cellWidget(row, 0))) {
                const int presetIndex = combo->findData(values.value(presetKey));
                if (presetIndex >= 0) {
                    combo->setCurrentIndex(presetIndex);
                }
            }
        }

        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 1))) {
            setDoubleSpinIfPresent(values, QStringLiteral("bottom_exposure_time_%1").arg(row), s);
        }
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 2))) {
            if (!setDoubleSpinIfPresent(values, QStringLiteral("bottom_exposure_strength_%1").arg(row), s)
                && machine && values.contains(QStringLiteral("bottom_exposure_current_%1").arg(row))) {
                bool ok = false;
                const int current = values.value(QStringLiteral("bottom_exposure_current_%1").arg(row)).toInt(&ok);
                if (ok) {
                    s->setValue(machine->strengthFromCurrent(current));
                }
            }
        }
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 3))) {
            setDoubleSpinIfPresent(values, QStringLiteral("standard_exposure_time_%1").arg(row), s);
        }
        if (auto* s = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 4))) {
            if (!setDoubleSpinIfPresent(values, QStringLiteral("standard_exposure_strength_%1").arg(row), s)
                && machine && values.contains(QStringLiteral("standard_exposure_current_%1").arg(row))) {
                bool ok = false;
                const int current = values.value(QStringLiteral("standard_exposure_current_%1").arg(row)).toInt(&ok);
                if (ok) {
                    s->setValue(machine->strengthFromCurrent(current));
                }
            }
        }
    }

    ui->grooveCheck->setChecked(configBool(values, QStringLiteral("groove"), ui->grooveCheck->isChecked()));
    ui->slide0Check->setChecked(configBool(values, QStringLiteral("slide_0"), ui->slide0Check->isChecked()));
    ui->slide1Check->setChecked(configBool(values, QStringLiteral("slide_1"), ui->slide1Check->isChecked()));
    ui->slide2Check->setChecked(configBool(values, QStringLiteral("slide_2"), ui->slide2Check->isChecked()));

    applyAdvancedParamsToTable(values);
    updateMaterialCombo();
    refreshModelList();
    ui->glView->setBuildPlateSize(ui->projectorWidthSpin->value() * ui->pixelSizeSpin->value(),
                                  ui->projectorHeightSpin->value() * ui->pixelSizeSpin->value());
    ui->glView->update();
    return true;
}

// ---------------------------------------------------------------------------
// Model management
// ---------------------------------------------------------------------------

int MainWindow::selectedModelIndex() const
{
    QTreeWidgetItem* item = ui->modelList->currentItem();
    if (!item || item->data(0, ItemKindRole).toInt() != ItemKindModel) {
        return -1;
    }
    const int index = item->data(0, ModelIndexRole).toInt();
    if (index < 0 || index >= m_models.size()) {
        return -1;
    }
    return index;
}

QString MainWindow::selectedAssemblyId() const
{
    QTreeWidgetItem* item = ui->modelList->currentItem();
    if (!item || item->data(0, ItemKindRole).toInt() != ItemKindAssembly) {
        return QString();
    }
    return item->data(0, AssemblyIdRole).toString();
}

bool MainWindow::selectedItemIsAssembly() const
{
    return !selectedAssemblyId().isEmpty();
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

void MainWindow::importStepAssembly()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import STEP"),
        QDir::homePath(),
        QStringLiteral("STEP files (*.step *.stp);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    const int imported = importStepAssemblyFile(path);
    if (imported <= 0) {
        QMessageBox::warning(
            this,
            QStringLiteral("STEP 导入失败"),
            QStringLiteral("未能从 STEP 文件导入子模型。请确认已安装/打包 step_to_stl_parts 转换工具，并查看日志。"));
    }
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
        setModelTreeCurrentModel(m_models.size() - 1);
        loadSelectedModelToControls();
        appendLog(QStringLiteral("Imported %1 STL model(s)").arg(imported));
    }
    ui->glView->update();
    return imported;
}

int MainWindow::importStepAssemblyFile(const QString& path)
{
    const QString tool = defaultStepToolPath();
    if (tool.isEmpty() || !QFileInfo::exists(tool)) {
        appendLog(QStringLiteral("STEP 转换工具不存在: %1").arg(tool));
        return 0;
    }

    const QString tempBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString outputDir = QDir(tempBase).absoluteFilePath(
        QStringLiteral("MultiMaterialSlicerStep/%1_%2")
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(QFileInfo(path).completeBaseName()));
    if (!QDir().mkpath(outputDir)) {
        appendLog(QStringLiteral("无法创建 STEP 临时目录: %1").arg(outputDir));
        return 0;
    }

    QString program = tool;
    QStringList args;
    if (tool.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive)) {
        program = defaultPythonPath();
        args << tool;
    }
    args << QStringLiteral("--input") << path
         << QStringLiteral("--output") << outputDir;

    appendLog(QStringLiteral("正在转换 STEP: %1").arg(path));
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        appendLog(QStringLiteral("无法启动 STEP 转换工具: %1").arg(program));
        return 0;
    }
    process.waitForFinished(-1);

    const QString converterOutput = QString::fromUtf8(process.readAll()).trimmed();
    if (!converterOutput.isEmpty()) {
        appendLog(converterOutput.left(1200));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        appendLog(QStringLiteral("STEP 转换失败，退出码 %1").arg(process.exitCode()));
        return 0;
    }

    const QString manifestPath = QDir(outputDir).absoluteFilePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("STEP 转换缺少 manifest.json: %1").arg(manifestPath));
        return 0;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        appendLog(QStringLiteral("manifest.json 解析失败: %1").arg(parseError.errorString()));
        return 0;
    }

    const QJsonObject root = doc.object();
    const QJsonArray parts = root.value(QStringLiteral("parts")).toArray();
    if (parts.isEmpty()) {
        appendLog(QStringLiteral("STEP 文件没有可导入的实体。"));
        return 0;
    }

    QVector<ModelInstance> importedModels;
    importedModels.reserve(parts.size());
    for (int i = 0; i < parts.size(); ++i) {
        const QJsonObject part = parts.at(i).toObject();
        QString stlPath = part.value(QStringLiteral("stl")).toString();
        if (stlPath.isEmpty()) {
            stlPath = part.value(QStringLiteral("file")).toString();
        }
        if (stlPath.isEmpty()) {
            appendLog(QStringLiteral("STEP 子实体 %1 缺少 STL 路径，已跳过。").arg(i + 1));
            continue;
        }
        if (QDir::isRelativePath(stlPath)) {
            stlPath = QDir(outputDir).absoluteFilePath(stlPath);
        }

        QString error;
        StlMesh mesh;
        if (!mesh.load(stlPath, &error)) {
            appendLog(QStringLiteral("无法读取 STEP 子实体 STL %1: %2").arg(stlPath, error));
            continue;
        }

        ModelInstance model;
        model.filePath = stlPath;
        model.name = part.value(QStringLiteral("name")).toString(
            QStringLiteral("SOLID-%1").arg(importedModels.size() + 1));
        model.mesh = mesh;
        model.materialIndex = 0;
        model.color = materialColorForSlot(0);
        model.isAssemblyChild = true;
        importedModels.append(model);
    }

    if (importedModels.isEmpty()) {
        appendLog(QStringLiteral("STEP 转换完成，但没有成功读入任何子实体。"));
        return 0;
    }

    normalizeMeshesTogether(&importedModels);

    const QString assemblyId = QStringLiteral("step_%1").arg(m_nextAssemblyId++);
    const QString assemblyName = root.value(QStringLiteral("assembly")).toString(QFileInfo(path).fileName());
    Transform assemblyTransform;
    m_assemblyTransforms.insert(assemblyId, assemblyTransform);
    m_assemblyNames.insert(assemblyId, assemblyName);
    m_assemblyOrder.append(assemblyId);

    for (ModelInstance& model : importedModels) {
        model.assemblyId = assemblyId;
        model.assemblyName = assemblyName;
        model.transform = assemblyTransform;
        m_models.append(model);
    }

    refreshModelList();
    setModelTreeCurrentAssembly(assemblyId);
    loadSelectedModelToControls();
    ui->glView->update();
    appendLog(QStringLiteral("已导入 STEP 装配体 %1，子实体 %2 个").arg(assemblyName).arg(importedModels.size()));
    return importedModels.size();
}

void MainWindow::duplicateSelectedModel()
{
    const QString assemblyId = selectedAssemblyId();
    if (!assemblyId.isEmpty()) {
        const QString baseName = m_assemblyNames.value(assemblyId, QStringLiteral("STEP 装配体"));
        const QString newAssemblyId = QStringLiteral("step_%1").arg(m_nextAssemblyId++);
        const QString newName = baseName + QStringLiteral(" 副本");
        m_assemblyTransforms.insert(newAssemblyId, m_assemblyTransforms.value(assemblyId));
        m_assemblyNames.insert(newAssemblyId, newName);
        m_assemblyOrder.append(newAssemblyId);

        int copied = 0;
        for (const ModelInstance& model : std::as_const(m_models)) {
            if (!model.isAssemblyChild || model.assemblyId != assemblyId) {
                continue;
            }
            ModelInstance copy = model;
            copy.assemblyId = newAssemblyId;
            copy.assemblyName = newName;
            m_models.append(copy);
            ++copied;
        }

        refreshModelList();
        setModelTreeCurrentAssembly(newAssemblyId);
        loadSelectedModelToControls();
        appendLog(QStringLiteral("已复制 STEP 装配体: %1 (%2 个子实体)").arg(newName).arg(copied));
        ui->glView->update();
        return;
    }

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
    copy.isAssemblyChild = false;
    copy.assemblyId.clear();
    copy.assemblyName.clear();

    m_models.append(copy);
    refreshModelList();
    setModelTreeCurrentModel(m_models.size() - 1);
    loadSelectedModelToControls();
    appendLog(QStringLiteral("已复制模型: %1").arg(candidate));
    ui->glView->update();
}

void MainWindow::removeSelectedModel()
{
    const QString assemblyId = selectedAssemblyId();
    if (!assemblyId.isEmpty()) {
        const QString name = m_assemblyNames.value(assemblyId);
        removeAssembly(assemblyId);
        refreshModelList();
        loadSelectedModelToControls();
        appendLog(QStringLiteral("Removed STEP assembly %1").arg(name));
        ui->glView->update();
        return;
    }

    const int index = selectedModelIndex();
    if (index < 0) {
        return;
    }
    const QString name = m_models[index].name;
    const QString childAssemblyId = m_models[index].assemblyId;
    m_models.removeAt(index);
    if (!childAssemblyId.isEmpty()) {
        bool hasSibling = false;
        for (const ModelInstance& model : std::as_const(m_models)) {
            if (model.assemblyId == childAssemblyId) {
                hasSibling = true;
                break;
            }
        }
        if (!hasSibling) {
            m_assemblyTransforms.remove(childAssemblyId);
            m_assemblyNames.remove(childAssemblyId);
            m_assemblyOrder.removeAll(childAssemblyId);
        }
    }
    refreshModelList();
    setModelTreeCurrentModel(std::min(index, m_models.size() - 1));
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

    const QString assemblyId = selectedAssemblyId();
    if (!assemblyId.isEmpty()) {
        const Transform transform = transformFromUi(ui);
        m_assemblyTransforms[assemblyId] = transform;
        applyAssemblyTransform(assemblyId, transform);
        refreshModelList();
        setModelTreeCurrentAssembly(assemblyId);
        ui->glView->update();
        return;
    }

    const int index = selectedModelIndex();
    if (index < 0) {
        return;
    }

    ModelInstance& model = m_models[index];
    model.materialIndex = std::max(0, ui->materialCombo->currentIndex());
    model.color = materialColorForSlot(model.materialIndex);
    if (!model.isAssemblyChild) {
        model.transform = transformFromUi(ui);
    }

    refreshModelList();
    setModelTreeCurrentModel(index);
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

void MainWindow::importConfigYaml()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("导入 config.yaml"),
        ui->outputPathEdit->text().isEmpty() ? QDir::homePath() : ui->outputPathEdit->text(),
        QStringLiteral("YAML 配置 (*.yaml *.yml);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!applyConfigYamlFile(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("导入失败"), error);
        return;
    }

    appendLog(QStringLiteral("已导入 config 参数: %1").arg(path));
    statusBar()->showMessage(QStringLiteral("已导入 config.yaml"));
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
    settings.advancedParams = collectAdvancedParamsFromTable();

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
    const int currentModel = selectedModelIndex();
    const QString currentAssembly = selectedAssemblyId();
    const QSignalBlocker blocker(ui->modelList);
    m_rebuildingModelTree = true;
    ui->modelList->clear();

    auto addModelItem = [this](QTreeWidgetItem* parent, int modelIndex) {
        const ModelInstance& model = m_models[modelIndex];
        auto* item = new QTreeWidgetItem();
        item->setText(0, model.name);
        item->setData(0, ItemKindRole, ItemKindModel);
        item->setData(0, ModelIndexRole, modelIndex);
        item->setText(1, QString::number(model.materialIndex + 1));
        if (parent) {
            parent->addChild(item);
        } else {
            ui->modelList->addTopLevelItem(item);
        }

        auto* spin = new QSpinBox(ui->modelList);
        spin->setRange(1, std::max(1, ui->materialCountSpin->value()));
        spin->setValue(std::min(model.materialIndex + 1, spin->maximum()));
        spin->setAlignment(Qt::AlignCenter);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, modelIndex](int value) {
                    if (m_rebuildingModelTree || modelIndex < 0 || modelIndex >= m_models.size()) {
                        return;
                    }
                    ModelInstance& model = m_models[modelIndex];
                    model.materialIndex = std::max(0, value - 1);
                    model.color = materialColorForSlot(model.materialIndex);
                    refreshModelList();
                    setModelTreeCurrentModel(modelIndex);
                    loadSelectedModelToControls();
                    ui->glView->update();
                });
        ui->modelList->setItemWidget(item, 1, spin);
        return item;
    };

    QStringList renderedAssemblies;
    auto hasAssemblyModels = [this](const QString& assemblyId) {
        for (const ModelInstance& model : m_models) {
            if (model.isAssemblyChild && model.assemblyId == assemblyId) {
                return true;
            }
        }
        return false;
    };

    for (const QString& assemblyId : std::as_const(m_assemblyOrder)) {
        if (!hasAssemblyModels(assemblyId)) {
            continue;
        }
        auto* assemblyItem = new QTreeWidgetItem();
        assemblyItem->setText(0, m_assemblyNames.value(assemblyId, QStringLiteral("STEP 装配体")));
        assemblyItem->setData(0, ItemKindRole, ItemKindAssembly);
        assemblyItem->setData(0, AssemblyIdRole, assemblyId);
        ui->modelList->addTopLevelItem(assemblyItem);
        renderedAssemblies.append(assemblyId);

        for (int i = 0; i < m_models.size(); ++i) {
            const ModelInstance& model = m_models[i];
            if (model.isAssemblyChild && model.assemblyId == assemblyId) {
                addModelItem(assemblyItem, i);
            }
        }
    }

    for (int i = 0; i < m_models.size(); ++i) {
        const ModelInstance& model = m_models[i];
        if (model.isAssemblyChild) {
            if (!renderedAssemblies.contains(model.assemblyId)) {
                m_assemblyOrder.append(model.assemblyId);
            }
            continue;
        }
        addModelItem(nullptr, i);
    }

    ui->modelList->expandAll();
    m_rebuildingModelTree = false;

    if (!currentAssembly.isEmpty()) {
        setModelTreeCurrentAssembly(currentAssembly);
    } else if (currentModel >= 0) {
        setModelTreeCurrentModel(currentModel);
    } else if (ui->modelList->topLevelItemCount() > 0) {
        ui->modelList->setCurrentItem(ui->modelList->topLevelItem(0));
    }
}

void MainWindow::setModelTreeCurrentModel(int modelIndex)
{
    if (modelIndex < 0) {
        ui->modelList->setCurrentItem(nullptr);
        return;
    }

    QList<QTreeWidgetItem*> stack;
    for (int i = 0; i < ui->modelList->topLevelItemCount(); ++i) {
        stack.append(ui->modelList->topLevelItem(i));
    }
    while (!stack.isEmpty()) {
        QTreeWidgetItem* item = stack.takeFirst();
        if (item->data(0, ItemKindRole).toInt() == ItemKindModel
            && item->data(0, ModelIndexRole).toInt() == modelIndex) {
            ui->modelList->setCurrentItem(item);
            return;
        }
        for (int i = 0; i < item->childCount(); ++i) {
            stack.append(item->child(i));
        }
    }
}

void MainWindow::setModelTreeCurrentAssembly(const QString& assemblyId)
{
    if (assemblyId.isEmpty()) {
        return;
    }
    for (int i = 0; i < ui->modelList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = ui->modelList->topLevelItem(i);
        if (item->data(0, ItemKindRole).toInt() == ItemKindAssembly
            && item->data(0, AssemblyIdRole).toString() == assemblyId) {
            ui->modelList->setCurrentItem(item);
            return;
        }
    }
}

void MainWindow::loadSelectedModelToControls()
{
    const QString assemblyId = selectedAssemblyId();
    const int index = selectedModelIndex();
    const bool hasSelection = index >= 0 || !assemblyId.isEmpty();
    ui->transformGroup->setEnabled(hasSelection && m_configReady);
    ui->copyButton->setEnabled(hasSelection && m_configReady);
    ui->removeButton->setEnabled(hasSelection && m_configReady);
    ui->glView->setSelectedIndex(index);

    m_loadingSelection = true;
    if (!assemblyId.isEmpty()) {
        const Transform transform = m_assemblyTransforms.value(assemblyId);
        ui->materialCombo->setCurrentIndex(-1);
        ui->materialCombo->setEnabled(false);
        setTransformEditorsEnabled(true);
        ui->posXSpin->setValue(transform.translation.x());
        ui->posYSpin->setValue(transform.translation.y());
        ui->posZSpin->setValue(transform.translation.z());
        ui->rotXSpin->setValue(transform.rotationDeg.x());
        ui->rotYSpin->setValue(transform.rotationDeg.y());
        ui->rotZSpin->setValue(transform.rotationDeg.z());
        ui->scaleSpin->setValue(transform.scale);
    } else if (index >= 0) {
        const ModelInstance& model = m_models[index];
        ui->materialCombo->setEnabled(true);
        ui->materialCombo->setCurrentIndex(std::min(model.materialIndex, ui->materialCombo->count() - 1));
        setTransformEditorsEnabled(!model.isAssemblyChild);
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
        ui->materialCombo->setEnabled(false);
        setTransformEditorsEnabled(false);
    }
    m_loadingSelection = false;
}

void MainWindow::setTransformEditorsEnabled(bool enabled)
{
    ui->posXLabel->setEnabled(enabled);
    ui->posYLabel->setEnabled(enabled);
    ui->posZLabel->setEnabled(enabled);
    ui->rotXLabel->setEnabled(enabled);
    ui->rotYLabel->setEnabled(enabled);
    ui->rotZLabel->setEnabled(enabled);
    ui->scaleLabel->setEnabled(enabled);
    ui->posXSpin->setEnabled(enabled);
    ui->posYSpin->setEnabled(enabled);
    ui->posZSpin->setEnabled(enabled);
    ui->rotXSpin->setEnabled(enabled);
    ui->rotYSpin->setEnabled(enabled);
    ui->rotZSpin->setEnabled(enabled);
    ui->scaleSpin->setEnabled(enabled);
}

void MainWindow::applyAssemblyTransform(const QString& assemblyId, const Transform& transform)
{
    for (ModelInstance& model : m_models) {
        if (model.isAssemblyChild && model.assemblyId == assemblyId) {
            model.transform = transform;
        }
    }
}

void MainWindow::removeAssembly(const QString& assemblyId)
{
    for (int i = m_models.size() - 1; i >= 0; --i) {
        if (m_models[i].isAssemblyChild && m_models[i].assemblyId == assemblyId) {
            m_models.removeAt(i);
        }
    }
    m_assemblyTransforms.remove(assemblyId);
    m_assemblyNames.remove(assemblyId);
    m_assemblyOrder.removeAll(assemblyId);
}

void MainWindow::appendLog(const QString& text)
{
    const QString prefix = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(prefix, text));
}

void MainWindow::setRunningState(bool running)
{
    const bool hasSelection = selectedModelIndex() >= 0 || selectedItemIsAssembly();
    ui->runWorkflowButton->setEnabled(!running);
    ui->importButton->setEnabled(!running);
    ui->importStepButton->setEnabled(!running);
    ui->copyButton->setEnabled(!running && hasSelection);
    ui->removeButton->setEnabled(!running && hasSelection);
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
        QMessageBox::warning(this, QStringLiteral("没有模型"), QStringLiteral("请先导入至少一个 STL 或 STEP 模型。"));
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
    // Exercise the regular options so they show up in config.yaml.
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

    ui->outputPathEdit->setText(QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("mms_selftest_out")));

    refreshModelList();
    qInfo("selftest: configured 2 materials, triggering runWorkflow()");
    runWorkflow();
}

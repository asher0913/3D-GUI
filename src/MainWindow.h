#pragma once

#include "ModelTypes.h"
#include "PresetLibrary.h"

#include <QMainWindow>
#include <QVector>

QT_BEGIN_NAMESPACE
class QThread;
class QComboBox;
QT_END_NAMESPACE

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void importModels();
    void duplicateSelectedModel();
    void removeSelectedModel();
    void selectedModelChanged();
    void selectedModelEdited();
    void materialCountChanged();
    void machineChanged();
    void browseOutputDir();
    void runWorkflow();
    void workerFinished(bool success, const QString& summary, const QString& outputDir);

private:
    int selectedModelIndex() const;
    SliceSettings collectSettings() const;
    int importModelFiles(const QStringList& paths);
    void updateMaterialCombo();
    void rebuildMaterialTable();
    void applyMaterialPresetToRow(int row);
    void populateMaterialPresetCombo(QComboBox* combo) const;
    void refreshModelList();
    void loadSelectedModelToControls();
    void appendLog(const QString& text);
    void setRunningState(bool running);
    QColor materialColor(int materialIndex) const;
    QColor materialColorForSlot(int slot) const;
    QString defaultMergeToolPath() const;
    QString defaultPythonPath() const;

    // Preset library
    bool loadPresetLibrary();
    bool promptForConfig();
    QStringList candidateConfigPaths() const;
    void populateMachineCombo();
    const MachinePreset* currentMachine() const;
    void applyMachineToUi(const MachinePreset& machine);
    void setConfigDependentEnabled(bool enabled);

    void runSelfTest();

    Ui::MainWindow* ui = nullptr;
    QVector<ModelInstance> m_models;
    PresetLibrary m_presets;
    bool m_configReady = false;
    bool m_loadingSelection = false;
    bool m_rebuildingTable = false;
    QThread* m_workerThread = nullptr;
    bool m_selfTest = false;
};

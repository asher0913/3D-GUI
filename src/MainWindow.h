#pragma once

#include "ModelTypes.h"

#include <QMainWindow>
#include <QVector>

QT_BEGIN_NAMESPACE
class QThread;
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
    void removeSelectedModel();
    void selectedModelChanged();
    void selectedModelEdited();
    void materialCountChanged();
    void browseOutputDir();
    void browseMergeScript();
    void runWorkflow();
    void workerFinished(bool success, const QString& summary, const QString& outputDir);

private:
    int selectedModelIndex() const;
    SliceSettings collectSettings() const;
    int importModelFiles(const QStringList& paths);
    void updateMaterialCombo();
    void rebuildMaterialTable();
    void refreshModelList();
    void loadSelectedModelToControls();
    void appendLog(const QString& text);
    void setRunningState(bool running);
    QColor materialColor(int materialIndex) const;
    QString defaultScriptPath() const;
    QString defaultPythonPath() const;

    void runSelfTest();

    Ui::MainWindow* ui = nullptr;
    QVector<ModelInstance> m_models;
    bool m_loadingSelection = false;
    QThread* m_workerThread = nullptr;
    bool m_selfTest = false;
};

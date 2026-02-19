#pragma once

#include <QMainWindow>

#include "EdsmApiClient.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class SystemSceneWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupUi();

    EdsmApiClient m_apiClient;
    QLineEdit* m_systemNameEdit = nullptr;
    QPushButton* m_loadButton = nullptr;
    QComboBox* m_sourceCombo = nullptr;
    QLabel* m_statusLabel = nullptr;
    SystemSceneWidget* m_sceneWidget = nullptr;
};

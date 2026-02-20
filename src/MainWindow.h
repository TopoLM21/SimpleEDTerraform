#pragma once

#include <QMainWindow>

#include "EdsmApiClient.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QTextEdit;
class SystemSceneWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupUi();
    void setBodyDetailsText(const QString& text);

    EdsmApiClient m_apiClient;
    QLineEdit* m_systemNameEdit = nullptr;
    QPushButton* m_loadButton = nullptr;
    QComboBox* m_sourceCombo = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTextEdit* m_bodyDetailsPanel = nullptr;
    SystemSceneWidget* m_sceneWidget = nullptr;
    QHash<int, CelestialBody> m_currentBodies;
};

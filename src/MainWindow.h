#pragma once

#include <QMainWindow>

#include "EdsmApiClient.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QSplitter;
class QCloseEvent;
class BodyDetailsWidget;
class SystemSceneWidget;
class SystemIdsWindow;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void setupUi();
    void setBodyDetailsPlaceholder(const QString& text);
    void setDetailsPanelVisible(bool visible);
    void saveUiState() const;
    void restoreUiState();
    void updateDetailsToggleText();
    QList<int> defaultSplitterSizesForWidth(int totalWidth) const;
    bool isValidSplitterSizes(const QList<int>& sizes, int totalWidth) const;

protected:
    void closeEvent(QCloseEvent* event) override;

    EdsmApiClient m_apiClient;
    QLineEdit* m_systemNameEdit = nullptr;
    QPushButton* m_loadButton = nullptr;
    QPushButton* m_showIdsButton = nullptr;
    QPushButton* m_toggleDetailsButton = nullptr;
    QComboBox* m_sourceCombo = nullptr;
    QComboBox* m_bodySizeModeCombo = nullptr;
    QLabel* m_statusLabel = nullptr;
    QSplitter* m_contentSplitter = nullptr;
    BodyDetailsWidget* m_bodyDetailsPanel = nullptr;
    SystemSceneWidget* m_sceneWidget = nullptr;
    SystemIdsWindow* m_systemIdsWindow = nullptr;
    QHash<int, CelestialBody> m_currentBodies;
    QList<int> m_lastVisibleSplitterSizes;
};

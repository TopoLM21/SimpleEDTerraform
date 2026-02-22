#include "MainWindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>

#include "BodyDetailsWidget.h"
#include "SystemModelBuilder.h"
#include "SystemIdsWindow.h"
#include "SystemSceneWidget.h"

namespace {

constexpr auto kSettingsGroupUi = "MainWindow";
constexpr auto kSettingsSplitterState = "contentSplitterState";
constexpr auto kSettingsDetailsVisible = "detailsVisible";

QString dataSourceTitle(const SystemDataSource source) {
    switch (source) {
    case SystemDataSource::Edsm:
        return QStringLiteral("EDSM");
    case SystemDataSource::Spansh:
        return QStringLiteral("Spansh");
    case SystemDataSource::Edastro:
        return QStringLiteral("EDAstro");
    case SystemDataSource::Merged:
        return QStringLiteral("EDSM + Spansh");
    }

    return QStringLiteral("Unknown");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    m_systemIdsWindow = new SystemIdsWindow(this);

    connect(m_bodySizeModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int index) {
        const auto mode = index == 1
            ? SystemSceneWidget::BodySizeMode::Physical
            : SystemSceneWidget::BodySizeMode::VisualClamped;
        m_sceneWidget->setBodySizeMode(mode);
    });

    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        const auto systemName = m_systemNameEdit->text().trimmed();

        m_statusLabel->setText(QStringLiteral("Загрузка данных только из EDAstro..."));
        m_apiClient.requestSystemBodies(systemName, SystemRequestMode::EdastroOnly);
    });

    connect(&m_apiClient, &EdsmApiClient::systemBodiesReady, this, [this](const SystemBodiesResult& result) {
        m_currentBodies = SystemModelBuilder::buildBodyMap(result.bodies);
        const auto roots = SystemModelBuilder::findRootBodies(m_currentBodies);

        m_sceneWidget->setSystemData(result.systemName, m_currentBodies, roots);

        int realBodiesCount = 0;
        for (auto it = m_currentBodies.constBegin(); it != m_currentBodies.constEnd(); ++it) {
            if (!isVirtualBarycenterRoot(it.value())) {
                ++realBodiesCount;
            }
        }

        QString status = QStringLiteral("Источник: %1. Загружено тел: %2")
                             .arg(dataSourceTitle(result.selectedSource))
                             .arg(realBodiesCount);


        m_statusLabel->setText(status);
        m_systemIdsWindow->setBodies(m_currentBodies);
    });

    connect(m_showIdsButton, &QPushButton::clicked, this, [this]() {
        m_systemIdsWindow->setBodies(m_currentBodies);
        m_systemIdsWindow->show();
        m_systemIdsWindow->raise();
        m_systemIdsWindow->activateWindow();
    });

    connect(m_toggleDetailsButton, &QPushButton::clicked, this, [this]() {
        setDetailsPanelVisible(!m_bodyDetailsPanel->isVisible());
    });

    connect(&m_apiClient, &EdsmApiClient::requestStateChanged, this, [this](const QString& state) {
        m_statusLabel->setText(state);
    });

    connect(m_sceneWidget, &SystemSceneWidget::bodyClicked, this, [this](const int bodyId) {
        if (!m_currentBodies.contains(bodyId)) {
            setBodyDetailsPlaceholder(QStringLiteral("Тело не найдено в текущих данных."));
            return;
        }

        if (!m_bodyDetailsPanel->isVisible()) {
            setDetailsPanelVisible(true);
        }

        m_bodyDetailsPanel->setBody(m_currentBodies.value(bodyId), m_currentBodies);
    });

    connect(m_sceneWidget, &SystemSceneWidget::emptyAreaClicked, this, [this]() {
        setBodyDetailsPlaceholder(QStringLiteral("Кликните по телу на карте, чтобы увидеть параметры."));
    });

    connect(&m_apiClient, &EdsmApiClient::requestDebugInfo, this, [](const QString& message) {
        qDebug().noquote() << message;
    });

    connect(&m_apiClient, &EdsmApiClient::requestFailed, this, [this](const QString& reason) {
        m_statusLabel->setText(QStringLiteral("Ошибка запроса к EDAstro"));
        qDebug().noquote() << QStringLiteral("[API] Пользовательская ошибка: %1").arg(reason);
        QMessageBox::warning(this, QStringLiteral("System API"), reason);
    });

    restoreUiState();
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* topPanel = new QHBoxLayout();
    auto* systemNameTitle = new QLabel(QStringLiteral("Система:"), central);
    m_systemNameEdit = new QLineEdit(central);
    m_systemNameEdit->setPlaceholderText(QStringLiteral("Например: Sol"));

    auto* sourceTitle = new QLabel(QStringLiteral("Источник:"), central);
    m_sourceCombo = new QComboBox(central);
    m_sourceCombo->addItem(QStringLiteral("Только EDAstro"));
    m_sourceCombo->setEnabled(false);

    auto* bodySizeModeTitle = new QLabel(QStringLiteral("Размер тел:"), central);
    m_bodySizeModeCombo = new QComboBox(central);
    m_bodySizeModeCombo->addItem(QStringLiteral("VisualClamped"));
    m_bodySizeModeCombo->addItem(QStringLiteral("Physical"));
    m_bodySizeModeCombo->setToolTip(QStringLiteral("VisualClamped ограничивает максимальный экранный размер, Physical показывает физический масштаб."));

    m_loadButton = new QPushButton(QStringLiteral("Загрузить"), central);
    m_showIdsButton = new QPushButton(QStringLiteral("ID системы"), central);
    m_toggleDetailsButton = new QPushButton(central);
    m_statusLabel = new QLabel(QStringLiteral("Ожидание запроса"), central);

    topPanel->addWidget(systemNameTitle);
    topPanel->addWidget(m_systemNameEdit, 1);
    topPanel->addWidget(sourceTitle);
    topPanel->addWidget(m_sourceCombo);
    topPanel->addWidget(bodySizeModeTitle);
    topPanel->addWidget(m_bodySizeModeCombo);
    topPanel->addWidget(m_loadButton);
    topPanel->addWidget(m_showIdsButton);
    topPanel->addWidget(m_toggleDetailsButton);

    m_sceneWidget = new SystemSceneWidget(central);

    m_bodyDetailsPanel = new BodyDetailsWidget(central);
    m_bodyDetailsPanel->setMinimumWidth(280);
    setBodyDetailsPlaceholder(QStringLiteral("Кликните по телу на карте, чтобы увидеть параметры."));

    m_contentSplitter = new QSplitter(Qt::Horizontal, central);
    m_contentSplitter->addWidget(m_bodyDetailsPanel);
    m_contentSplitter->addWidget(m_sceneWidget);
    m_contentSplitter->setCollapsible(0, true);
    m_contentSplitter->setCollapsible(1, false);
    m_contentSplitter->setStretchFactor(0, 0);
    m_contentSplitter->setStretchFactor(1, 1);
    m_contentSplitter->setSizes({340, 860});

    updateDetailsToggleText();

    rootLayout->addLayout(topPanel);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(m_contentSplitter, 1);

    setCentralWidget(central);
    resize(1200, 780);
    setWindowTitle(QStringLiteral("SimpleEDTerraform — EDAstro System Viewer"));
}

void MainWindow::setBodyDetailsPlaceholder(const QString& text) {
    if (m_bodyDetailsPanel) {
        m_bodyDetailsPanel->setPlaceholderText(text);
    }
}

void MainWindow::setDetailsPanelVisible(const bool visible) {
    if (!m_bodyDetailsPanel || !m_contentSplitter) {
        return;
    }

    if (visible == m_bodyDetailsPanel->isVisible()) {
        updateDetailsToggleText();
        return;
    }

    if (!visible) {
        m_lastVisibleSplitterSizes = m_contentSplitter->sizes();
        m_bodyDetailsPanel->hide();
        m_contentSplitter->setSizes({0, 1});
    } else {
        m_bodyDetailsPanel->show();
        if (m_lastVisibleSplitterSizes.size() == 2 && m_lastVisibleSplitterSizes.at(0) > 0) {
            m_contentSplitter->setSizes(m_lastVisibleSplitterSizes);
        } else {
            m_contentSplitter->setSizes({340, 860});
        }
    }

    updateDetailsToggleText();
}

void MainWindow::saveUiState() const {
    if (!m_contentSplitter || !m_bodyDetailsPanel) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QStringLiteral(kSettingsGroupUi));
    settings.setValue(QStringLiteral(kSettingsSplitterState), m_contentSplitter->saveState());
    settings.setValue(QStringLiteral(kSettingsDetailsVisible), m_bodyDetailsPanel->isVisible());
    settings.endGroup();
}

void MainWindow::restoreUiState() {
    if (!m_contentSplitter || !m_bodyDetailsPanel) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QStringLiteral(kSettingsGroupUi));

    const auto splitterState = settings.value(QStringLiteral(kSettingsSplitterState)).toByteArray();
    const bool detailsVisible = settings.value(QStringLiteral(kSettingsDetailsVisible), true).toBool();

    if (!splitterState.isEmpty()) {
        m_contentSplitter->restoreState(splitterState);
    }

    settings.endGroup();

    setDetailsPanelVisible(detailsVisible);
    if (detailsVisible) {
        m_lastVisibleSplitterSizes = m_contentSplitter->sizes();
    }
}

void MainWindow::updateDetailsToggleText() {
    if (!m_toggleDetailsButton || !m_bodyDetailsPanel) {
        return;
    }

    m_toggleDetailsButton->setText(
        m_bodyDetailsPanel->isVisible()
            ? QStringLiteral("Скрыть детали")
            : QStringLiteral("Показать детали"));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveUiState();
    QMainWindow::closeEvent(event);
}

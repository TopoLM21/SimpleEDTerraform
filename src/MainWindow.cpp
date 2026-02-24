#include "MainWindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
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

    auto* topControlsLayout = new QVBoxLayout();

    auto* primaryRow = new QHBoxLayout();
    primaryRow->setSpacing(8);

    auto* systemNameTitle = new QLabel(QStringLiteral("Система:"), central);
    m_systemNameEdit = new QLineEdit(central);
    m_systemNameEdit->setPlaceholderText(QStringLiteral("Например: Sol"));

    auto* sourceTitle = new QLabel(QStringLiteral("Источник:"), central);
    m_sourceCombo = new QComboBox(central);
    m_sourceCombo->addItem(QStringLiteral("Только EDAstro"));
    m_sourceCombo->setEnabled(false);

    m_loadButton = new QPushButton(QStringLiteral("Загрузить"), central);
    m_toggleDetailsButton = new QPushButton(central);
    m_statusLabel = new QLabel(QStringLiteral("Ожидание запроса"), central);

    primaryRow->addWidget(systemNameTitle);
    primaryRow->addWidget(m_systemNameEdit, 1);
    primaryRow->addWidget(sourceTitle);
    primaryRow->addWidget(m_sourceCombo);
    primaryRow->addStretch(1);
    primaryRow->addWidget(m_loadButton);
    primaryRow->addWidget(m_toggleDetailsButton);

    m_showIdsButton = new QPushButton(QStringLiteral("Все ID тел текущей системы"), central);
    m_showIdsButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_showIdsButton->setMinimumWidth(m_showIdsButton->sizeHint().width());

    auto* actionsRow = new QHBoxLayout();
    actionsRow->addWidget(m_showIdsButton);
    actionsRow->addStretch(1);

    auto* secondarySettingsGroup = new QGroupBox(QStringLiteral("Вторичные настройки"), central);
    auto* secondaryRow = new QHBoxLayout(secondarySettingsGroup);

    auto* bodySizeModeTitle = new QLabel(QStringLiteral("Размер тел:"), secondarySettingsGroup);
    m_bodySizeModeCombo = new QComboBox(secondarySettingsGroup);
    m_bodySizeModeCombo->addItem(QStringLiteral("VisualClamped"));
    m_bodySizeModeCombo->addItem(QStringLiteral("Physical"));
    m_bodySizeModeCombo->setToolTip(QStringLiteral("VisualClamped ограничивает максимальный экранный размер, Physical показывает физический масштаб."));


    secondaryRow->addWidget(bodySizeModeTitle);
    secondaryRow->addWidget(m_bodySizeModeCombo);
    secondaryRow->addStretch(1);

    topControlsLayout->addLayout(primaryRow);
    topControlsLayout->addLayout(actionsRow);
    topControlsLayout->addWidget(secondarySettingsGroup);

    m_sceneWidget = new SystemSceneWidget(central);

    m_bodyDetailsPanel = new BodyDetailsWidget(central);
    m_bodyDetailsPanel->setMinimumWidth(260);
    m_bodyDetailsPanel->setMaximumWidth(420);
    m_bodyDetailsPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_sceneWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setBodyDetailsPlaceholder(QStringLiteral("Кликните по телу на карте, чтобы увидеть параметры."));

    m_contentSplitter = new QSplitter(Qt::Horizontal, central);
    m_contentSplitter->addWidget(m_bodyDetailsPanel);
    m_contentSplitter->addWidget(m_sceneWidget);
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setCollapsible(0, false);
    m_contentSplitter->setCollapsible(1, false);
    m_contentSplitter->setStretchFactor(0, 0);
    m_contentSplitter->setStretchFactor(1, 1);
    m_contentSplitter->setSizes(defaultSplitterSizesForWidth(width()));

    updateDetailsToggleText();

    rootLayout->addLayout(topControlsLayout);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(m_contentSplitter, 1);

    setCentralWidget(central);
    resize(1200, 780);
    m_contentSplitter->setSizes(defaultSplitterSizesForWidth(width()));
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
            m_contentSplitter->setSizes(defaultSplitterSizesForWidth(m_contentSplitter->width()));
        }
    }

    updateDetailsToggleText();
}

void MainWindow::saveUiState() const {
    if (!m_contentSplitter || !m_bodyDetailsPanel) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroupUi));
    settings.setValue(QLatin1String(kSettingsSplitterState), m_contentSplitter->saveState());
    settings.setValue(QLatin1String(kSettingsDetailsVisible), m_bodyDetailsPanel->isVisible());
    settings.endGroup();
}

void MainWindow::restoreUiState() {
    if (!m_contentSplitter || !m_bodyDetailsPanel) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QLatin1String(kSettingsGroupUi));

    const auto splitterState = settings.value(QLatin1String(kSettingsSplitterState)).toByteArray();
    const bool detailsVisible = settings.value(QLatin1String(kSettingsDetailsVisible), true).toBool();

    bool splitterRestored = false;
    if (!splitterState.isEmpty()) {
        splitterRestored = m_contentSplitter->restoreState(splitterState);
    }

    const auto currentSizes = m_contentSplitter->sizes();
    if (!splitterRestored || !isValidSplitterSizes(currentSizes, m_contentSplitter->width())) {
        m_contentSplitter->setSizes(defaultSplitterSizesForWidth(m_contentSplitter->width()));
    }

    settings.endGroup();

    setDetailsPanelVisible(detailsVisible);
    if (detailsVisible) {
        m_lastVisibleSplitterSizes = m_contentSplitter->sizes();
    }
}


QList<int> MainWindow::defaultSplitterSizesForWidth(const int totalWidth) const {
    const int safeTotalWidth = qMax(totalWidth, 700);
    const int targetDetails = qBound(260, static_cast<int>(safeTotalWidth * 0.28), 420);
    const int sceneWidth = qMax(safeTotalWidth - targetDetails, 380);
    return {targetDetails, sceneWidth};
}

bool MainWindow::isValidSplitterSizes(const QList<int>& sizes, const int totalWidth) const {
    if (sizes.size() != 2) {
        return false;
    }

    const int safeTotalWidth = qMax(totalWidth, 700);
    const int detailsWidth = sizes.at(0);
    const int sceneWidth = sizes.at(1);

    if (detailsWidth < 260 || detailsWidth > 420) {
        return false;
    }

    if (sceneWidth < 320) {
        return false;
    }

    const int sum = detailsWidth + sceneWidth;
    return sum >= static_cast<int>(safeTotalWidth * 0.75) && sum <= static_cast<int>(safeTotalWidth * 1.25);
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

#include "MainWindow.h"

#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "OrbitClassifier.h"
#include "SystemModelBuilder.h"
#include "SystemIdsWindow.h"
#include "SystemSceneWidget.h"

namespace {

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

QString parentDetailsText(const CelestialBody& body, const QHash<int, CelestialBody>& bodyMap) {
    if (body.parentId < 0) {
        return QStringLiteral("Родитель: —");
    }

    const auto parentIt = bodyMap.constFind(body.parentId);
    if (parentIt == bodyMap.constEnd()) {
        return QStringLiteral("Родитель: ID %1").arg(body.parentId);
    }

    const CelestialBody& parent = parentIt.value();
    QString parentLine = QStringLiteral("Родитель: %1 (ID %2)").arg(parent.name, QString::number(parent.id));

    if (body.orbitsBarycenter && OrbitClassifier::isBarycenterType(parent.type)) {
        // Для тел с орбитой вокруг барицентра parentId указывает именно на барицентр,
        // который задаёт орбитальные параметры соответствующей пары.
        parentLine += QStringLiteral(" — барицентр пары");
    }

    return parentLine;
}

QString bodyDetailsText(const CelestialBody& body, const QHash<int, CelestialBody>& bodyMap) {
    QStringList lines;
    lines << QStringLiteral("Название: %1").arg(body.name);
    lines << QStringLiteral("Тип: %1").arg(body.type.isEmpty() ? QStringLiteral("—") : body.type);
    lines << QStringLiteral("ID: %1").arg(body.id);
    lines << parentDetailsText(body, bodyMap);

    if (!body.parentRelationType.isEmpty()) {
        lines << QStringLiteral("Связь с родителем: %1").arg(body.parentRelationType);
    }

    lines << QStringLiteral("До точки входа: %1 ls").arg(body.distanceToArrivalLs, 0, 'f', 2);
    lines << QStringLiteral("Большая полуось: %1 AU").arg(body.semiMajorAxisAu, 0, 'f', 5);
    lines << QStringLiteral("Детей: %1").arg(body.children.size());
    lines << QStringLiteral("Орбита вокруг барицентра: %1").arg(body.orbitsBarycenter ? QStringLiteral("да") : QStringLiteral("нет"));

    return lines.join('\n');
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    m_systemIdsWindow = new SystemIdsWindow(this);

    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        const auto systemName = m_systemNameEdit->text().trimmed();

        m_statusLabel->setText(QStringLiteral("Загрузка данных только из EDAstro..."));
        m_apiClient.requestSystemBodies(systemName, SystemRequestMode::EdastroOnly);
    });

    connect(&m_apiClient, &EdsmApiClient::systemBodiesReady, this, [this](const SystemBodiesResult& result) {
        m_currentBodies = SystemModelBuilder::buildBodyMap(result.bodies);
        const auto roots = SystemModelBuilder::findRootBodies(m_currentBodies);

        m_sceneWidget->setSystemData(result.systemName, m_currentBodies, roots);

        QString status = QStringLiteral("Источник: %1. Загружено тел: %2")
                             .arg(dataSourceTitle(result.selectedSource))
                             .arg(m_currentBodies.size());


        m_statusLabel->setText(status);
        m_systemIdsWindow->setBodies(m_currentBodies);
    });

    connect(m_showIdsButton, &QPushButton::clicked, this, [this]() {
        m_systemIdsWindow->setBodies(m_currentBodies);
        m_systemIdsWindow->show();
        m_systemIdsWindow->raise();
        m_systemIdsWindow->activateWindow();
    });

    connect(&m_apiClient, &EdsmApiClient::requestStateChanged, this, [this](const QString& state) {
        m_statusLabel->setText(state);
    });

    connect(m_sceneWidget, &SystemSceneWidget::bodyClicked, this, [this](const int bodyId) {
        if (!m_currentBodies.contains(bodyId)) {
            setBodyDetailsText(QStringLiteral("Тело не найдено в текущих данных."));
            return;
        }

        setBodyDetailsText(bodyDetailsText(m_currentBodies.value(bodyId), m_currentBodies));
    });

    connect(m_sceneWidget, &SystemSceneWidget::emptyAreaClicked, this, [this]() {
        setBodyDetailsText(QStringLiteral("Кликните по телу на карте, чтобы увидеть параметры."));
    });

    connect(&m_apiClient, &EdsmApiClient::requestDebugInfo, this, [](const QString& message) {
        qDebug().noquote() << message;
    });

    connect(&m_apiClient, &EdsmApiClient::requestFailed, this, [this](const QString& reason) {
        m_statusLabel->setText(QStringLiteral("Ошибка запроса к EDAstro"));
        qDebug().noquote() << QStringLiteral("[API] Пользовательская ошибка: %1").arg(reason);
        QMessageBox::warning(this, QStringLiteral("System API"), reason);
    });
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

    m_loadButton = new QPushButton(QStringLiteral("Загрузить"), central);
    m_showIdsButton = new QPushButton(QStringLiteral("ID системы"), central);
    m_statusLabel = new QLabel(QStringLiteral("Ожидание запроса"), central);

    topPanel->addWidget(systemNameTitle);
    topPanel->addWidget(m_systemNameEdit, 1);
    topPanel->addWidget(sourceTitle);
    topPanel->addWidget(m_sourceCombo);
    topPanel->addWidget(m_loadButton);
    topPanel->addWidget(m_showIdsButton);

    m_sceneWidget = new SystemSceneWidget(central);

    m_bodyDetailsPanel = new QTextEdit(central);
    m_bodyDetailsPanel->setReadOnly(true);
    m_bodyDetailsPanel->setMinimumWidth(280);
    m_bodyDetailsPanel->setMaximumWidth(380);
    setBodyDetailsText(QStringLiteral("Кликните по телу на карте, чтобы увидеть параметры."));

    auto* contentLayout = new QHBoxLayout();
    contentLayout->addWidget(m_bodyDetailsPanel);
    contentLayout->addWidget(m_sceneWidget, 1);

    rootLayout->addLayout(topPanel);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addLayout(contentLayout, 1);

    setCentralWidget(central);
    resize(1200, 780);
    setWindowTitle(QStringLiteral("SimpleEDTerraform — EDAstro System Viewer"));
}

void MainWindow::setBodyDetailsText(const QString& text) {
    if (m_bodyDetailsPanel) {
        m_bodyDetailsPanel->setPlainText(text);
    }
}

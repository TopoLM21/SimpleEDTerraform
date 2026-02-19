#include "MainWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>

#include "SystemModelBuilder.h"
#include "SystemSceneWidget.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();

    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        const auto systemName = m_systemNameEdit->text().trimmed();
        m_statusLabel->setText(QStringLiteral("Загрузка данных из EDSM..."));
        m_apiClient.requestSystemBodies(systemName);
    });

    connect(&m_apiClient, &EdsmApiClient::systemBodiesReady, this, [this](const QString& systemName, const QVector<CelestialBody>& bodies) {
        const auto bodyMap = SystemModelBuilder::buildBodyMap(bodies);
        const auto roots = SystemModelBuilder::findRootBodies(bodyMap);

        m_sceneWidget->setSystemData(systemName, bodyMap, roots);
        m_statusLabel->setText(QStringLiteral("Загружено тел: %1").arg(bodyMap.size()));
    });

    connect(&m_apiClient, &EdsmApiClient::requestStateChanged, this, [this](const QString& state) {
        m_statusLabel->setText(state);
    });

    connect(&m_apiClient, &EdsmApiClient::requestDebugInfo, this, [](const QString& message) {
        qDebug().noquote() << message;
    });

    connect(&m_apiClient, &EdsmApiClient::requestFailed, this, [this](const QString& reason) {
        m_statusLabel->setText(QStringLiteral("Ошибка запроса к EDSM"));
        qDebug().noquote() << QStringLiteral("[EDSM] Пользовательская ошибка: %1").arg(reason);
        QMessageBox::warning(this, QStringLiteral("EDSM API"), reason);
    });
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* topPanel = new QHBoxLayout();
    auto* systemNameTitle = new QLabel(QStringLiteral("Система:"), central);
    m_systemNameEdit = new QLineEdit(central);
    m_systemNameEdit->setPlaceholderText(QStringLiteral("Например: Sol"));

    m_loadButton = new QPushButton(QStringLiteral("Загрузить"), central);
    m_statusLabel = new QLabel(QStringLiteral("Ожидание запроса"), central);

    topPanel->addWidget(systemNameTitle);
    topPanel->addWidget(m_systemNameEdit, 1);
    topPanel->addWidget(m_loadButton);

    m_sceneWidget = new SystemSceneWidget(central);

    rootLayout->addLayout(topPanel);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(m_sceneWidget, 1);

    setCentralWidget(central);
    resize(1200, 780);
    setWindowTitle(QStringLiteral("SimpleEDTerraform — EDSM System Viewer"));
}

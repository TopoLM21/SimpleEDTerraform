#include "MainWindow.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "SystemModelBuilder.h"
#include "SystemSceneWidget.h"

namespace {

QString dataSourceTitle(const SystemDataSource source) {
    switch (source) {
    case SystemDataSource::Edsm:
        return QStringLiteral("EDSM");
    case SystemDataSource::Spansh:
        return QStringLiteral("Spansh");
    case SystemDataSource::Merged:
        return QStringLiteral("EDSM + Spansh");
    }

    return QStringLiteral("Unknown");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();

    connect(m_loadButton, &QPushButton::clicked, this, [this]() {
        const auto systemName = m_systemNameEdit->text().trimmed();
        m_statusLabel->setText(QStringLiteral("Загрузка данных: EDSM (приоритет), Spansh (fallback)..."));
        m_apiClient.requestSystemBodies(systemName);
    });

    connect(&m_apiClient, &EdsmApiClient::systemBodiesReady, this, [this](const SystemBodiesResult& result) {
        const auto bodyMap = SystemModelBuilder::buildBodyMap(result.bodies);
        const auto roots = SystemModelBuilder::findRootBodies(bodyMap);

        m_sceneWidget->setSystemData(result.systemName, bodyMap, roots);

        QString status = QStringLiteral("Источник: %1. Загружено тел: %2")
                             .arg(dataSourceTitle(result.selectedSource))
                             .arg(bodyMap.size());

        if (result.hadConflict) {
            status += QStringLiteral(". Конфликт данных: выбран приоритет EDSM");
        }

        if (result.selectedSource == SystemDataSource::Merged) {
            status += QStringLiteral(". EDSM=%1, Spansh=%2")
                          .arg(result.hasEdsmData ? QStringLiteral("да") : QStringLiteral("нет"))
                          .arg(result.hasSpanshData ? QStringLiteral("да") : QStringLiteral("нет"));
        }

        m_statusLabel->setText(status);
    });

    connect(&m_apiClient, &EdsmApiClient::requestStateChanged, this, [this](const QString& state) {
        m_statusLabel->setText(state);
    });

    connect(&m_apiClient, &EdsmApiClient::requestDebugInfo, this, [](const QString& message) {
        qDebug().noquote() << message;
    });

    connect(&m_apiClient, &EdsmApiClient::requestFailed, this, [this](const QString& reason) {
        m_statusLabel->setText(QStringLiteral("Ошибка запроса к EDSM/Spansh"));
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
    setWindowTitle(QStringLiteral("SimpleEDTerraform — EDSM/Spansh System Viewer"));
}

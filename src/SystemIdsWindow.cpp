#include "SystemIdsWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString yesNo(const bool value) {
    return value ? QStringLiteral("да") : QStringLiteral("нет");
}

} // namespace

SystemIdsWindow::SystemIdsWindow(QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("Список ID системы"));
    resize(760, 460);

    auto* rootLayout = new QVBoxLayout(this);
    auto* title = new QLabel(QStringLiteral("Все ID тел текущей системы"), this);

    auto* contentLayout = new QHBoxLayout();
    m_idsList = new QListWidget(this);
    m_idsList->setMinimumWidth(220);

    m_detailsPanel = new QTextEdit(this);
    m_detailsPanel->setReadOnly(true);
    m_detailsPanel->setPlainText(QStringLiteral("Выберите ID слева, чтобы посмотреть параметры."));

    contentLayout->addWidget(m_idsList);
    contentLayout->addWidget(m_detailsPanel, 1);

    rootLayout->addWidget(title);
    rootLayout->addLayout(contentLayout, 1);

    connect(m_idsList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (!item) {
            return;
        }

        const int bodyId = item->data(Qt::UserRole).toInt();
        if (!m_bodies.contains(bodyId)) {
            m_detailsPanel->setPlainText(QStringLiteral("Параметры для выбранного ID не найдены."));
            return;
        }

        m_detailsPanel->setPlainText(bodyDetailsText(m_bodies.value(bodyId)));
    });
}

void SystemIdsWindow::setBodies(const QHash<int, CelestialBody>& bodies) {
    m_bodies = bodies;

    m_idsList->clear();

    QList<int> ids = m_bodies.keys();
    std::sort(ids.begin(), ids.end());

    for (const int id : ids) {
        const CelestialBody body = m_bodies.value(id);
        if (isVirtualBarycenterRoot(body)) {
            continue;
        }

        auto* item = new QListWidgetItem(QStringLiteral("ID %1 — %2").arg(QString::number(id), body.name), m_idsList);
        item->setData(Qt::UserRole, id);
    }

    if (m_idsList->count() == 0) {
        m_detailsPanel->setPlainText(QStringLiteral("Нет данных для отображения ID."));
        return;
    }

    m_idsList->setCurrentRow(0);
    const int firstId = m_idsList->item(0)->data(Qt::UserRole).toInt();
    m_detailsPanel->setPlainText(bodyDetailsText(m_bodies.value(firstId)));
}

QString SystemIdsWindow::bodyDetailsText(const CelestialBody& body) const {
    QStringList lines;
    lines << QStringLiteral("Название: %1").arg(body.name);
    lines << QStringLiteral("ID: %1").arg(body.id);
    lines << QStringLiteral("Тип: %1").arg(body.type.isEmpty() ? QStringLiteral("—") : body.type);
    lines << QStringLiteral("Parent ID: %1").arg(body.parentId >= 0 ? QString::number(body.parentId) : QStringLiteral("—"));

    if (!body.parentRelationType.isEmpty()) {
        lines << QStringLiteral("Связь с родителем: %1").arg(body.parentRelationType);
    }

    lines << QStringLiteral("До точки входа: %1 ls").arg(body.distanceToArrivalLs, 0, 'f', 2);
    lines << QStringLiteral("Большая полуось: %1 AU").arg(body.semiMajorAxisAu, 0, 'f', 5);
    lines << QStringLiteral("Детей: %1").arg(body.children.size());
    lines << QStringLiteral("Орбита вокруг барицентра: %1").arg(yesNo(body.orbitsBarycenter));

    return lines.join('\n');
}

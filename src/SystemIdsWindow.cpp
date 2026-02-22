#include "SystemIdsWindow.h"

#include "BodyDetailsWidget.h"

#include <QCloseEvent>
#include <QLabel>
#include <QSettings>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr auto kSettingsGroup = "SystemIdsWindow";
constexpr auto kSplitterSizesKey = "splitterSizes";

} // namespace

SystemIdsWindow::SystemIdsWindow(QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("Список ID системы"));
    resize(760, 460);

    auto* rootLayout = new QVBoxLayout(this);
    auto* title = new QLabel(QStringLiteral("Все ID тел текущей системы"), this);

    m_splitter = new QSplitter(Qt::Horizontal, this);

    m_bodiesTree = new QTreeWidget(m_splitter);
    m_bodiesTree->setHeaderHidden(true);
    m_bodiesTree->setMinimumWidth(220);

    m_detailsPanel = new BodyDetailsWidget(m_splitter);
    m_detailsPanel->setPlaceholderText(QStringLiteral("Выберите тело слева, чтобы посмотреть параметры."));

    m_splitter->addWidget(m_bodiesTree);
    m_splitter->addWidget(m_detailsPanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    rootLayout->addWidget(title);
    rootLayout->addWidget(m_splitter, 1);

    restoreSplitterState();

    connect(m_bodiesTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (!current) {
                    m_detailsPanel->setPlaceholderText(QStringLiteral("Выберите тело слева, чтобы посмотреть параметры."));
                    return;
                }

                const QVariant idData = current->data(0, Qt::UserRole);
                if (!idData.isValid()) {
                    m_detailsPanel->setPlaceholderText(QStringLiteral("Выберите узел тела, чтобы посмотреть параметры."));
                    return;
                }

                const int bodyId = idData.toInt();
                if (!m_bodies.contains(bodyId)) {
                    m_detailsPanel->setPlaceholderText(QStringLiteral("Параметры для выбранного ID не найдены."));
                    return;
                }

                m_detailsPanel->setBody(m_bodies.value(bodyId));
            });
}

void SystemIdsWindow::setBodies(const QHash<int, CelestialBody>& bodies) {
    m_bodies = bodies;
    m_bodiesTree->clear();

    QList<int> ids = m_bodies.keys();
    std::sort(ids.begin(), ids.end());

    QHash<QString, QTreeWidgetItem*> classGroups;

    for (const int id : ids) {
        const CelestialBody body = m_bodies.value(id);
        if (isVirtualBarycenterRoot(body)) {
            continue;
        }

        const QString groupName = bodyClassGroupName(body);
        QTreeWidgetItem* groupItem = classGroups.value(groupName, nullptr);
        if (!groupItem) {
            groupItem = new QTreeWidgetItem(m_bodiesTree);
            groupItem->setText(0, groupName);
            groupItem->setFirstColumnSpanned(true);
            groupItem->setExpanded(true);
            classGroups.insert(groupName, groupItem);
        }

        auto* bodyItem = new QTreeWidgetItem(groupItem);
        bodyItem->setText(0, QStringLiteral("ID %1 — %2").arg(QString::number(id), body.name));
        bodyItem->setData(0, Qt::UserRole, id);
    }

    if (m_bodiesTree->topLevelItemCount() == 0) {
        m_detailsPanel->setPlaceholderText(QStringLiteral("Нет данных для отображения ID."));
        return;
    }

    m_bodiesTree->expandAll();

    for (int i = 0; i < m_bodiesTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_bodiesTree->topLevelItem(i);
        if (group && group->childCount() > 0) {
            m_bodiesTree->setCurrentItem(group->child(0));
            break;
        }
    }
}

void SystemIdsWindow::closeEvent(QCloseEvent* event) {
    saveSplitterState();
    QWidget::closeEvent(event);
}

QString SystemIdsWindow::bodyClassGroupName(const CelestialBody& body) const {
    const QString normalizedType = body.type.trimmed().toLower();

    if (normalizedType.startsWith("star")) {
        return QStringLiteral("Star");
    }

    if (normalizedType.startsWith("planet")) {
        return QStringLiteral("Planet");
    }

    if (normalizedType.startsWith("moon")) {
        return QStringLiteral("Moon");
    }

    if (normalizedType.startsWith("belt")) {
        return QStringLiteral("Belt");
    }

    if (normalizedType.startsWith("ring")) {
        return QStringLiteral("Ring");
    }

    return QStringLiteral("Other");
}

void SystemIdsWindow::restoreSplitterState() {
    QSettings settings;
    settings.beginGroup(QStringLiteral(kSettingsGroup));

    const QList<QVariant> savedSizes = settings.value(QStringLiteral(kSplitterSizesKey)).toList();
    if (savedSizes.size() == 2) {
        m_splitter->setSizes({savedSizes.at(0).toInt(), savedSizes.at(1).toInt()});
    } else {
        m_splitter->setSizes({260, 500});
    }

    settings.endGroup();
}

void SystemIdsWindow::saveSplitterState() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral(kSettingsGroup));

    const QList<int> sizes = m_splitter->sizes();
    QVariantList variantSizes;
    for (const int size : sizes) {
        variantSizes.push_back(size);
    }

    settings.setValue(QStringLiteral(kSplitterSizesKey), variantSizes);
    settings.endGroup();
}

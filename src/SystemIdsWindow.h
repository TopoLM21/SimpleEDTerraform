#pragma once

#include <QHash>
#include <QWidget>

#include "CelestialBody.h"

class BodyDetailsWidget;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;

class SystemIdsWindow : public QWidget {
    Q_OBJECT
public:
    explicit SystemIdsWindow(QWidget* parent = nullptr);

    void setBodies(const QHash<int, CelestialBody>& bodies);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QString bodyClassGroupName(const CelestialBody& body) const;
    void restoreSplitterState();
    void saveSplitterState() const;

    QSplitter* m_splitter = nullptr;
    QTreeWidget* m_bodiesTree = nullptr;
    BodyDetailsWidget* m_detailsPanel = nullptr;
    QHash<int, CelestialBody> m_bodies;
};

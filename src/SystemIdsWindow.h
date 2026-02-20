#pragma once

#include <QHash>
#include <QWidget>

#include "CelestialBody.h"

class QListWidget;
class QListWidgetItem;
class QTextEdit;

class SystemIdsWindow : public QWidget {
    Q_OBJECT
public:
    explicit SystemIdsWindow(QWidget* parent = nullptr);

    void setBodies(const QHash<int, CelestialBody>& bodies);

private:
    QString bodyDetailsText(const CelestialBody& body) const;

    QListWidget* m_idsList = nullptr;
    QTextEdit* m_detailsPanel = nullptr;
    QHash<int, CelestialBody> m_bodies;
};

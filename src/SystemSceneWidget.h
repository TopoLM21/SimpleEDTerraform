#pragma once

#include <QHash>
#include <QWidget>

#include "CelestialBody.h"
#include "SystemLayoutEngine.h"

class SystemSceneWidget : public QWidget {
    Q_OBJECT
public:
    explicit SystemSceneWidget(QWidget* parent = nullptr);

    void setSystemData(const QString& systemName,
                       const QHash<int, CelestialBody>& bodyMap,
                       const QVector<int>& roots);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_systemName;
    QHash<int, CelestialBody> m_bodyMap;
    QVector<int> m_roots;
    QHash<int, BodyLayout> m_layout;
};

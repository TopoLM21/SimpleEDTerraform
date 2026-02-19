#pragma once

#include <QHash>
#include <QPoint>
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
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildLayout();

    QString m_systemName;
    QHash<int, CelestialBody> m_bodyMap;
    QVector<int> m_roots;
    QHash<int, BodyLayout> m_layout;

    double m_zoom = 1.0;
    QPointF m_panOffset;
    bool m_isDragging = false;
    QPoint m_lastMousePos;
};

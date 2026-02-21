#pragma once

#include <QHash>
#include <QPoint>
#include <QWidget>

#include "CelestialBody.h"
#include "OrbitClassifier.h"
#include "SystemLayoutEngine.h"

class SystemSceneWidget : public QWidget {
    Q_OBJECT
public:
    explicit SystemSceneWidget(QWidget* parent = nullptr);

    void setSystemData(const QString& systemName,
                       const QHash<int, CelestialBody>& bodyMap,
                       const QVector<int>& roots);

signals:
    void bodyClicked(int bodyId);
    void emptyAreaClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void rebuildLayout();
    int findBodyAt(const QPointF& widgetPos) const;
    double bodyDrawRadiusPx(const CelestialBody& body, const BodyLayout& bodyLayout) const;
    double zoomProgress() const;

    QString m_systemName;
    QHash<int, CelestialBody> m_bodyMap;
    QVector<int> m_roots;
    QHash<int, BodyLayout> m_layout;
    OrbitClassificationResult m_orbitClassification;

    double m_zoom = 1.0;
    QPointF m_panOffset;
    bool m_isDragging = false;
    bool m_movedSincePress = false;
    QPoint m_lastMousePos;
    QPoint m_pressPos;
};

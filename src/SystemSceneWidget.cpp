#include "SystemSceneWidget.h"

#include <cmath>
#include <limits>

#include <QtGlobal>

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace {
QColor bodyColorForClass(const CelestialBody::BodyClass bodyClass, const QSet<BodyOrbitType>& bodyTypes) {
    QColor bodyColor(190, 210, 240);
    switch (bodyClass) {
    case CelestialBody::BodyClass::Star:
        bodyColor = QColor(255, 206, 92);
        break;
    case CelestialBody::BodyClass::Planet:
        bodyColor = QColor(98, 176, 255);
        break;
    case CelestialBody::BodyClass::Moon:
        bodyColor = QColor(166, 166, 176);
        break;
    case CelestialBody::BodyClass::Barycenter:
    case CelestialBody::BodyClass::Unknown:
        break;
    }

    if (bodyTypes.contains(BodyOrbitType::BinaryPlanetComponent)) {
        bodyColor = QColor(140, 255, 168);
    } else if (bodyTypes.contains(BodyOrbitType::CircumbinaryPlanet)) {
        bodyColor = QColor(126, 255, 200);
    }

    return bodyColor;
}

double minimumBodyDiameterPx(const CelestialBody::BodyClass bodyClass) {
    switch (bodyClass) {
    case CelestialBody::BodyClass::Star:
        return 15.0;
    case CelestialBody::BodyClass::Planet:
        return 10.0;
    case CelestialBody::BodyClass::Moon:
        return 5.0;
    case CelestialBody::BodyClass::Barycenter:
    case CelestialBody::BodyClass::Unknown:
        return 4.0;
    }

    return 4.0;
}
}


SystemSceneWidget::SystemSceneWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(900, 600);
    setAutoFillBackground(true);
    setMouseTracking(true);
}

void SystemSceneWidget::setSystemData(const QString& systemName,
                                      const QHash<int, CelestialBody>& bodyMap,
                                      const QVector<int>& roots) {
    m_systemName = systemName;
    m_bodyMap = bodyMap;
    m_roots = roots;
    m_zoom = 1.0;
    m_panOffset = QPointF(0.0, 0.0);
    m_isDragging = false;
    m_movedSincePress = false;
    m_orbitClassification = OrbitClassifier::classify(m_bodyMap);
    rebuildLayout();
}

void SystemSceneWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(10, 15, 24));

    painter.setPen(QColor(180, 200, 255));
    painter.drawText(20, 30, QStringLiteral("Система: %1").arg(m_systemName.isEmpty() ? QStringLiteral("—") : m_systemName));

    const QStringList systemLabels = OrbitClassifier::systemTypeLabels(m_orbitClassification.systemTypes);
    const QString systemTypesLine = systemLabels.isEmpty()
        ? QStringLiteral("Типы системы: не обнаружены")
        : QStringLiteral("Типы системы: %1").arg(systemLabels.join(QStringLiteral(", ")));
    painter.setPen(QColor(148, 173, 230));
    painter.drawText(20, 50, systemTypesLine);

    const double kmPerPixel = currentKmPerPixel();
    painter.setPen(QColor(126, 155, 220));
    if (kmPerPixel > 0.0) {
        painter.drawText(20, 70, QStringLiteral("Масштаб: 1 px = %1 км").arg(kmPerPixel, 0, 'g', 6));
    } else {
        painter.drawText(20, 70, QStringLiteral("Масштаб: недостаточно данных"));
    }

    if (m_bodyMap.isEmpty() || m_layout.isEmpty()) {
        painter.drawText(20, 90, QStringLiteral("Нет данных для отображения."));
        return;
    }

    painter.save();
    painter.translate(m_panOffset);
    painter.scale(m_zoom, m_zoom);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(84, 111, 168, 150), 1.0 / m_zoom));
    for (auto it = m_layout.constBegin(); it != m_layout.constEnd(); ++it) {
        const auto bodyIt = m_bodyMap.constFind(it.key());
        if (bodyIt == m_bodyMap.constEnd() || bodyIt->parentId < 0 || !m_layout.contains(bodyIt->parentId)) {
            continue;
        }

        if (bodyIt->bodyClass == CelestialBody::BodyClass::Barycenter) {
            continue;
        }

        const QPointF parentPos = m_layout.value(bodyIt->parentId).position;
        painter.drawEllipse(parentPos, it->orbitRadius, it->orbitRadius);
    }

    struct BodyLabel {
        QPointF widgetPos;
        QString text;
    };
    QVector<BodyLabel> bodyLabels;
    bodyLabels.reserve(m_bodyMap.size());

    for (auto it = m_bodyMap.constBegin(); it != m_bodyMap.constEnd(); ++it) {
        if (!m_layout.contains(it.key()) || it->bodyClass == CelestialBody::BodyClass::Barycenter) {
            continue;
        }

        const BodyLayout bodyLayout = m_layout.value(it.key());
        const QPointF point = bodyLayout.position;
        const double radius = bodyDrawRadiusPx(*it, bodyLayout);

        const QSet<BodyOrbitType> bodyTypes = m_orbitClassification.bodyTypes.value(it.key());

        const QColor bodyColor = bodyColorForClass(it->bodyClass, bodyTypes);

        painter.setBrush(bodyColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(point, radius, radius);

        QStringList labelParts;
        labelParts.push_back(it->name);

        if (it->orbitsBarycenter && it->parentId >= 0 && m_bodyMap.contains(it->parentId)) {
            labelParts.push_back(QStringLiteral("вокруг барицентра"));
        }

        const QStringList typeLabels = OrbitClassifier::bodyTypeLabels(bodyTypes);
        if (!typeLabels.isEmpty()) {
            labelParts.push_back(typeLabels.join(QStringLiteral(", ")));
        }

        const QPointF labelScenePos = point + QPointF(radius + 4.0 / m_zoom, -radius - 2.0 / m_zoom);
        const QPointF labelWidgetPos = labelScenePos * m_zoom + m_panOffset;
        bodyLabels.push_back({labelWidgetPos, labelParts.join(QStringLiteral(" | "))});
    }

    painter.restore();

    painter.setPen(QColor(220, 230, 245));
    for (const BodyLabel& bodyLabel : bodyLabels) {
        painter.drawText(bodyLabel.widgetPos, bodyLabel.text);
    }
}

void SystemSceneWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    rebuildLayout();
}

void SystemSceneWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_movedSincePress = false;
        m_pressPos = event->pos();
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void SystemSceneWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging) {
        const QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        if (!m_movedSincePress && (event->pos() - m_pressPos).manhattanLength() > 3) {
            m_movedSincePress = true;
        }
        m_panOffset += QPointF(delta.x(), delta.y());
        update();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void SystemSceneWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isDragging) {
        const bool treatAsClick = !m_movedSincePress;
        m_isDragging = false;
        unsetCursor();

        if (treatAsClick) {
            const int bodyId = findBodyAt(event->pos());
            if (bodyId >= 0) {
                emit bodyClicked(bodyId);
            } else {
                emit emptyAreaClicked();
            }
        }

        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void SystemSceneWidget::wheelEvent(QWheelEvent* event) {
    const QPoint numDegrees = event->angleDelta() / 8;
    if (numDegrees.isNull()) {
        QWidget::wheelEvent(event);
        return;
    }

    const double step = numDegrees.y() / 15.0;
    const double factor = std::pow(1.15, step);
    const double newZoom = qBound(0.02, m_zoom * factor, 400.0);
    if (qFuzzyCompare(newZoom, m_zoom)) {
        return;
    }

    const QPointF mousePos = event->position();
    const QPointF scenePosBefore = (mousePos - m_panOffset) / m_zoom;
    m_zoom = newZoom;
    const QPointF scenePosAfter = scenePosBefore * m_zoom;
    m_panOffset = mousePos - scenePosAfter;

    update();
    event->accept();
}



double SystemSceneWidget::bodyDrawRadiusPx(const CelestialBody& body, const BodyLayout& bodyLayout) const {
    const double minWidgetRadiusPx = minimumBodyDiameterPx(body.bodyClass) / 2.0;

    if (body.physicalRadiusKm > 0.0 && bodyLayout.pxPerAu > 0.0) {
        constexpr double kmPerAu = 149597870.7;
        // Текущий «истинный» масштаб: сколько экранных пикселей приходится на 1 км.
        const double physicalRadiusWidgetPx = body.physicalRadiusKm * (bodyLayout.pxPerAu * m_zoom / kmPerAu);

        // На сильном отдалении фиксируем минимальный диаметр.
        // Как только физический размер в пикселях становится больше минимума,
        // объект растет строго по реальному масштабу.
        const double widgetRadiusPx = qMax(minWidgetRadiusPx, physicalRadiusWidgetPx);
        constexpr double maxWidgetRadiusPx = 170.0;
        return qMin(widgetRadiusPx, maxWidgetRadiusPx) / m_zoom;
    }

    const double fallbackWidgetPx = qBound(minWidgetRadiusPx, bodyLayout.radius * m_zoom, 14.0);
    return fallbackWidgetPx / m_zoom;
}

double SystemSceneWidget::currentKmPerPixel() const {
    if (m_layout.isEmpty() || m_zoom <= 0.0) {
        return 0.0;
    }

    const auto firstLayout = m_layout.constBegin().value();
    if (firstLayout.pxPerAu <= 0.0) {
        return 0.0;
    }

    constexpr double kmPerAu = 149597870.7;
    return kmPerAu / (firstLayout.pxPerAu * m_zoom);
}
void SystemSceneWidget::rebuildLayout() {
    m_layout = SystemLayoutEngine::buildLayout(m_bodyMap, m_roots, rect());
    update();
}

int SystemSceneWidget::findBodyAt(const QPointF& widgetPos) const {
    if (m_layout.isEmpty()) {
        return -1;
    }

    const QPointF scenePos = (widgetPos - m_panOffset) / m_zoom;
    int foundBodyId = -1;
    double smallestDistance = std::numeric_limits<double>::max();

    for (auto it = m_layout.constBegin(); it != m_layout.constEnd(); ++it) {
        const auto bodyIt = m_bodyMap.constFind(it.key());
        if (bodyIt != m_bodyMap.constEnd() && bodyIt->bodyClass == CelestialBody::BodyClass::Barycenter) {
            continue;
        }

        const QPointF delta = scenePos - it->position;
        const double distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
        const double drawRadius = bodyDrawRadiusPx(*bodyIt, *it);
        const double radiusSquared = drawRadius * drawRadius;
        if (distanceSquared <= radiusSquared && distanceSquared < smallestDistance) {
            foundBodyId = it.key();
            smallestDistance = distanceSquared;
        }
    }

    return foundBodyId;
}

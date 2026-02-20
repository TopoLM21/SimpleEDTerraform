#include "SystemSceneWidget.h"

#include <limits>

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace {
bool isStarBody(const CelestialBody& body) {
    return body.type.contains(QStringLiteral("Star"), Qt::CaseInsensitive);
}

bool isPlanetBody(const CelestialBody& body) {
    return body.type.contains(QStringLiteral("Planet"), Qt::CaseInsensitive);
}

bool isMoonBody(const CelestialBody& body) {
    return body.type.contains(QStringLiteral("Moon"), Qt::CaseInsensitive);
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

    if (m_bodyMap.isEmpty() || m_layout.isEmpty()) {
        painter.drawText(20, 75, QStringLiteral("Нет данных для отображения."));
        return;
    }

    painter.save();
    painter.translate(m_panOffset);
    painter.scale(m_zoom, m_zoom);

    painter.setPen(QPen(QColor(70, 92, 130), 1));
    for (auto it = m_bodyMap.constBegin(); it != m_bodyMap.constEnd(); ++it) {
        if (!m_layout.contains(it.key()) || it->parentId < 0 || !m_layout.contains(it->parentId)
            || OrbitClassifier::isBarycenterType(it->type)) {
            continue;
        }

        const auto& bodyLayout = m_layout[it.key()];
        if (bodyLayout.orbitRadius <= 0.0) {
            continue;
        }

        const auto& parentLayout = m_layout[it->parentId];
        painter.drawEllipse(parentLayout.position, bodyLayout.orbitRadius, bodyLayout.orbitRadius);
    }

    for (auto it = m_bodyMap.constBegin(); it != m_bodyMap.constEnd(); ++it) {
        if (!m_layout.contains(it.key()) || OrbitClassifier::isBarycenterType(it->type)) {
            continue;
        }

        const auto point = m_layout[it.key()].position;
        const auto radius = m_layout[it.key()].radius;

        const QSet<BodyOrbitType> bodyTypes = m_orbitClassification.bodyTypes.value(it.key());

        QColor bodyColor = QColor(190, 210, 240);
        if (isStarBody(*it)) {
            bodyColor = QColor(255, 208, 96);
        } else if (isPlanetBody(*it)) {
            bodyColor = QColor(111, 200, 255);
        } else if (isMoonBody(*it)) {
            bodyColor = QColor(170, 170, 180);
        }

        if (bodyTypes.contains(BodyOrbitType::BinaryPlanetComponent)) {
            bodyColor = QColor(140, 255, 168);
        } else if (bodyTypes.contains(BodyOrbitType::CircumbinaryPlanet)) {
            bodyColor = QColor(126, 255, 200);
        }

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

        painter.setPen(QColor(220, 230, 245));
        painter.drawText(point + QPointF(radius + 4.0, -radius - 2.0), labelParts.join(QStringLiteral(" | ")));
    }

    painter.restore();
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
    const double factor = 1.0 + step * 0.1;
    const double newZoom = qBound(0.2, m_zoom * factor, 10.0);
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
        if (bodyIt != m_bodyMap.constEnd() && OrbitClassifier::isBarycenterType(bodyIt->type)) {
            continue;
        }

        const QPointF delta = scenePos - it->position;
        const double distanceSquared = delta.x() * delta.x() + delta.y() * delta.y();
        const double radiusSquared = it->radius * it->radius;
        if (distanceSquared <= radiusSquared && distanceSquared < smallestDistance) {
            foundBodyId = it.key();
            smallestDistance = distanceSquared;
        }
    }

    return foundBodyId;
}

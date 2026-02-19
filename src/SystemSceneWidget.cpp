#include "SystemSceneWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>


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
        if (!m_layout.contains(it.key()) || it->parentId < 0 || !m_layout.contains(it->parentId)) {
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
        if (!m_layout.contains(it.key())) {
            continue;
        }

        const auto point = m_layout[it.key()].position;
        const auto radius = m_layout[it.key()].radius;

        const QSet<BodyOrbitType> bodyTypes = m_orbitClassification.bodyTypes.value(it.key());

        QColor bodyColor = QColor(245, 208, 96);
        if (bodyTypes.contains(BodyOrbitType::BinaryPlanetPairBarycenter)) {
            bodyColor = QColor(198, 115, 255);
        } else if (bodyTypes.contains(BodyOrbitType::BinaryPlanetNonStarBarycenter)) {
            bodyColor = QColor(255, 170, 90);
        } else if (bodyTypes.contains(BodyOrbitType::BinaryNonStarBarycenter)) {
            bodyColor = QColor(255, 138, 171);
        } else if (bodyTypes.contains(BodyOrbitType::BinaryPlanetComponent)) {
            bodyColor = QColor(140, 255, 168);
        } else if (bodyTypes.contains(BodyOrbitType::CircumbinaryPlanet)) {
            bodyColor = QColor(126, 255, 200);
        } else if (OrbitClassifier::isBarycenterType(it->type)) {
            bodyColor = QColor(255, 120, 120);
        } else if (it->type.contains(QStringLiteral("Planet"), Qt::CaseInsensitive)) {
            bodyColor = QColor(111, 200, 255);
        } else if (it->type.contains(QStringLiteral("Moon"), Qt::CaseInsensitive)) {
            bodyColor = QColor(170, 170, 180);
        }

        painter.setBrush(bodyColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(point, radius, radius);

        QStringList labelParts;
        labelParts.push_back(it->name);

        if (it->orbitsBarycenter) {
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
        m_panOffset += QPointF(delta.x(), delta.y());
        update();
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void SystemSceneWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        unsetCursor();
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

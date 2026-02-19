#include "SystemSceneWidget.h"

#include <QPainter>

SystemSceneWidget::SystemSceneWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(900, 600);
    setAutoFillBackground(true);
}

void SystemSceneWidget::setSystemData(const QString& systemName,
                                      const QHash<int, CelestialBody>& bodyMap,
                                      const QVector<int>& roots) {
    m_systemName = systemName;
    m_bodyMap = bodyMap;
    m_roots = roots;
    m_layout = SystemLayoutEngine::buildLayout(m_bodyMap, m_roots, rect());
    update();
}

void SystemSceneWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(10, 15, 24));

    painter.setPen(QColor(180, 200, 255));
    painter.drawText(20, 30, QStringLiteral("Система: %1").arg(m_systemName.isEmpty() ? QStringLiteral("—") : m_systemName));

    if (m_bodyMap.isEmpty() || m_layout.isEmpty()) {
        painter.drawText(20, 55, QStringLiteral("Нет данных для отображения."));
        return;
    }

    painter.setPen(QPen(QColor(120, 140, 180), 1));
    for (auto it = m_bodyMap.constBegin(); it != m_bodyMap.constEnd(); ++it) {
        if (it->parentId < 0 || !m_layout.contains(it.key()) || !m_layout.contains(it->parentId)) {
            continue;
        }
        painter.drawLine(m_layout[it.key()].position, m_layout[it->parentId].position);
    }

    for (auto it = m_bodyMap.constBegin(); it != m_bodyMap.constEnd(); ++it) {
        if (!m_layout.contains(it.key())) {
            continue;
        }

        const auto point = m_layout[it.key()].position;
        const auto radius = m_layout[it.key()].radius;

        QColor bodyColor = QColor(245, 208, 96);
        if (it->type.contains(QStringLiteral("Planet"), Qt::CaseInsensitive)) {
            bodyColor = QColor(111, 200, 255);
        } else if (it->type.contains(QStringLiteral("Moon"), Qt::CaseInsensitive)) {
            bodyColor = QColor(170, 170, 180);
        }

        painter.setBrush(bodyColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(point, radius, radius);

        painter.setPen(QColor(220, 230, 245));
        painter.drawText(point + QPointF(radius + 4.0, -radius - 2.0), it->name);
    }
}

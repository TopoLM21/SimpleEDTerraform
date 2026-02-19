#include "SystemLayoutEngine.h"

#include <QtMath>

QHash<int, BodyLayout> SystemLayoutEngine::buildLayout(const QHash<int, CelestialBody>& bodyMap,
                                                       const QVector<int>& roots,
                                                       const QRectF& canvasRect) {
    QHash<int, BodyLayout> layout;
    layout.reserve(bodyMap.size());

    if (roots.isEmpty()) {
        return layout;
    }

    const QPointF center = canvasRect.center();
    const double ringRadius = qMin(canvasRect.width(), canvasRect.height()) * 0.18;

    for (int i = 0; i < roots.size(); ++i) {
        const int rootId = roots[i];
        const double angle = (2.0 * M_PI * i) / qMax(1, roots.size());
        const QPointF position(center.x() + qCos(angle) * ringRadius,
                               center.y() + qSin(angle) * ringRadius);

        layout.insert(rootId, BodyLayout{position, 8.0});
        layoutChildrenRecursive(bodyMap, layout, rootId, 36.0, angle);
    }

    return layout;
}

void SystemLayoutEngine::layoutChildrenRecursive(const QHash<int, CelestialBody>& bodyMap,
                                                 QHash<int, BodyLayout>& layout,
                                                 int bodyId,
                                                 double baseDistancePx,
                                                 double angleOffsetRad) {
    if (!bodyMap.contains(bodyId) || !layout.contains(bodyId)) {
        return;
    }

    const auto& body = bodyMap[bodyId];
    if (body.children.isEmpty()) {
        return;
    }

    const auto parentPosition = layout[bodyId].position;
    for (int i = 0; i < body.children.size(); ++i) {
        const int childId = body.children[i];
        const auto& child = bodyMap[childId];

        // В EDSM большая полуось задается в астрономических единицах, а дистанция до прибытия — в светосекундах.
        // Для схемы важна читаемость, поэтому используем логарифм, чтобы одновременно показать близкие и дальние орбиты.
        const double orbitalDistanceHint = child.semiMajorAxisAu > 0.0
                                               ? child.semiMajorAxisAu
                                               : qMax(0.001, child.distanceToArrivalLs / 499.0);
        const double distancePx = baseDistancePx + qLn(1.0 + orbitalDistanceHint) * 24.0;

        const double childAngle = angleOffsetRad + (2.0 * M_PI * i) / qMax(1, body.children.size());
        const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                    parentPosition.y() + qSin(childAngle) * distancePx);

        layout.insert(childId, BodyLayout{childPosition, 6.0});
        layoutChildrenRecursive(bodyMap, layout, childId, baseDistancePx * 0.75, childAngle);
    }
}

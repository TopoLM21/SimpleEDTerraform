#include "SystemLayoutEngine.h"

#include "OrbitClassifier.h"

#include <algorithm>

#include <QtMath>

namespace {
double orbitalDistanceAu(const CelestialBody& body) {
    if (body.semiMajorAxisAu > 0.0) {
        return body.semiMajorAxisAu;
    }

    // 1 а.е. ~ 499 светосекунд. Если большая полуось отсутствует, используем distanceToArrival как приближение.
    return qMax(0.0, body.distanceToArrivalLs / 499.0);
}

bool isStarBody(const CelestialBody& body) {
    return body.type.contains(QStringLiteral("Star"), Qt::CaseInsensitive);
}

bool isPlanetBody(const CelestialBody& body) {
    return body.type.contains(QStringLiteral("Planet"), Qt::CaseInsensitive);
}

bool isMoonBody(const CelestialBody& body) {
    return body.type.contains(QStringLiteral("Moon"), Qt::CaseInsensitive);
}

int bodyTypePriority(const CelestialBody& body) {
    if (isStarBody(body)) {
        return 0;
    }
    if (isPlanetBody(body)) {
        return 1;
    }
    if (isMoonBody(body)) {
        return 2;
    }
    return 3;
}

bool lessForStableLayout(const CelestialBody& lhs, const int lhsId, const CelestialBody& rhs, const int rhsId) {
    const int lhsPriority = bodyTypePriority(lhs);
    const int rhsPriority = bodyTypePriority(rhs);
    if (lhsPriority != rhsPriority) {
        return lhsPriority < rhsPriority;
    }

    const double lhsOrbitAu = orbitalDistanceAu(lhs);
    const double rhsOrbitAu = orbitalDistanceAu(rhs);
    if (!qFuzzyCompare(lhsOrbitAu + 1.0, rhsOrbitAu + 1.0)) {
        return lhsOrbitAu < rhsOrbitAu;
    }

    return lhsId < rhsId;
}
}

QHash<int, BodyLayout> SystemLayoutEngine::buildLayout(const QHash<int, CelestialBody>& bodyMap,
                                                       const QVector<int>& roots,
                                                       const QRectF& canvasRect) {
    QHash<int, BodyLayout> layout;
    layout.reserve(bodyMap.size());

    if (roots.isEmpty()) {
        return layout;
    }

    double maxOrbitAu = 0.0;
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        maxOrbitAu = qMax(maxOrbitAu, orbitalDistanceAu(*it));
    }

    const QPointF center = canvasRect.center();
    const double safeHalfSize = qMax(50.0, qMin(canvasRect.width(), canvasRect.height()) * 0.44);
    const double pxPerAu = maxOrbitAu > 0.0 ? (safeHalfSize / maxOrbitAu) : 50.0;

    if (roots.size() == 1) {
        layout.insert(roots.first(), BodyLayout{center, 9.0, 0.0});
        layoutChildrenRecursive(bodyMap, layout, roots.first(), pxPerAu, 24.0);
        return layout;
    }

    // Если корней несколько (например, данные неполные), раскладываем их по кругу, чтобы не перекрывались.
    const double ringRadius = qMin(canvasRect.width(), canvasRect.height()) * 0.15;
    for (int i = 0; i < roots.size(); ++i) {
        const int rootId = roots[i];
        const double angle = (2.0 * M_PI * i) / qMax(1, roots.size());
        const QPointF position(center.x() + qCos(angle) * ringRadius,
                               center.y() + qSin(angle) * ringRadius);

        layout.insert(rootId, BodyLayout{position, 8.0, 0.0});
        layoutChildrenRecursive(bodyMap, layout, rootId, pxPerAu, 22.0);
    }

    return layout;
}

void SystemLayoutEngine::layoutChildrenRecursive(const QHash<int, CelestialBody>& bodyMap,
                                                 QHash<int, BodyLayout>& layout,
                                                 int bodyId,
                                                 double pxPerAu,
                                                 double fallbackDistancePx) {
    if (!bodyMap.contains(bodyId) || !layout.contains(bodyId)) {
        return;
    }

    const auto& body = bodyMap[bodyId];
    if (body.children.isEmpty()) {
        return;
    }

    const auto parentPosition = layout[bodyId].position;

    QVector<int> sortedChildren;
    sortedChildren.reserve(body.children.size());
    for (const int childId : body.children) {
        const auto childIt = bodyMap.constFind(childId);
        if (childIt == bodyMap.constEnd()) {
            continue;
        }

        sortedChildren.push_back(childId);
    }

    std::sort(sortedChildren.begin(), sortedChildren.end(), [&](const int lhs, const int rhs) {
        return lessForStableLayout(bodyMap[lhs], lhs, bodyMap[rhs], rhs);
    });

    QVector<int> keyChildren;
    keyChildren.reserve(2);
    QVector<int> outerChildren;
    outerChildren.reserve(sortedChildren.size());

    if (OrbitClassifier::isBarycenterType(body.type) && sortedChildren.size() >= 2) {
        // Для барицентра считаем ключевой бинарной парой два самых "внутренних" тела,
        // а остальные дети автоматически становятся внешними circumbinary-орбитами.
        QVector<int> byOrbit = sortedChildren;
        std::sort(byOrbit.begin(), byOrbit.end(), [&](const int lhs, const int rhs) {
            const double lhsOrbitAu = orbitalDistanceAu(bodyMap[lhs]);
            const double rhsOrbitAu = orbitalDistanceAu(bodyMap[rhs]);
            if (!qFuzzyCompare(lhsOrbitAu + 1.0, rhsOrbitAu + 1.0)) {
                return lhsOrbitAu < rhsOrbitAu;
            }
            return lessForStableLayout(bodyMap[lhs], lhs, bodyMap[rhs], rhs);
        });

        keyChildren = {byOrbit[0], byOrbit[1]};
        for (const int childId : sortedChildren) {
            if (childId != keyChildren[0] && childId != keyChildren[1]) {
                outerChildren.push_back(childId);
            }
        }
    }

    if (keyChildren.size() == 2) {
        // Компоненты бинарной пары размещаем симметрично относительно барицентра.
        std::sort(keyChildren.begin(), keyChildren.end(), [&](const int lhs, const int rhs) {
            return lessForStableLayout(bodyMap[lhs], lhs, bodyMap[rhs], rhs);
        });

        const double innerFallbackPx = qMax(8.0, fallbackDistancePx * 0.55);
        double pairDistancePx = innerFallbackPx;
        for (const int childId : keyChildren) {
            const double orbitAu = orbitalDistanceAu(bodyMap[childId]);
            const double scaledDistancePx = orbitAu > 0.0 ? (orbitAu * pxPerAu * 0.35) : innerFallbackPx;
            pairDistancePx = qMax(pairDistancePx, scaledDistancePx);
        }
        pairDistancePx = qBound(innerFallbackPx * 0.6,
                                pairDistancePx,
                                qMax(innerFallbackPx * 1.4, fallbackDistancePx * 0.95));

        double maxInnerOrbitRadiusPx = pairDistancePx;
        for (int i = 0; i < keyChildren.size(); ++i) {
            const int childId = keyChildren[i];
            const double childAngle = M_PI * static_cast<double>(i);
            const QPointF childPosition(parentPosition.x() + qCos(childAngle) * pairDistancePx,
                                        parentPosition.y() + qSin(childAngle) * pairDistancePx);

            layout.insert(childId, BodyLayout{childPosition, 6.0, pairDistancePx});
            maxInnerOrbitRadiusPx = qMax(maxInnerOrbitRadiusPx, pairDistancePx);
            layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, innerFallbackPx * 0.8);
        }

        std::sort(outerChildren.begin(), outerChildren.end(), [&](const int lhs, const int rhs) {
            return lessForStableLayout(bodyMap[lhs], lhs, bodyMap[rhs], rhs);
        });

        const double outerStartPx = qMax(maxInnerOrbitRadiusPx + fallbackDistancePx * 0.9,
                                         fallbackDistancePx * 1.6);
        const double outerStepPx = qMax(12.0, fallbackDistancePx * 0.85);

        for (int i = 0; i < outerChildren.size(); ++i) {
            const int childId = outerChildren[i];
            const auto& child = bodyMap[childId];

            const double orbitAu = orbitalDistanceAu(child);
            const double scaledDistancePx = orbitAu > 0.0 ? orbitAu * pxPerAu : 0.0;
            const double separatedByClassPx = outerStartPx + outerStepPx * static_cast<double>(i);
            const double distancePx = qMax(separatedByClassPx, qMax(fallbackDistancePx, scaledDistancePx));

            const double childAngle = (2.0 * M_PI * i) / qMax(1, outerChildren.size());
            const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                        parentPosition.y() + qSin(childAngle) * distancePx);

            layout.insert(childId, BodyLayout{childPosition, 6.0, distancePx});
            layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, fallbackDistancePx * 0.85);
        }

        return;
    }

    for (int i = 0; i < sortedChildren.size(); ++i) {
        const int childId = sortedChildren[i];
        if (!bodyMap.contains(childId)) {
            continue;
        }

        const auto& child = bodyMap[childId];
        const double orbitAu = orbitalDistanceAu(child);
        const double scaledDistancePx = orbitAu > 0.0 ? orbitAu * pxPerAu : fallbackDistancePx;
        const double distancePx = qMax(fallbackDistancePx, scaledDistancePx);

        const double childAngle = (2.0 * M_PI * i) / qMax(1, sortedChildren.size());
        const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                    parentPosition.y() + qSin(childAngle) * distancePx);

        layout.insert(childId, BodyLayout{childPosition, 6.0, distancePx});
        layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, fallbackDistancePx * 0.85);
    }
}

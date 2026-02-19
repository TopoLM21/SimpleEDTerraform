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

    QVector<int> starChildren;
    starChildren.reserve(body.children.size());
    QVector<int> nonStarChildren;
    nonStarChildren.reserve(body.children.size());
    for (const int childId : body.children) {
        const auto childIt = bodyMap.constFind(childId);
        if (childIt == bodyMap.constEnd()) {
            continue;
        }

        if (isStarBody(childIt.value())) {
            starChildren.push_back(childId);
        } else {
            nonStarChildren.push_back(childId);
        }
    }

    if (OrbitClassifier::isBarycenterType(body.type) && starChildren.size() == 2) {
        // Для барицентра с двумя звёздами раскладываем внутреннюю пару отдельно от внешних circumbinary-орбит.
        std::sort(starChildren.begin(), starChildren.end());

        const double innerFallbackPx = qMax(8.0, fallbackDistancePx * 0.55);
        double maxInnerOrbitRadiusPx = innerFallbackPx;
        for (int i = 0; i < starChildren.size(); ++i) {
            const int childId = starChildren[i];
            const auto& child = bodyMap[childId];

            const double orbitAu = orbitalDistanceAu(child);
            const double scaledDistancePx = orbitAu > 0.0 ? (orbitAu * pxPerAu * 0.35) : innerFallbackPx;
            const double distancePx = qBound(innerFallbackPx * 0.6,
                                             scaledDistancePx,
                                             qMax(innerFallbackPx * 1.4, fallbackDistancePx * 0.95));

            const double childAngle = M_PI * static_cast<double>(i);
            const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                        parentPosition.y() + qSin(childAngle) * distancePx);

            layout.insert(childId, BodyLayout{childPosition, 6.0, distancePx});
            maxInnerOrbitRadiusPx = qMax(maxInnerOrbitRadiusPx, distancePx);
            layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, innerFallbackPx * 0.8);
        }

        std::sort(nonStarChildren.begin(), nonStarChildren.end(), [&](const int lhs, const int rhs) {
            const double lhsOrbitAu = orbitalDistanceAu(bodyMap[lhs]);
            const double rhsOrbitAu = orbitalDistanceAu(bodyMap[rhs]);
            if (qFuzzyCompare(lhsOrbitAu + 1.0, rhsOrbitAu + 1.0)) {
                return lhs < rhs;
            }
            return lhsOrbitAu < rhsOrbitAu;
        });

        const double outerStartPx = qMax(maxInnerOrbitRadiusPx + fallbackDistancePx * 0.9,
                                         fallbackDistancePx * 1.6);
        const double outerStepPx = qMax(12.0, fallbackDistancePx * 0.85);

        for (int i = 0; i < nonStarChildren.size(); ++i) {
            const int childId = nonStarChildren[i];
            const auto& child = bodyMap[childId];

            const double orbitAu = orbitalDistanceAu(child);
            const double scaledDistancePx = orbitAu > 0.0 ? orbitAu * pxPerAu : 0.0;
            const double separatedByClassPx = outerStartPx + outerStepPx * static_cast<double>(i);
            const double distancePx = qMax(separatedByClassPx, qMax(fallbackDistancePx, scaledDistancePx));

            const double childAngle = (2.0 * M_PI * i) / qMax(1, nonStarChildren.size());
            const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                        parentPosition.y() + qSin(childAngle) * distancePx);

            layout.insert(childId, BodyLayout{childPosition, 6.0, distancePx});
            layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, fallbackDistancePx * 0.85);
        }

        return;
    }

    for (int i = 0; i < body.children.size(); ++i) {
        const int childId = body.children[i];
        if (!bodyMap.contains(childId)) {
            continue;
        }

        const auto& child = bodyMap[childId];
        const double orbitAu = orbitalDistanceAu(child);
        const double scaledDistancePx = orbitAu > 0.0 ? orbitAu * pxPerAu : fallbackDistancePx;
        const double distancePx = qMax(fallbackDistancePx, scaledDistancePx);

        const double childAngle = (2.0 * M_PI * i) / qMax(1, body.children.size());
        const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                    parentPosition.y() + qSin(childAngle) * distancePx);

        layout.insert(childId, BodyLayout{childPosition, 6.0, distancePx});
        layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, fallbackDistancePx * 0.85);
    }
}

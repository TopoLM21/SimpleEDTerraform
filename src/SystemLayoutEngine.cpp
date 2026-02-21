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

QVector<int> sortedByLayoutOrder(const QVector<int>& source, const QHash<int, CelestialBody>& bodyMap) {
    QVector<int> result = source;
    std::sort(result.begin(), result.end(), [&](const int lhs, const int rhs) {
        return lessForStableLayout(bodyMap[lhs], lhs, bodyMap[rhs], rhs);
    });
    return result;
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
    const double safeHalfSize = qMax(70.0, qMin(canvasRect.width(), canvasRect.height()) * 0.72);
    // Усиливаем масштаб орбит: система выглядит крупнее и читается на отдалении лучше.
    const double pxPerAu = maxOrbitAu > 0.0 ? (safeHalfSize / maxOrbitAu) : 85.0;

    if (roots.size() == 1) {
        layout.insert(roots.first(), BodyLayout{center, 9.0, 0.0, pxPerAu});
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

        layout.insert(rootId, BodyLayout{position, 8.0, 0.0, pxPerAu});
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
        QVector<int> starChildren;
        for (const int childId : sortedChildren) {
            if (isStarBody(bodyMap[childId])) {
                starChildren.push_back(childId);
            }
        }

        if (starChildren.size() == 2) {
            // Бинарная звезда: обе звезды всегда ставим в противоположные стороны от барицентра.
            keyChildren = sortedByLayoutOrder(starChildren, bodyMap);
            for (const int childId : sortedChildren) {
                if (childId != keyChildren[0] && childId != keyChildren[1]) {
                    outerChildren.push_back(childId);
                }
            }
        } else {
            // Фолбэк для неполных данных: берём два наиболее внутренних тела как ключевую пару.
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
    }

    if (keyChildren.size() == 2) {
        // Компоненты бинарной пары размещаем симметрично относительно барицентра.
        std::sort(keyChildren.begin(), keyChildren.end(), [&](const int lhs, const int rhs) {
            return lessForStableLayout(bodyMap[lhs], lhs, bodyMap[rhs], rhs);
        });

        const double innerFallbackPx = qMax(8.0, fallbackDistancePx * 0.55);
        const double firstOrbitAu = orbitalDistanceAu(bodyMap[keyChildren[0]]);
        const double secondOrbitAu = orbitalDistanceAu(bodyMap[keyChildren[1]]);
        // Компоненты бинарной пары должны лежать на одном диаметре. Если полуоси отличаются,
        // используем среднюю, чтобы обе звезды располагались строго симметрично.
        const double averagedOrbitAu = (firstOrbitAu > 0.0 && secondOrbitAu > 0.0)
            ? ((firstOrbitAu + secondOrbitAu) * 0.5)
            : 0.0;
        const double pairDistancePx = averagedOrbitAu > 0.0 ? (averagedOrbitAu * pxPerAu) : innerFallbackPx;

        for (int i = 0; i < keyChildren.size(); ++i) {
            const int childId = keyChildren[i];
            const double childAngle = M_PI * static_cast<double>(i);
            const QPointF childPosition(parentPosition.x() + qCos(childAngle) * pairDistancePx,
                                        parentPosition.y() + qSin(childAngle) * pairDistancePx);

            layout.insert(childId, BodyLayout{childPosition, 6.0, pairDistancePx, pxPerAu});
            layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, innerFallbackPx * 0.8);
        }

        outerChildren = sortedByLayoutOrder(outerChildren, bodyMap);

        for (int i = 0; i < outerChildren.size(); ++i) {
            const int childId = outerChildren[i];
            const auto& child = bodyMap[childId];

            const double orbitAu = orbitalDistanceAu(child);
            // Для всех объектов, орбитирующих барицентр (включая планеты),
            // радиус орбиты берём напрямую из полуоси. Это сохраняет физический смысл схемы.
            const double scaledDistancePx = orbitAu * pxPerAu;
            const double distancePx = orbitAu > 0.0 ? scaledDistancePx : (fallbackDistancePx * 0.65);

            const double childAngle = (2.0 * M_PI * i) / qMax(1, outerChildren.size());
            const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                        parentPosition.y() + qSin(childAngle) * distancePx);

            layout.insert(childId, BodyLayout{childPosition, 6.0, distancePx, pxPerAu});
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
        const double scaledDistancePx = orbitAu * pxPerAu;
        const double distancePx = orbitAu > 0.0 ? scaledDistancePx : fallbackDistancePx;

        const double childAngle = (2.0 * M_PI * i) / qMax(1, sortedChildren.size());
        const QPointF childPosition(parentPosition.x() + qCos(childAngle) * distancePx,
                                    parentPosition.y() + qSin(childAngle) * distancePx);

        layout.insert(childId, BodyLayout{childPosition, 6.0, distancePx, pxPerAu});
        layoutChildrenRecursive(bodyMap, layout, childId, pxPerAu, fallbackDistancePx * 0.85);
    }
}

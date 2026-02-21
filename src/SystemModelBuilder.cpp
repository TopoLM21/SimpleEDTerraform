#include "SystemModelBuilder.h"

#include <QDebug>

QHash<int, CelestialBody> SystemModelBuilder::buildBodyMap(const QVector<CelestialBody>& bodies) {
    QHash<int, CelestialBody> map;
    map.reserve(bodies.size());

    for (const auto& body : bodies) {
        if (body.id < 0) {
            continue;
        }

        CelestialBody normalizedBody = body;
        if (normalizedBody.parentId == normalizedBody.id) {
            qWarning().noquote()
                << QStringLiteral("[SystemModelBuilder][WARN] Invalid self-parent reference for body id=%1 ('%2'). Normalizing parent to virtual root.")
                       .arg(QString::number(normalizedBody.id),
                            normalizedBody.name.isEmpty() ? QStringLiteral("<без имени>") : normalizedBody.name);

            if (normalizedBody.id == kExternalVirtualBarycenterMarkerId) {
                normalizedBody.parentId = -1;
                normalizedBody.orbitsBarycenter = false;
            } else {
                normalizedBody.parentId = kExternalVirtualBarycenterMarkerId;
                normalizedBody.orbitsBarycenter = true;
            }
            normalizedBody.parentRelationType = QStringLiteral("Null");
        }

        if (map.contains(normalizedBody.id)) {
            const CelestialBody existing = map.value(body.id);
            qWarning().noquote()
                << QStringLiteral("[SystemModelBuilder][WARN] Duplicate body id=%1. Keeping the latest entry. Existing='%2' (%3), incoming='%4' (%5)")
                       .arg(QString::number(normalizedBody.id),
                            existing.name.isEmpty() ? QStringLiteral("<без имени>") : existing.name,
                            existing.type.isEmpty() ? QStringLiteral("<без типа>") : existing.type,
                            normalizedBody.name.isEmpty() ? QStringLiteral("<без имени>") : normalizedBody.name,
                            normalizedBody.type.isEmpty() ? QStringLiteral("<без типа>") : normalizedBody.type);
        }

        map.insert(normalizedBody.id, normalizedBody);
    }

    for (auto it = map.begin(); it != map.end(); ++it) {
        const int parentId = it->parentId;
        if (parentId == it->id) {
            qWarning().noquote()
                << QStringLiteral("[SystemModelBuilder][WARN] Skipping self-parent link for body id=%1 while building children map.")
                       .arg(QString::number(it->id));
            continue;
        }

        if (parentId >= 0 && map.contains(parentId)) {
            map[parentId].children.push_back(it->id);
        }
    }

    return map;
}

QVector<int> SystemModelBuilder::findRootBodies(const QHash<int, CelestialBody>& bodyMap) {
    QVector<int> roots;
    roots.reserve(bodyMap.size());

    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        if (it->parentId < 0 || it->parentId == it->id || !bodyMap.contains(it->parentId)) {
            roots.push_back(it.key());
        }
    }

    if (roots.isEmpty() && !bodyMap.isEmpty()) {
        int fallbackRootId = bodyMap.constBegin().key();
        for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
            fallbackRootId = qMin(fallbackRootId, it.key());
        }

        qWarning().noquote()
            << QStringLiteral("[SystemModelBuilder][WARN] No roots detected after normalization, selecting fallback root id=%1.")
                   .arg(QString::number(fallbackRootId));
        roots.push_back(fallbackRootId);
    }

    return roots;
}

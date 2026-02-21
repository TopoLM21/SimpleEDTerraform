#include "SystemModelBuilder.h"

#include <QDebug>

QHash<int, CelestialBody> SystemModelBuilder::buildBodyMap(const QVector<CelestialBody>& bodies) {
    QHash<int, CelestialBody> map;
    map.reserve(bodies.size());

    for (const auto& body : bodies) {
        if (body.id < 0) {
            continue;
        }

        if (map.contains(body.id)) {
            const CelestialBody existing = map.value(body.id);
            qWarning().noquote()
                << QStringLiteral("[SystemModelBuilder][WARN] Duplicate body id=%1. Keeping the latest entry. Existing='%2' (%3), incoming='%4' (%5)")
                       .arg(QString::number(body.id),
                            existing.name.isEmpty() ? QStringLiteral("<без имени>") : existing.name,
                            existing.type.isEmpty() ? QStringLiteral("<без типа>") : existing.type,
                            body.name.isEmpty() ? QStringLiteral("<без имени>") : body.name,
                            body.type.isEmpty() ? QStringLiteral("<без типа>") : body.type);
        }

        map.insert(body.id, body);
    }

    for (auto it = map.begin(); it != map.end(); ++it) {
        const int parentId = it->parentId;
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
        if (it->parentId < 0 || !bodyMap.contains(it->parentId)) {
            roots.push_back(it.key());
        }
    }

    return roots;
}

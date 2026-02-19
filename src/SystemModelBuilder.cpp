#include "SystemModelBuilder.h"

QHash<int, CelestialBody> SystemModelBuilder::buildBodyMap(const QVector<CelestialBody>& bodies) {
    QHash<int, CelestialBody> map;
    map.reserve(bodies.size());

    for (const auto& body : bodies) {
        if (body.id >= 0) {
            map.insert(body.id, body);
        }
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

#pragma once

#include <QHash>
#include <QVector>

#include "CelestialBody.h"

class SystemModelBuilder {
public:
    static QHash<int, CelestialBody> buildBodyMap(const QVector<CelestialBody>& bodies);
    static QVector<int> findRootBodies(const QHash<int, CelestialBody>& bodyMap);
};

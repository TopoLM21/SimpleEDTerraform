#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include "CelestialBody.h"

enum class BodyOrbitType {
    BinaryStarComponent,
    BinaryStarBarycenter,
    HierarchicalPairMemberBarycenter,
    HierarchicalPairOfPairsBarycenter,
    CircumbinaryPlanet,
};

enum class SystemOrbitType {
    BinaryStar,
    HierarchicalPairOfPairs,
    CircumbinaryPlanetarySystem,
};

struct OrbitClassificationResult {
    QHash<int, QSet<BodyOrbitType>> bodyTypes;
    QSet<SystemOrbitType> systemTypes;
};

class OrbitClassifier {
public:
    static OrbitClassificationResult classify(const QHash<int, CelestialBody>& bodyMap);

    static QString bodyTypeToLabel(BodyOrbitType type);
    static QString systemTypeToLabel(SystemOrbitType type);
    static QStringList bodyTypeLabels(const QSet<BodyOrbitType>& types);
    static QStringList systemTypeLabels(const QSet<SystemOrbitType>& types);
};

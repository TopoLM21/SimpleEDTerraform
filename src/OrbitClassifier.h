#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <type_traits>

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

inline uint qHash(const BodyOrbitType key, const uint seed = 0) noexcept {
    return ::qHash(static_cast<std::underlying_type_t<BodyOrbitType>>(key), seed);
}

inline uint qHash(const SystemOrbitType key, const uint seed = 0) noexcept {
    return ::qHash(static_cast<std::underlying_type_t<SystemOrbitType>>(key), seed);
}

struct OrbitClassificationResult {
    QHash<int, QSet<BodyOrbitType>> bodyTypes;
    QSet<SystemOrbitType> systemTypes;
};

class OrbitClassifier {
public:
    static OrbitClassificationResult classify(const QHash<int, CelestialBody>& bodyMap);
    static bool isBarycenterType(const QString& type);

    static QString bodyTypeToLabel(BodyOrbitType type);
    static QString systemTypeToLabel(SystemOrbitType type);
    static QStringList bodyTypeLabels(const QSet<BodyOrbitType>& types);
    static QStringList systemTypeLabels(const QSet<SystemOrbitType>& types);
};

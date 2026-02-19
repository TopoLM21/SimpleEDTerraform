#include "OrbitClassifier.h"

#include <algorithm>

namespace {

bool containsInsensitive(const QString& value, const QString& token) {
    return value.contains(token, Qt::CaseInsensitive);
}

bool isStar(const CelestialBody& body) {
    return containsInsensitive(body.type, QStringLiteral("Star"));
}

bool isPlanet(const CelestialBody& body) {
    return containsInsensitive(body.type, QStringLiteral("Planet"));
}

bool isBarycenter(const CelestialBody& body) {
    return containsInsensitive(body.type, QStringLiteral("Barycentre"));
}

} // namespace

OrbitClassificationResult OrbitClassifier::classify(const QHash<int, CelestialBody>& bodyMap) {
    OrbitClassificationResult result;

    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        if (!isBarycenter(it.value())) {
            continue;
        }

        int starChildren = 0;
        for (const int childId : it->children) {
            const auto childIt = bodyMap.constFind(childId);
            if (childIt != bodyMap.constEnd() && isStar(childIt.value())) {
                ++starChildren;
            }
        }

        if (starChildren == 2) {
            result.bodyTypes[it.key()].insert(BodyOrbitType::BinaryStarBarycenter);
            result.systemTypes.insert(SystemOrbitType::BinaryStar);

            for (const int childId : it->children) {
                const auto childIt = bodyMap.constFind(childId);
                if (childIt != bodyMap.constEnd() && isStar(childIt.value())) {
                    result.bodyTypes[childId].insert(BodyOrbitType::BinaryStarComponent);
                }
            }
        }
    }

    // Ищем иерархическую "пару пар": верхний барицентр, у которого минимум два дочерних
    // барицентра уже классифицированы как барицентры бинарных звёздных пар.
    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        if (!isBarycenter(it.value())) {
            continue;
        }

        QVector<int> binaryPairChildren;
        for (const int childId : it->children) {
            if (result.bodyTypes.value(childId).contains(BodyOrbitType::BinaryStarBarycenter)) {
                binaryPairChildren.push_back(childId);
            }
        }

        if (binaryPairChildren.size() >= 2) {
            result.bodyTypes[it.key()].insert(BodyOrbitType::HierarchicalPairOfPairsBarycenter);
            result.systemTypes.insert(SystemOrbitType::HierarchicalPairOfPairs);

            for (const int childId : binaryPairChildren) {
                result.bodyTypes[childId].insert(BodyOrbitType::HierarchicalPairMemberBarycenter);
            }
        }
    }

    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        if (!isPlanet(it.value()) || it->parentId < 0) {
            continue;
        }

        if (result.bodyTypes.value(it->parentId).contains(BodyOrbitType::BinaryStarBarycenter)) {
            result.bodyTypes[it.key()].insert(BodyOrbitType::CircumbinaryPlanet);
            result.systemTypes.insert(SystemOrbitType::CircumbinaryPlanetarySystem);
        }
    }

    return result;
}

QString OrbitClassifier::bodyTypeToLabel(const BodyOrbitType type) {
    switch (type) {
    case BodyOrbitType::BinaryStarComponent:
        return QStringLiteral("компонент бинарной звезды");
    case BodyOrbitType::BinaryStarBarycenter:
        return QStringLiteral("барицентр бинарной звезды");
    case BodyOrbitType::HierarchicalPairMemberBarycenter:
        return QStringLiteral("барицентр нижнего уровня в иерархической паре пар");
    case BodyOrbitType::HierarchicalPairOfPairsBarycenter:
        return QStringLiteral("барицентр иерархической пары пар");
    case BodyOrbitType::CircumbinaryPlanet:
        return QStringLiteral("circumbinary planet");
    }

    return QString();
}

QString OrbitClassifier::systemTypeToLabel(const SystemOrbitType type) {
    switch (type) {
    case SystemOrbitType::BinaryStar:
        return QStringLiteral("бинарная звёздная система");
    case SystemOrbitType::HierarchicalPairOfPairs:
        return QStringLiteral("иерархическая пара пар");
    case SystemOrbitType::CircumbinaryPlanetarySystem:
        return QStringLiteral("система с circumbinary planet");
    }

    return QString();
}

QStringList OrbitClassifier::bodyTypeLabels(const QSet<BodyOrbitType>& types) {
    QStringList labels;
    labels.reserve(types.size());
    for (const auto type : types) {
        labels.push_back(bodyTypeToLabel(type));
    }
    std::sort(labels.begin(), labels.end());
    return labels;
}

QStringList OrbitClassifier::systemTypeLabels(const QSet<SystemOrbitType>& types) {
    QStringList labels;
    labels.reserve(types.size());
    for (const auto type : types) {
        labels.push_back(systemTypeToLabel(type));
    }
    std::sort(labels.begin(), labels.end());
    return labels;
}

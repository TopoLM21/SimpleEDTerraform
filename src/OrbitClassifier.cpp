#include "OrbitClassifier.h"

#include <algorithm>

namespace {

bool containsInsensitive(const QString& value, const QString& token) {
    return value.contains(token, Qt::CaseInsensitive);
}

bool isStar(const CelestialBody& body) {
    return body.bodyClass == CelestialBody::BodyClass::Star
        || containsInsensitive(body.type, QStringLiteral("Star"));
}

bool isPlanet(const CelestialBody& body) {
    return body.bodyClass == CelestialBody::BodyClass::Planet
        || containsInsensitive(body.type, QStringLiteral("Planet"));
}

bool isBarycenter(const CelestialBody& body) {
    return body.bodyClass == CelestialBody::BodyClass::Barycenter
        || OrbitClassifier::isBarycenterType(body.type);
}

bool isNonStarBody(const CelestialBody& body) {
    return !isStar(body) && !isBarycenter(body);
}

} // namespace

bool OrbitClassifier::isBarycenterType(const QString& type) {
    return containsInsensitive(type, QStringLiteral("Barycentre"))
        || containsInsensitive(type, QStringLiteral("Barycenter"));
}

OrbitClassificationResult OrbitClassifier::classify(const QHash<int, CelestialBody>& bodyMap) {
    OrbitClassificationResult result;

    for (auto it = bodyMap.constBegin(); it != bodyMap.constEnd(); ++it) {
        if (!isBarycenter(it.value())) {
            continue;
        }

        QVector<int> starChildren;
        QVector<int> planetChildren;
        QVector<int> nonStarChildren;
        QVector<int> nonStarNonPlanetChildren;

        for (const int childId : it->children) {
            const auto childIt = bodyMap.constFind(childId);
            if (childIt == bodyMap.constEnd()) {
                continue;
            }

            const CelestialBody& childBody = childIt.value();
            if (isStar(childBody)) {
                starChildren.push_back(childId);
                continue;
            }

            if (isPlanet(childBody)) {
                planetChildren.push_back(childId);
            }

            if (isNonStarBody(childBody)) {
                nonStarChildren.push_back(childId);
                if (!isPlanet(childBody)) {
                    nonStarNonPlanetChildren.push_back(childId);
                }
            }
        }

        if (starChildren.size() == 2) {
            result.bodyTypes[it.key()].insert(BodyOrbitType::BinaryStarBarycenter);
            result.systemTypes.insert(SystemOrbitType::BinaryStar);

            for (const int childId : starChildren) {
                result.bodyTypes[childId].insert(BodyOrbitType::BinaryStarComponent);
            }
        }

        if (nonStarChildren.size() == 2) {
            // Для не-звёздных барицентров выделяем как общий класс "не-звёздная пара",
            // так и более узкие подтипы (2 планеты или планета + другое не-звёздное тело).
            result.bodyTypes[it.key()].insert(BodyOrbitType::BinaryNonStarBarycenter);
            result.systemTypes.insert(SystemOrbitType::BinaryNonStarPair);

            if (planetChildren.size() == 2) {
                result.bodyTypes[it.key()].insert(BodyOrbitType::BinaryPlanetPairBarycenter);
                result.systemTypes.insert(SystemOrbitType::BinaryPlanetPair);

                for (const int childId : planetChildren) {
                    result.bodyTypes[childId].insert(BodyOrbitType::BinaryPlanetComponent);
                }
            }

            if (planetChildren.size() == 1 && nonStarNonPlanetChildren.size() == 1) {
                result.bodyTypes[it.key()].insert(BodyOrbitType::BinaryPlanetNonStarBarycenter);
                result.systemTypes.insert(SystemOrbitType::BinaryPlanetNonStarPair);

                result.bodyTypes[planetChildren.front()].insert(BodyOrbitType::BinaryPlanetComponent);
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
    case BodyOrbitType::BinaryNonStarBarycenter:
        return QStringLiteral("барицентр двойной не-звёздной пары");
    case BodyOrbitType::BinaryPlanetPairBarycenter:
        return QStringLiteral("барицентр двойной планетной пары");
    case BodyOrbitType::BinaryPlanetNonStarBarycenter:
        return QStringLiteral("барицентр пары: планета + не-звёздное тело");
    case BodyOrbitType::BinaryPlanetComponent:
        return QStringLiteral("компонент двойной планетной/не-звёздной пары");
    case BodyOrbitType::HierarchicalPairMemberBarycenter:
        return QStringLiteral("барицентр нижнего уровня в иерархической паре пар");
    case BodyOrbitType::HierarchicalPairOfPairsBarycenter:
        return QStringLiteral("барицентр иерархической пары пар");
    case BodyOrbitType::CircumbinaryPlanet:
        return QStringLiteral("циркумбинарная планета");
    }

    return QString();
}

QString OrbitClassifier::systemTypeToLabel(const SystemOrbitType type) {
    switch (type) {
    case SystemOrbitType::BinaryStar:
        return QStringLiteral("бинарная звёздная система");
    case SystemOrbitType::BinaryNonStarPair:
        return QStringLiteral("двойная не-звёздная система");
    case SystemOrbitType::BinaryPlanetPair:
        return QStringLiteral("двойная планетная система");
    case SystemOrbitType::BinaryPlanetNonStarPair:
        return QStringLiteral("система с парой планета + не-звёздное тело");
    case SystemOrbitType::HierarchicalPairOfPairs:
        return QStringLiteral("иерархическая пара пар");
    case SystemOrbitType::CircumbinaryPlanetarySystem:
        return QStringLiteral("система с циркумбинарной планетой");
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

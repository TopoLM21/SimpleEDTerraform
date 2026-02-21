#include "EdsmApiClient.h"

#include "OrbitClassifier.h"

#include <QHash>
#include <functional>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QSharedPointer>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr int kRequestTimeoutMs = 15000;
constexpr double kExpectedLsToAuRatio = 499.0047838;
constexpr double kMinAllowedLsToAuRatio = 200.0;
constexpr double kMaxAllowedLsToAuRatio = 2000.0;

QString sourceToText(const SystemDataSource source) {
    switch (source) {
    case SystemDataSource::Edsm:
        return QStringLiteral("EDSM");
    case SystemDataSource::Spansh:
        return QStringLiteral("Spansh");
    case SystemDataSource::Edastro:
        return QStringLiteral("EDAstro");
    case SystemDataSource::Merged:
        return QStringLiteral("EDSM+Spansh");
    }

    return QStringLiteral("Unknown");
}

QString modeToText(const SystemRequestMode mode) {
    switch (mode) {
    case SystemRequestMode::AutoMerge:
        return QStringLiteral("AutoMerge");
    case SystemRequestMode::EdsmOnly:
        return QStringLiteral("EdsmOnly");
    case SystemRequestMode::SpanshOnly:
        return QStringLiteral("SpanshOnly");
    case SystemRequestMode::EdastroOnly:
        return QStringLiteral("EdastroOnly");
    }

    return QStringLiteral("Unknown");
}

void reportLsToAuSanityWarnings(const QVector<CelestialBody>& bodies,
                                const QString& sourceLabel,
                                const std::function<void(const QString&)>& onDebugInfo) {
    for (const auto& body : bodies) {
        if (body.distanceToArrivalLs <= 0.0 || body.semiMajorAxisAu <= 0.0) {
            continue;
        }

        // Проверяем только тела, орбитирующие звезду: для спутников планет
        // ожидаемое отношение LS/AU не применимо.
        if (!body.parentRelationType.contains(QStringLiteral("Star"), Qt::CaseInsensitive)) {
            continue;
        }

        const double ratio = body.distanceToArrivalLs / body.semiMajorAxisAu;
        if (ratio >= kMinAllowedLsToAuRatio && ratio <= kMaxAllowedLsToAuRatio) {
            continue;
        }

        const QString bodyName = body.name.isEmpty() ? QStringLiteral("<без имени>") : body.name;
        const QString bodyIdText = body.id >= 0 ? QString::number(body.id) : QStringLiteral("unknown");
        onDebugInfo(QStringLiteral("[%1][WARN] Подозрительное отношение distanceToArrivalLS/semiMajorAxisAU для тела id=%2, name='%3': ratio=%4, distanceToArrivalLS=%5, semiMajorAxisAU=%6, expected≈%7")
                        .arg(sourceLabel,
                             bodyIdText,
                             bodyName,
                             QString::number(ratio, 'f', 3),
                             QString::number(body.distanceToArrivalLs, 'f', 3),
                             QString::number(body.semiMajorAxisAu, 'f', 6),
                             QString::number(kExpectedLsToAuRatio, 'f', 3)));
    }
}



CelestialBody::BodyClass classifyBodyClassFromType(const QString& bodyType) {
    if (OrbitClassifier::isBarycenterType(bodyType)) {
        return CelestialBody::BodyClass::Barycenter;
    }
    if (bodyType.contains(QStringLiteral("Star"), Qt::CaseInsensitive)) {
        return CelestialBody::BodyClass::Star;
    }
    if (bodyType.contains(QStringLiteral("Moon"), Qt::CaseInsensitive)) {
        return CelestialBody::BodyClass::Moon;
    }
    if (bodyType.contains(QStringLiteral("Planet"), Qt::CaseInsensitive)
        || bodyType.contains(QStringLiteral("world"), Qt::CaseInsensitive)
        || bodyType.contains(QStringLiteral("giant"), Qt::CaseInsensitive)) {
        return CelestialBody::BodyClass::Planet;
    }
    return CelestialBody::BodyClass::Unknown;
}


struct ParentRef {
    QString type;
    int bodyId = -1;
};

QString normalizeParentType(const QString& type) {
    return type.trimmed();
}

bool isBarycenterRef(const QString& type) {
    return type.contains(QStringLiteral("Null"), Qt::CaseInsensitive)
           || type.contains(QStringLiteral("Bary"), Qt::CaseInsensitive);
}

bool isVirtualRootRef(const ParentRef& ref) {
    return ref.bodyId == kVirtualBarycenterRootId
           && ref.type.compare(kVirtualBarycenterRootType, Qt::CaseInsensitive) == 0;
}

ParentRef normalizeParentRef(const ParentRef& ref) {
    ParentRef normalized{normalizeParentType(ref.type), ref.bodyId};
    return normalized;
}

QString parentRefKey(const ParentRef& ref) {
    return QStringLiteral("%1:%2").arg(ref.type.toLower(), QString::number(ref.bodyId));
}

QString parentRefToString(const ParentRef& ref) {
    return QStringLiteral("%1:%2").arg(ref.type, QString::number(ref.bodyId));
}

int readInt(const QJsonObject& object, const QStringList& keys, int defaultValue);
QString readString(const QJsonObject& object, const QStringList& keys);

QVector<ParentRef> parseParentChainFromString(const QString& parentsText) {
    QVector<ParentRef> chain;

    const auto trimmed = parentsText.trimmed();
    if (trimmed.isEmpty()) {
        return chain;
    }

    const auto relations = trimmed.split(';', Qt::SkipEmptyParts);
    chain.reserve(relations.size());

    for (const auto& relationTextRaw : relations) {
        const auto relationText = relationTextRaw.trimmed();
        const int delimiterIndex = relationText.indexOf(':');
        if (delimiterIndex <= 0 || delimiterIndex >= relationText.size() - 1) {
            continue;
        }

        bool ok = false;
        const int relationId = relationText.mid(delimiterIndex + 1).trimmed().toInt(&ok);
        if (!ok) {
            continue;
        }

        chain.push_back(normalizeParentRef({relationText.left(delimiterIndex), relationId}));
    }

    return chain;
}

QVector<ParentRef> parseParentChainFromArray(const QJsonValue& parentsValue) {
    QVector<ParentRef> chain;
    if (!parentsValue.isArray()) {
        return chain;
    }

    const auto parentsArray = parentsValue.toArray();
    chain.reserve(parentsArray.size());

    for (const auto& relationValue : parentsArray) {
        if (!relationValue.isObject()) {
            continue;
        }

        const auto relationObject = relationValue.toObject();
        if (relationObject.isEmpty()) {
            continue;
        }

        const auto relation = relationObject.constBegin();
        bool ok = false;
        int relationId = -1;
        if (relation.value().isDouble()) {
            relationId = relation.value().toInt(-1);
            ok = (relationId >= 0);
        } else if (relation.value().isString()) {
            relationId = relation.value().toString().trimmed().toInt(&ok);
        }
        if (!ok || relationId < 0) {
            continue;
        }

        chain.push_back(normalizeParentRef({relation.key(), relationId}));
    }

    return chain;
}

void applyDirectParentFromChain(const QVector<ParentRef>& parentChain,
                                const QSet<int>& existingBodyIds,
                                const QSet<int>& barycenterBodyIds,
                                int bodyId,
                                const std::function<void(const QString&)>& onDebugInfo,
                                int* outParentId,
                                QString* outRelationType,
                                bool* outOrbitsBarycenter) {
    if (parentChain.isEmpty()) {
        return;
    }

    auto isCandidateValid = [&](const ParentRef& candidate) {
        if (isVirtualRootRef(candidate)) {
            return true;
        }
        if (!existingBodyIds.contains(candidate.bodyId)) {
            return false;
        }

        // Null/Bary relationship корректен только для известного barycenter body id.
        if (isBarycenterRef(candidate.type)) {
            return barycenterBodyIds.contains(candidate.bodyId);
        }

        return true;
    };

    const ParentRef firstParent = normalizeParentRef(parentChain.first());
    ParentRef selectedParent = firstParent;
    int selectedIndex = 0;

    if (!isCandidateValid(firstParent)) {
        for (int index = 1; index < parentChain.size(); ++index) {
            const ParentRef candidate = normalizeParentRef(parentChain.at(index));
            if (!isCandidateValid(candidate)) {
                continue;
            }

            selectedParent = candidate;
            selectedIndex = index;
            break;
        }
    }

    if (selectedIndex > 0) {
        onDebugInfo(QStringLiteral("parents-order mismatch detected: bodyId=%1, selected=%2, original=%3")
                        .arg(QString::number(bodyId),
                             parentRefToString(selectedParent),
                             parentRefToString(firstParent)));
    }

    *outParentId = selectedParent.bodyId;
    *outRelationType = selectedParent.type;
    *outOrbitsBarycenter = isBarycenterRef(selectedParent.type);
}

bool isParentReferenceValid(const int parentId, const QSet<int>& existingBodyIds) {
    return parentId == kExternalVirtualBarycenterMarkerId || existingBodyIds.contains(parentId);
}

bool resolveFallbackParent(const QJsonObject& bodyObj,
                           const QSet<int>& existingBodyIds,
                           int* outParentId,
                           QString* outParentRelationType,
                           QString* outFallbackDescription) {
    const int fallbackParentPlanetId = readInt(bodyObj,
                                               {QStringLiteral("parentPlanetID"),
                                                QStringLiteral("parentPlanetId"),
                                                QStringLiteral("parent_planet_id")},
                                               -1);
    if (fallbackParentPlanetId >= 0 && isParentReferenceValid(fallbackParentPlanetId, existingBodyIds)) {
        *outParentId = fallbackParentPlanetId;
        *outParentRelationType = QStringLiteral("Planet");
        *outFallbackDescription = QStringLiteral("parentPlanetId=%1").arg(fallbackParentPlanetId);
        return true;
    }

    const int fallbackParentStarId = readInt(bodyObj,
                                             {QStringLiteral("parentStarID"),
                                              QStringLiteral("parentStarId"),
                                              QStringLiteral("parent_star_id")},
                                             -1);
    if (fallbackParentStarId >= 0 && isParentReferenceValid(fallbackParentStarId, existingBodyIds)) {
        *outParentId = fallbackParentStarId;
        *outParentRelationType = QStringLiteral("Star");
        *outFallbackDescription = QStringLiteral("parentStarId=%1").arg(fallbackParentStarId);
        return true;
    }

    if (bodyObj.value(QStringLiteral("parent")).isObject()) {
        const auto parentObject = bodyObj.value(QStringLiteral("parent")).toObject();
        const int parentId = readInt(parentObject, {QStringLiteral("bodyId"), QStringLiteral("id")}, -1);
        QString parentRelationType = readString(parentObject,
                                                {QStringLiteral("relationType"),
                                                 QStringLiteral("relation_type"),
                                                 QStringLiteral("type")});
        const ParentRef normalizedParent = normalizeParentRef({parentRelationType, parentId});
        if (normalizedParent.bodyId >= 0 && isParentReferenceValid(normalizedParent.bodyId, existingBodyIds)) {
            *outParentId = normalizedParent.bodyId;
            *outParentRelationType = normalizedParent.type;
            *outFallbackDescription = QStringLiteral("parent.object(type=%1,id=%2)")
                                          .arg(normalizedParent.type, QString::number(normalizedParent.bodyId));
            return true;
        }
    }

    return false;
}



struct HierarchyDiagnostic {
    QString level;
    int bodyId = -1;
    QString parents;
    QString reason;
};

void reportHierarchyDiagnostic(const QString& systemName,
                               const HierarchyDiagnostic& diagnostic,
                               const std::function<void(const QString&)>& onDebugInfo) {
    onDebugInfo(QStringLiteral("[EDASTRO][%1] hierarchy diagnostics: system='%2', bodyId=%3, parents='%4', reason=%5")
                    .arg(diagnostic.level,
                         systemName,
                         QString::number(diagnostic.bodyId),
                         diagnostic.parents,
                         diagnostic.reason));
}

QString parentChainToString(const QVector<ParentRef>& chain) {
    if (chain.isEmpty()) {
        return QStringLiteral("<empty>");
    }

    QStringList parts;
    parts.reserve(chain.size());
    for (const auto& ref : chain) {
        parts.push_back(parentRefToString(ref));
    }
    return parts.join(QStringLiteral(";"));
}

void validateEdastroParentChains(const QVector<CelestialBody>& bodies,
                                 const QHash<int, QVector<ParentRef>>& parentsByBodyId,
                                 const QSet<int>& barycenterIds,
                                 const QString& systemName,
                                 const std::function<void(const QString&)>& onDebugInfo) {
    QHash<int, QVector<ParentRef>> finalChainByBodyId;
    for (const auto& body : bodies) {
        if (body.id < 0) {
            continue;
        }

        QVector<ParentRef> chain = parentsByBodyId.value(body.id);
        if (chain.isEmpty() && body.parentId >= 0) {
            chain.push_back(normalizeParentRef({body.parentRelationType, body.parentId}));
        }
        finalChainByBodyId.insert(body.id, chain);
    }

    // 1) Проверяем циклы Null/Star/Planet в цепочке родителей.
    for (auto it = finalChainByBodyId.constBegin(); it != finalChainByBodyId.constEnd(); ++it) {
        QSet<QString> seen;
        for (const auto& ref : it.value()) {
            const bool trackType = ref.type.contains(QStringLiteral("Null"), Qt::CaseInsensitive)
                                   || ref.type.contains(QStringLiteral("Star"), Qt::CaseInsensitive)
                                   || ref.type.contains(QStringLiteral("Planet"), Qt::CaseInsensitive);
            if (!trackType) {
                continue;
            }

            const QString key = parentRefKey(normalizeParentRef(ref));
            if (seen.contains(key)) {
                reportHierarchyDiagnostic(systemName,
                                          HierarchyDiagnostic{QStringLiteral("ERROR"),
                                                              it.key(),
                                                              parentChainToString(it.value()),
                                                              QStringLiteral("cycle")},
                                          onDebugInfo);
                break;
            }
            seen.insert(key);
        }
    }

    // 2) Каждый Null:B у тел должен существовать в barycenters, кроме Null:0.
    for (auto it = finalChainByBodyId.constBegin(); it != finalChainByBodyId.constEnd(); ++it) {
        for (const auto& ref : it.value()) {
            const ParentRef normalized = normalizeParentRef(ref);
            if (!normalized.type.contains(QStringLiteral("Null"), Qt::CaseInsensitive)) {
                continue;
            }
            if (isVirtualRootRef(normalized)) {
                continue;
            }
            if (barycenterIds.contains(normalized.bodyId)) {
                continue;
            }

            reportHierarchyDiagnostic(systemName,
                                      HierarchyDiagnostic{QStringLiteral("ERROR"),
                                                          it.key(),
                                                          parentChainToString(it.value()),
                                                          QStringLiteral("missing barycenter")},
                                      onDebugInfo);
            break;
        }
    }

    // 3) У барицентра не более одного итогового родителя.
    QHash<int, QSet<QString>> parentVariants;
    for (auto it = finalChainByBodyId.constBegin(); it != finalChainByBodyId.constEnd(); ++it) {
        const auto& chain = it.value();
        for (int i = 0; i < chain.size(); ++i) {
            const ParentRef node = normalizeParentRef(chain[i]);
            if (!node.type.contains(QStringLiteral("Null"), Qt::CaseInsensitive) || isVirtualRootRef(node)) {
                continue;
            }

            ParentRef candidate = ParentRef{kVirtualBarycenterRootType, kVirtualBarycenterRootId};
            if (i + 1 < chain.size()) {
                candidate = normalizeParentRef(chain[i + 1]);
            }
            parentVariants[node.bodyId].insert(parentRefKey(candidate));
        }
    }

    for (auto it = parentVariants.constBegin(); it != parentVariants.constEnd(); ++it) {
        if (it.value().size() <= 1) {
            continue;
        }

        QVector<ParentRef> sourceChain;
        for (auto bodyIt = finalChainByBodyId.constBegin(); bodyIt != finalChainByBodyId.constEnd(); ++bodyIt) {
            for (const auto& ref : bodyIt.value()) {
                const ParentRef normalized = normalizeParentRef(ref);
                if (normalized.type.contains(QStringLiteral("Null"), Qt::CaseInsensitive)
                    && !isVirtualRootRef(normalized)
                    && normalized.bodyId == it.key()) {
                    sourceChain = bodyIt.value();
                    break;
                }
            }
            if (!sourceChain.isEmpty()) {
                break;
            }
        }

        reportHierarchyDiagnostic(systemName,
                                  HierarchyDiagnostic{QStringLiteral("WARNING"),
                                                      it.key(),
                                                      parentChainToString(sourceChain),
                                                      QStringLiteral("multiple parents")},
                                  onDebugInfo);
    }
}

void buildBarycenterHierarchy(QVector<CelestialBody>* bodies,
                              const QHash<int, QVector<ParentRef>>& parentsByBodyId,
                              const QString& systemName,
                              const std::function<void(const QString&)>& onDebugInfo) {
    QHash<int, QHash<QString, ParentRef>> barycenterCandidates;

    for (const auto& body : *bodies) {
        if (body.bodyClass != CelestialBody::BodyClass::Star
            && body.bodyClass != CelestialBody::BodyClass::Planet
            && body.bodyClass != CelestialBody::BodyClass::Moon) {
            continue;
        }

        const auto chain = parentsByBodyId.value(body.id);
        for (int i = 0; i < chain.size(); ++i) {
            const ParentRef parent = normalizeParentRef(chain[i]);
            if (!parent.type.contains(QStringLiteral("Null"), Qt::CaseInsensitive)) {
                continue;
            }

            // Если в цепочке верхний предок Null:0 (после нормализации — технический id), подвешиваем ветку на виртуальный корень.
            if (isVirtualRootRef(parent)) {
                continue;
            }

            ParentRef candidate;
            if (i + 1 < chain.size()) {
                candidate = normalizeParentRef(chain[i + 1]);
            } else {
                candidate = ParentRef{kVirtualBarycenterRootType, kVirtualBarycenterRootId};
            }

            barycenterCandidates[parent.bodyId].insert(parentRefKey(candidate), candidate);
        }
    }

    for (auto& body : *bodies) {
        if (body.bodyClass != CelestialBody::BodyClass::Barycenter) {
            continue;
        }

        const auto candidates = barycenterCandidates.value(body.id);
        if (candidates.isEmpty()) {
            body.parentId = -1;
            body.parentRelationType = QStringLiteral("Unknown");
            body.orbitsBarycenter = false;
            continue;
        }

        if (candidates.size() > 1) {
            QStringList rendered;
            rendered.reserve(candidates.size());
            for (const auto& candidate : candidates) {
                rendered.push_back(parentRefToString(candidate));
            }

            onDebugInfo(QStringLiteral("[EDASTRO][WARN] Конфликт иерархии барицентра: system='%1', barycenter=%2, candidates=[%3]")
                            .arg(systemName,
                                 QString::number(body.id),
                                 rendered.join(QStringLiteral(", "))));
            body.parentId = -1;
            body.parentRelationType = QStringLiteral("Conflict");
            body.orbitsBarycenter = false;
            continue;
        }

        const ParentRef parent = candidates.constBegin().value();
        body.parentId = parent.bodyId;
        body.parentRelationType = parent.type;
        body.orbitsBarycenter = isBarycenterRef(parent.type);
    }
}

void synthesizeMissingBarycenters(QVector<CelestialBody>* bodies,
                                  const QHash<int, QVector<ParentRef>>& parentsByBodyId) {
    QSet<int> existingBodyIds;
    for (const auto& body : *bodies) {
        if (body.id >= 0) {
            existingBodyIds.insert(body.id);
        }
    }

    QSet<int> missingBarycenterIds;
    for (auto it = parentsByBodyId.constBegin(); it != parentsByBodyId.constEnd(); ++it) {
        const auto& parentChain = it.value();
        for (const auto& relation : parentChain) {
            const ParentRef normalizedParent = normalizeParentRef(relation);
            if (!normalizedParent.type.contains(QStringLiteral("Null"), Qt::CaseInsensitive)
                || normalizedParent.bodyId == kVirtualBarycenterRootId
                || normalizedParent.bodyId < 0
                || existingBodyIds.contains(normalizedParent.bodyId)) {
                continue;
            }

            missingBarycenterIds.insert(normalizedParent.bodyId);
        }
    }

    for (const int barycenterId : missingBarycenterIds) {
        CelestialBody syntheticBarycenter;
        syntheticBarycenter.id = barycenterId;
        syntheticBarycenter.name = QStringLiteral("Barycenter %1").arg(barycenterId);
        syntheticBarycenter.type = QStringLiteral("Barycenter");
        syntheticBarycenter.bodyClass = CelestialBody::BodyClass::Barycenter;
        syntheticBarycenter.parentId = -1;
        syntheticBarycenter.parentRelationType = QStringLiteral("Unknown");
        syntheticBarycenter.orbitsBarycenter = false;
        bodies->push_back(syntheticBarycenter);
    }
}


void ensureCentralRootBody(QVector<CelestialBody>* bodies) {
    for (const auto& body : *bodies) {
        if (body.id == kExternalVirtualBarycenterMarkerId) {
            return;
        }
    }

    CelestialBody root;
    root.id = kExternalVirtualBarycenterMarkerId;
    root.name = QStringLiteral("System Center");
    root.type = QStringLiteral("Null");
    root.bodyClass = CelestialBody::BodyClass::Unknown;
    root.parentId = -1;
    root.parentRelationType.clear();
    root.orbitsBarycenter = false;
    // Синтетический центр служит только опорной точкой иерархии и отрисовки.
    root.distanceToArrivalLs = 0.0;
    root.semiMajorAxisAu = 0.0;
    root.physicalRadiusKm = 0.0;

    bodies->push_back(root);
}

void attachDetachedBodiesToCenterRoot(QVector<CelestialBody>* bodies) {
    QSet<int> knownIds;
    knownIds.reserve(bodies->size());
    for (const auto& body : *bodies) {
        if (body.id >= 0) {
            knownIds.insert(body.id);
        }
    }

    for (auto& body : *bodies) {
        if (body.id == kExternalVirtualBarycenterMarkerId) {
            continue;
        }

        if (body.parentId < 0 || !knownIds.contains(body.parentId)) {
            body.parentId = kExternalVirtualBarycenterMarkerId;
            body.parentRelationType = QStringLiteral("Null");
            body.orbitsBarycenter = true;
        }
    }
}

bool canReachStarOrCenterRoot(const int bodyId,
                              const QHash<int, CelestialBody>& bodyById,
                              QSet<int>* recursionGuard) {
    if (recursionGuard->contains(bodyId)) {
        return false;
    }

    recursionGuard->insert(bodyId);
    const auto it = bodyById.constFind(bodyId);
    if (it == bodyById.constEnd()) {
        recursionGuard->remove(bodyId);
        return false;
    }

    const CelestialBody& body = it.value();
    if (body.bodyClass == CelestialBody::BodyClass::Star || body.id == kExternalVirtualBarycenterMarkerId) {
        recursionGuard->remove(bodyId);
        return true;
    }

    if (body.parentId < 0 || !bodyById.contains(body.parentId)) {
        recursionGuard->remove(bodyId);
        return false;
    }

    const bool ok = canReachStarOrCenterRoot(body.parentId, bodyById, recursionGuard);
    recursionGuard->remove(bodyId);
    return ok;
}

bool validateHierarchyCanReachStarOrCenterRoot(const QVector<CelestialBody>& bodies,
                                               const std::function<void(const QString&)>& onDebugInfo,
                                               const QString& sourceLabel) {
    QHash<int, CelestialBody> bodyById;
    for (const auto& body : bodies) {
        if (body.id >= 0) {
            bodyById.insert(body.id, body);
        }
    }

    bool allValid = true;
    for (const auto& body : bodies) {
        if (body.id < 0) {
            continue;
        }

        QSet<int> recursionGuard;
        if (!canReachStarOrCenterRoot(body.id, bodyById, &recursionGuard)) {
            allValid = false;
            onDebugInfo(QStringLiteral("[%1][WARN] Некорректная иерархия: тело id=%2 ('%3') не имеет пути до Star:* или Null:0")
                            .arg(sourceLabel,
                                 QString::number(body.id),
                                 body.name.isEmpty() ? QStringLiteral("<без имени>") : body.name));
        }
    }

    return allValid;
}

bool prepareBodiesForGraph(QVector<CelestialBody>* bodies,
                           const std::function<void(const QString&)>& onDebugInfo,
                           const QString& sourceLabel) {
    ensureCentralRootBody(bodies);
    attachDetachedBodiesToCenterRoot(bodies);
    return validateHierarchyCanReachStarOrCenterRoot(*bodies, onDebugInfo, sourceLabel);
}


bool parseParentFromString(const QString& parentsText,
                           int* outParentId,
                           QString* outRelationType,
                           bool* outOrbitsBarycenter) {
    const auto trimmed = parentsText.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const auto relations = trimmed.split(';', Qt::SkipEmptyParts);
    for (const auto& relationTextRaw : relations) {
        const auto relationText = relationTextRaw.trimmed();
        const int delimiterIndex = relationText.indexOf(':');
        if (delimiterIndex <= 0 || delimiterIndex >= relationText.size() - 1) {
            continue;
        }

        const auto relationType = relationText.left(delimiterIndex).trimmed();
        bool ok = false;
        const int relationId = relationText.mid(delimiterIndex + 1).trimmed().toInt(&ok);
        if (!ok) {
            continue;
        }

        const ParentRef relation = normalizeParentRef({relationType, relationId});
        const bool relationOrbitsBarycenter = isBarycenterRef(relation.type);

        // В данных `parents` первый элемент — непосредственный родитель для отрисовки орбит.
        *outParentId = relation.bodyId;
        *outRelationType = relation.type;
        *outOrbitsBarycenter = relationOrbitsBarycenter;
        return true;
    }

    return false;
}
bool parseParentFromArray(const QJsonValue& parentsValue,
                          int* outParentId,
                          QString* outRelationType,
                          bool* outOrbitsBarycenter) {
    if (!parentsValue.isArray()) {
        return false;
    }

    const auto parentsArray = parentsValue.toArray();
    if (parentsArray.isEmpty()) {
        return false;
    }

    for (const auto& relationValue : parentsArray) {
        if (!relationValue.isObject()) {
            continue;
        }

        const auto parentObject = relationValue.toObject();
        if (parentObject.isEmpty()) {
            continue;
        }

        const auto relationIt = parentObject.constBegin();
        const QString relationType = relationIt.key();
        const int relationId = relationIt.value().toInt(-1);
        if (relationId < 0) {
            continue;
        }

        const ParentRef relation = normalizeParentRef({relationType, relationId});
        const bool relationOrbitsBarycenter = isBarycenterRef(relation.type);

        // В данных `parents` первый элемент — непосредственный родитель для отрисовки орбит.
        *outParentId = relation.bodyId;
        *outRelationType = relation.type;
        *outOrbitsBarycenter = relationOrbitsBarycenter;
        return true;
    }

    return false;
}

double readPhysicalRadiusKm(const QJsonObject& object);

QVector<CelestialBody> parseEdsmBodies(const QJsonObject& rootObject) {
    const auto bodiesArray = rootObject.value(QStringLiteral("bodies")).toArray();
    QVector<CelestialBody> bodies;
    bodies.reserve(bodiesArray.size());

    for (const auto& bodyValue : bodiesArray) {
        const auto bodyObj = bodyValue.toObject();
        CelestialBody body;
        body.id = bodyObj.value(QStringLiteral("bodyId")).toInt(-1);
        body.name = bodyObj.value(QStringLiteral("name")).toString();
        body.type = bodyObj.value(QStringLiteral("type")).toString();
        body.bodyClass = classifyBodyClassFromType(body.type);
        body.distanceToArrivalLs = bodyObj.value(QStringLiteral("distanceToArrival")).toDouble(0.0);
        body.semiMajorAxisAu = bodyObj.value(QStringLiteral("semiMajorAxis")).toDouble(0.0);
        body.physicalRadiusKm = readPhysicalRadiusKm(bodyObj);

        parseParentFromArray(bodyObj.value(QStringLiteral("parents")),
                             &body.parentId,
                             &body.parentRelationType,
                             &body.orbitsBarycenter);

        bodies.push_back(body);
    }

    prepareBodiesForGraph(&bodies,
                          [](const QString&) {},
                          QStringLiteral("EDSM"));
    return bodies;
}

int readInt(const QJsonObject& object, const QStringList& keys, const int defaultValue = -1) {
    for (const auto& key : keys) {
        if (object.contains(key)) {
            const auto value = object.value(key);
            if (value.isDouble()) {
                return value.toInt(defaultValue);
            }

            if (value.isString()) {
                bool ok = false;
                const int parsed = value.toString().trimmed().toInt(&ok);
                if (ok) {
                    return parsed;
                }
            }
        }
    }

    return defaultValue;
}

double readDouble(const QJsonObject& object, const QStringList& keys, const double defaultValue = 0.0) {
    for (const auto& key : keys) {
        if (object.contains(key)) {
            const auto value = object.value(key);
            if (value.isDouble()) {
                return value.toDouble(defaultValue);
            }

            if (value.isString()) {
                bool ok = false;
                const double parsed = value.toString().trimmed().toDouble(&ok);
                if (ok) {
                    return parsed;
                }
            }
        }
    }

    return defaultValue;
}

QString readString(const QJsonObject& object, const QStringList& keys) {
    for (const auto& key : keys) {
        if (object.contains(key)) {
            const auto value = object.value(key);
            if (value.isString()) {
                return value.toString();
            }

            if (value.isDouble()) {
                return QString::number(value.toDouble());
            }

            if (value.isObject()) {
                const auto nested = value.toObject();
                const auto nestedString = readString(nested,
                                                     {QStringLiteral("name"),
                                                      QStringLiteral("type"),
                                                      QStringLiteral("value"),
                                                      QStringLiteral("label")});
                if (!nestedString.isEmpty()) {
                    return nestedString;
                }
            }
        }
    }

    return QString();
}

QJsonArray readArray(const QJsonObject& object, const QStringList& keys) {
    for (const auto& key : keys) {
        const auto value = object.value(key);
        if (value.isArray()) {
            return value.toArray();
        }
    }

    return QJsonArray();
}



double readPhysicalRadiusKm(const QJsonObject& object) {
    // Пробуем сразу несколько полей: разные API отдают радиус в разных единицах.
    const double radiusKm = readDouble(object,
                                       {QStringLiteral("radiusKm"),
                                        QStringLiteral("radius_km")});
    if (radiusKm > 0.0) {
        return radiusKm;
    }

    // В части источников поле radius уже в километрах.
    const double genericRadius = readDouble(object, {QStringLiteral("radius")});
    if (genericRadius > 0.0) {
        return genericRadius;
    }

    // Для полей earthRadius/earthRadii переводим радиус Земли в километры.
    constexpr double earthRadiusKm = 6371.0;
    const double earthRadii = readDouble(object,
                                         {QStringLiteral("earthRadius"),
                                          QStringLiteral("earthRadii"),
                                          QStringLiteral("earth_radius")});
    if (earthRadii > 0.0) {
        return earthRadii * earthRadiusKm;
    }

    // Для солнечных радиусов используем экваториальный радиус Солнца.
    constexpr double solarRadiusKm = 695700.0;
    const double solarRadii = readDouble(object,
                                         {QStringLiteral("solarRadius"),
                                          QStringLiteral("solarRadii"),
                                          QStringLiteral("solar_radius")});
    if (solarRadii > 0.0) {
        return solarRadii * solarRadiusKm;
    }

    return 0.0;
}

QString readMessageField(const QJsonObject& object) {
    for (const auto& key : {QStringLiteral("error"),
                            QStringLiteral("message"),
                            QStringLiteral("msg"),
                            QStringLiteral("detail"),
                            QStringLiteral("description")}) {
        const auto value = object.value(key);
        if (value.isString()) {
            return value.toString();
        }

        if (value.isObject()) {
            const auto nested = value.toObject();
            const auto nestedMessage = readString(nested,
                                                  {QStringLiteral("message"),
                                                   QStringLiteral("error"),
                                                   QStringLiteral("detail"),
                                                   QStringLiteral("description")});
            if (!nestedMessage.isEmpty()) {
                return nestedMessage;
            }
        }
    }

    return QString();
}

QString readSystemIndexFromEdsmObject(const QJsonObject& object) {
    for (const auto& key : {QStringLiteral("id64"), QStringLiteral("systemId64"), QStringLiteral("id")}) {
        const auto value = object.value(key);
        if (value.isString()) {
            const auto textValue = value.toString().trimmed();
            if (!textValue.isEmpty()) {
                return textValue;
            }
        }
    }

    return QString();
}

QString parseEdsmSystemIndexFromRawPayload(const QByteArray& payload) {
    // QJsonValue хранит числа как double, поэтому значения id64 > 2^53 могут
    // округляться. Для полей id64/systemId64/id читаем исходный JSON-токен.
    static const QRegularExpression kSystemIndexRegex(
        QStringLiteral("\"(?:id64|systemId64|id)\"\\s*:\\s*(?:\"([0-9]+)\"|([0-9]+))"));

    const auto payloadText = QString::fromUtf8(payload);
    const auto match = kSystemIndexRegex.match(payloadText);
    if (!match.hasMatch()) {
        return QString();
    }

    const auto quotedValue = match.captured(1);
    if (!quotedValue.isEmpty()) {
        return quotedValue;
    }

    return match.captured(2);
}

QString parseEdsmSystemIndex(const QJsonDocument& document, const QByteArray& payload) {
    if (document.isObject()) {
        const auto rootObject = document.object();
        auto index = readSystemIndexFromEdsmObject(rootObject);
        if (!index.isEmpty()) {
            return index;
        }

        index = readSystemIndexFromEdsmObject(rootObject.value(QStringLiteral("system")).toObject());
        if (!index.isEmpty()) {
            return index;
        }
    }

    if (document.isArray()) {
        const auto systems = document.array();
        if (!systems.isEmpty() && systems.first().isObject()) {
            return readSystemIndexFromEdsmObject(systems.first().toObject());
        }
    }

    return parseEdsmSystemIndexFromRawPayload(payload);
}

QVector<CelestialBody> parseSpanshBodies(const QJsonObject& rootObject) {
    QJsonArray bodiesArray = readArray(rootObject,
                                       {QStringLiteral("bodies"),
                                        QStringLiteral("systemBodies"),
                                        QStringLiteral("system_bodies"),
                                        QStringLiteral("body")});

    const auto systemObject = rootObject.value(QStringLiteral("system")).toObject();
    if (bodiesArray.isEmpty() && !systemObject.isEmpty()) {
        bodiesArray = readArray(systemObject,
                                {QStringLiteral("bodies"),
                                 QStringLiteral("systemBodies"),
                                 QStringLiteral("system_bodies"),
                                 QStringLiteral("body")});
    }

    const auto dataObject = rootObject.value(QStringLiteral("data")).toObject();
    if (bodiesArray.isEmpty() && !dataObject.isEmpty()) {
        bodiesArray = readArray(dataObject,
                                {QStringLiteral("bodies"),
                                 QStringLiteral("systemBodies"),
                                 QStringLiteral("system_bodies"),
                                 QStringLiteral("body")});

        if (bodiesArray.isEmpty()) {
            const auto nestedSystem = dataObject.value(QStringLiteral("system")).toObject();
            bodiesArray = readArray(nestedSystem,
                                    {QStringLiteral("bodies"),
                                     QStringLiteral("systemBodies"),
                                     QStringLiteral("system_bodies"),
                                     QStringLiteral("body")});
        }
    }

    if (bodiesArray.isEmpty()) {
        for (auto it = rootObject.constBegin(); it != rootObject.constEnd(); ++it) {
            if (!it.value().isArray()) {
                continue;
            }

            const auto candidate = it.value().toArray();
            if (candidate.isEmpty() || !candidate.first().isObject()) {
                continue;
            }

            const auto firstObject = candidate.first().toObject();
            if (firstObject.contains(QStringLiteral("id")) || firstObject.contains(QStringLiteral("bodyId"))
                || firstObject.contains(QStringLiteral("name"))) {
                bodiesArray = candidate;
                break;
            }
        }
    }

    QVector<CelestialBody> bodies;
    bodies.reserve(bodiesArray.size());

    QSet<int> existingBodyIds;
    QSet<int> barycenterBodyIds;
    for (const auto& bodyValue : bodiesArray) {
        if (!bodyValue.isObject()) {
            continue;
        }

        const auto bodyObj = bodyValue.toObject();
        const int bodyId = readInt(bodyObj, {QStringLiteral("bodyId"), QStringLiteral("id")});
        if (bodyId >= 0) {
            existingBodyIds.insert(bodyId);

            const QString bodyType = readString(bodyObj,
                                                {QStringLiteral("type"),
                                                 QStringLiteral("subType"),
                                                 QStringLiteral("sub_type"),
                                                 QStringLiteral("bodyType")});
            if (OrbitClassifier::isBarycenterType(bodyType)) {
                barycenterBodyIds.insert(bodyId);
            }
        }
    }

    for (const auto& bodyValue : bodiesArray) {
        if (!bodyValue.isObject()) {
            continue;
        }

        const auto bodyObj = bodyValue.toObject();
        CelestialBody body;

        body.id = readInt(bodyObj, {QStringLiteral("bodyId"), QStringLiteral("id")});
        body.name = readString(bodyObj, {QStringLiteral("name")});
        body.type = readString(bodyObj,
                               {QStringLiteral("type"),
                                QStringLiteral("subType"),
                                QStringLiteral("sub_type"),
                                QStringLiteral("bodyType")});
        body.bodyClass = classifyBodyClassFromType(body.type);
        body.distanceToArrivalLs = readDouble(bodyObj,
                                              {QStringLiteral("distanceToArrival"),
                                               QStringLiteral("distance_to_arrival")});

        // В Spansh semi-major axis обычно хранится в световых секундах,
        // в модели UI используется а.е., поэтому делим на 499.0047838.
        constexpr double lightSecondsPerAu = 499.0047838;
        const double semiMajorAxisLs = readDouble(bodyObj,
                                                  {QStringLiteral("semiMajorAxis"),
                                                   QStringLiteral("semi_major_axis")});
        body.semiMajorAxisAu = semiMajorAxisLs > 0.0 ? (semiMajorAxisLs / lightSecondsPerAu) : 0.0;
        body.physicalRadiusKm = readPhysicalRadiusKm(bodyObj);

        QVector<ParentRef> parentChain = parseParentChainFromArray(bodyObj.value(QStringLiteral("parents")));
        if (parentChain.isEmpty()) {
            parentChain = parseParentChainFromString(readString(bodyObj, {QStringLiteral("parents")}));
        }
        applyDirectParentFromChain(parentChain,
                                   existingBodyIds,
                                   barycenterBodyIds,
                                   body.id,
                                   [](const QString&) {},
                                   &body.parentId,
                                   &body.parentRelationType,
                                   &body.orbitsBarycenter);

        const bool hasInvalidParentRef = body.parentId >= 0
                                         && !isParentReferenceValid(body.parentId, existingBodyIds);
        if (body.parentId < 0 || hasInvalidParentRef) {
            QString fallbackDescription;
            if (resolveFallbackParent(bodyObj,
                                      existingBodyIds,
                                      &body.parentId,
                                      &body.parentRelationType,
                                      &fallbackDescription)) {
                body.orbitsBarycenter = isBarycenterRef(body.parentRelationType);
            } else if (hasInvalidParentRef) {
                body.parentId = -1;
                body.parentRelationType.clear();
                body.orbitsBarycenter = false;
            }
        }

        if (body.bodyClass == CelestialBody::BodyClass::Unknown) {
            if (body.parentRelationType.contains(QStringLiteral("Planet"), Qt::CaseInsensitive)) {
                body.bodyClass = CelestialBody::BodyClass::Moon;
            } else if (body.parentRelationType.contains(QStringLiteral("Star"), Qt::CaseInsensitive)) {
                body.bodyClass = CelestialBody::BodyClass::Planet;
            }
        }

        bodies.push_back(body);
    }

    prepareBodiesForGraph(&bodies,
                          [](const QString&) {},
                          QStringLiteral("SPANSH"));
    return bodies;
}

CelestialBody::BodyClass classifyEdastroBodyClass(const QString& collectionKey,
                                                 const QJsonObject& bodyObj,
                                                 const QString& bodyType) {
    const auto key = collectionKey.toLower();
    if (key.contains(QStringLiteral("star"))) {
        return CelestialBody::BodyClass::Star;
    }
    if (key.contains(QStringLiteral("planet"))) {
        return CelestialBody::BodyClass::Planet;
    }
    if (key.contains(QStringLiteral("moon"))) {
        return CelestialBody::BodyClass::Moon;
    }
    if (key.contains(QStringLiteral("bary"))) {
        return CelestialBody::BodyClass::Barycenter;
    }

    if (OrbitClassifier::isBarycenterType(bodyType)) {
        return CelestialBody::BodyClass::Barycenter;
    }
    if (bodyType.contains(QStringLiteral("Star"), Qt::CaseInsensitive)) {
        return CelestialBody::BodyClass::Star;
    }
    if (bodyType.contains(QStringLiteral("Moon"), Qt::CaseInsensitive)) {
        return CelestialBody::BodyClass::Moon;
    }
    if (bodyType.contains(QStringLiteral("Planet"), Qt::CaseInsensitive)
        || bodyType.contains(QStringLiteral("world"), Qt::CaseInsensitive)
        || bodyType.contains(QStringLiteral("giant"), Qt::CaseInsensitive)) {
        return CelestialBody::BodyClass::Planet;
    }

    const int parentPlanetId = readInt(bodyObj,
                                       {QStringLiteral("parentPlanetID"),
                                        QStringLiteral("parentPlanetId"),
                                        QStringLiteral("parent_planet_id")},
                                       0);
    if (parentPlanetId > 0) {
        return CelestialBody::BodyClass::Moon;
    }

    return CelestialBody::BodyClass::Unknown;
}

QVector<CelestialBody> parseEdastroBodiesFromObject(const QJsonObject& rootObject,
                                                    const QString& systemName,
                                                    const std::function<void(const QString&)>& onDebugInfo) {
    QVector<QPair<QString, QJsonObject>> rawBodies;

    auto appendBodies = [&rawBodies](const QString& key, const QJsonArray& part) {
        for (const auto& bodyValue : part) {
            if (bodyValue.isObject()) {
                rawBodies.push_back({key, bodyValue.toObject()});
            }
        }
    };

    const QStringList edastroCollectionKeys = {
        QStringLiteral("stars"),
        QStringLiteral("planets"),
        QStringLiteral("moons"),
        QStringLiteral("barycentres"),
        QStringLiteral("barycenters"),
        QStringLiteral("belts"),
        QStringLiteral("bodies"),
        QStringLiteral("systemBodies"),
        QStringLiteral("system_bodies"),
        QStringLiteral("body")
    };

    for (const auto& key : edastroCollectionKeys) {
        appendBodies(key, readArray(rootObject, {key}));
    }

    if (rawBodies.isEmpty()) {
        const auto dataObject = rootObject.value(QStringLiteral("data")).toObject();
        if (!dataObject.isEmpty()) {
            for (const auto& key : edastroCollectionKeys) {
                appendBodies(key, readArray(dataObject, {key}));
            }
        }
    }

    QVector<CelestialBody> bodies;
    bodies.reserve(rawBodies.size());
    QHash<int, QVector<ParentRef>> parentsByBodyId;
    QSet<int> existingBodyIds;
    QSet<int> barycenterBodyIds;

    for (const auto& [collectionKey, bodyObj] : rawBodies) {
        const int bodyId = readInt(bodyObj, {QStringLiteral("bodyId"), QStringLiteral("id")});
        if (bodyId >= 0) {
            existingBodyIds.insert(bodyId);

            const QString bodyType = readString(bodyObj,
                                                {QStringLiteral("type"),
                                                 QStringLiteral("subType"),
                                                 QStringLiteral("sub_type"),
                                                 QStringLiteral("bodyType"),
                                                 QStringLiteral("body_type")});
            if (classifyEdastroBodyClass(collectionKey, bodyObj, bodyType) == CelestialBody::BodyClass::Barycenter) {
                barycenterBodyIds.insert(bodyId);
            }
        }
    }

    for (const auto& [collectionKey, bodyObj] : rawBodies) {
        CelestialBody body;

        body.id = readInt(bodyObj, {QStringLiteral("bodyId"), QStringLiteral("id")});
        body.name = readString(bodyObj, {QStringLiteral("name")});
        body.type = readString(bodyObj,
                               {QStringLiteral("type"),
                                QStringLiteral("subType"),
                                QStringLiteral("sub_type"),
                                QStringLiteral("bodyType"),
                                QStringLiteral("body_type")});
        body.distanceToArrivalLs = readDouble(bodyObj,
                                              {QStringLiteral("distanceToArrival"),
                                               QStringLiteral("distance_to_arrival"),
                                               QStringLiteral("distanceToArrivalLs"),
                                               QStringLiteral("distanceToArrivalLS")});

        body.semiMajorAxisAu = readDouble(bodyObj,
                                          {QStringLiteral("semiMajorAxis"),
                                           QStringLiteral("semi_major_axis")});
        if (body.semiMajorAxisAu <= 0.0) {
            constexpr double lightSecondsPerAu = 499.0047838;
            const double semiMajorAxisLs = readDouble(bodyObj,
                                                      {QStringLiteral("semiMajorAxisLs"),
                                                       QStringLiteral("semi_major_axis_ls")});
            body.semiMajorAxisAu = semiMajorAxisLs > 0.0 ? (semiMajorAxisLs / lightSecondsPerAu) : 0.0;
        }
        body.physicalRadiusKm = readPhysicalRadiusKm(bodyObj);

        body.bodyClass = classifyEdastroBodyClass(collectionKey, bodyObj, body.type);
        body.orbitsBarycenter = (body.bodyClass == CelestialBody::BodyClass::Barycenter);

        QVector<ParentRef> parentChain = parseParentChainFromString(readString(bodyObj, {QStringLiteral("parents")}));
        if (parentChain.isEmpty()) {
            parentChain = parseParentChainFromArray(bodyObj.value(QStringLiteral("parents")));
        }
        applyDirectParentFromChain(parentChain,
                                   existingBodyIds,
                                   barycenterBodyIds,
                                   body.id,
                                   onDebugInfo,
                                   &body.parentId,
                                   &body.parentRelationType,
                                   &body.orbitsBarycenter);

        const int parsedParentId = body.parentId;
        const QString parsedParentType = body.parentRelationType;
        const bool hasInvalidParentRef = parsedParentId >= 0
                                         && !isParentReferenceValid(parsedParentId, existingBodyIds);
        if (parsedParentId < 0 || hasInvalidParentRef) {
            QString fallbackDescription = QStringLiteral("<none>");
            if (resolveFallbackParent(bodyObj,
                                      existingBodyIds,
                                      &body.parentId,
                                      &body.parentRelationType,
                                      &fallbackDescription)) {
                body.orbitsBarycenter = isBarycenterRef(body.parentRelationType);
            } else if (hasInvalidParentRef) {
                body.parentId = -1;
                body.parentRelationType.clear();
                body.orbitsBarycenter = false;
            }

            if (hasInvalidParentRef) {
                onDebugInfo(QStringLiteral("[EDASTRO][WARN] Inconsistent parent reference: system='%1', bodyId=%2, parsedParent=%3:%4, parents='%5', fallback=%6")
                                .arg(systemName,
                                     QString::number(body.id),
                                     parsedParentType,
                                     QString::number(parsedParentId),
                                     parentChainToString(parentChain),
                                     fallbackDescription));
            }
        }

        if (body.bodyClass == CelestialBody::BodyClass::Unknown) {
            if (body.parentRelationType.contains(QStringLiteral("Planet"), Qt::CaseInsensitive)) {
                body.bodyClass = CelestialBody::BodyClass::Moon;
            } else if (body.parentRelationType.contains(QStringLiteral("Star"), Qt::CaseInsensitive)) {
                body.bodyClass = CelestialBody::BodyClass::Planet;
            }
        }

        if (body.id >= 0) {
            parentsByBodyId.insert(body.id, parentChain);
        }

        bodies.push_back(body);
    }

    synthesizeMissingBarycenters(&bodies, parentsByBodyId);
    buildBarycenterHierarchy(&bodies, parentsByBodyId, systemName, onDebugInfo);

    QSet<int> barycenterIds;
    for (const auto& body : bodies) {
        if (body.bodyClass == CelestialBody::BodyClass::Barycenter && body.id >= 0) {
            barycenterIds.insert(body.id);
        }
    }
    validateEdastroParentChains(bodies, parentsByBodyId, barycenterIds, systemName, onDebugInfo);
    prepareBodiesForGraph(&bodies, onDebugInfo, QStringLiteral("EDASTRO"));

    return bodies;
}

QVector<CelestialBody> parseEdastroBodies(const QJsonDocument& document,
                                          const QString& defaultSystemName,
                                          const std::function<void(const QString&)>& onDebugInfo) {
    if (document.isObject()) {
        const auto rootObject = document.object();

        // /api/starsystem может вернуть данные как объект системы либо как контейнер с массивом систем.
        if (rootObject.contains(QStringLiteral("name")) || rootObject.contains(QStringLiteral("bodies"))
            || rootObject.contains(QStringLiteral("systemBodies"))) {
            const auto parsedSystemName = readString(rootObject, {QStringLiteral("name")});
            const auto directBodies = parseEdastroBodiesFromObject(rootObject,
                                                                   parsedSystemName.isEmpty() ? defaultSystemName : parsedSystemName,
                                                                   onDebugInfo);
            if (!directBodies.isEmpty()) {
                return directBodies;
            }
        }

        for (auto it = rootObject.constBegin(); it != rootObject.constEnd(); ++it) {
            if (!it.value().isArray()) {
                continue;
            }

            const auto systemsArray = it.value().toArray();
            if (systemsArray.isEmpty() || !systemsArray.first().isObject()) {
                continue;
            }

            const auto firstObject = systemsArray.first().toObject();
            const auto parsedSystemName = readString(firstObject, {QStringLiteral("name")});
            const auto candidateBodies = parseEdastroBodiesFromObject(firstObject,
                                                                      parsedSystemName.isEmpty() ? defaultSystemName : parsedSystemName,
                                                                      onDebugInfo);
            if (!candidateBodies.isEmpty()) {
                return candidateBodies;
            }
        }

        return {};
    }

    if (!document.isArray()) {
        return {};
    }

    const auto systemsArray = document.array();
    if (systemsArray.isEmpty() || !systemsArray.first().isObject()) {
        return {};
    }

    const auto firstObject = systemsArray.first().toObject();
    const auto parsedSystemName = readString(firstObject, {QStringLiteral("name")});
    return parseEdastroBodiesFromObject(firstObject,
                                        parsedSystemName.isEmpty() ? defaultSystemName : parsedSystemName,
                                        onDebugInfo);
}

QVector<CelestialBody> mergeBodies(const QVector<CelestialBody>& edsmBodies,
                                   const QVector<CelestialBody>& spanshBodies,
                                   bool* outHadConflict) {
    *outHadConflict = false;

    QHash<int, CelestialBody> mergedById;
    mergedById.reserve(edsmBodies.size() + spanshBodies.size());

    for (const auto& body : edsmBodies) {
        if (body.id >= 0) {
            mergedById.insert(body.id, body);
        }
    }

    for (const auto& body : spanshBodies) {
        if (body.id < 0) {
            continue;
        }

        if (!mergedById.contains(body.id)) {
            mergedById.insert(body.id, body);
            continue;
        }

        const auto& existing = mergedById.value(body.id);
        if ((!existing.name.isEmpty() && existing.name != body.name) || (!existing.type.isEmpty() && existing.type != body.type)) {
            *outHadConflict = true;
        }

        // По требованиям используем Spansh как приоритетный источник,
        // поэтому при совпадении идентификатора заменяем EDSM-запись.
        mergedById.insert(body.id, body);
    }

    for (const auto& body : spanshBodies) {
        if (body.parentId >= 0 && !mergedById.contains(body.parentId)) {
            CelestialBody syntheticParent;
            syntheticParent.id = body.parentId;
            syntheticParent.name = QStringLiteral("Body %1").arg(body.parentId);
            syntheticParent.type = QStringLiteral("Unknown");
            mergedById.insert(syntheticParent.id, syntheticParent);
        }
    }

    QVector<CelestialBody> merged;
    merged.reserve(mergedById.size());
    for (auto it = mergedById.cbegin(); it != mergedById.cend(); ++it) {
        merged.push_back(it.value());
    }

    return merged;
}

} // namespace

QVector<CelestialBody> parseEdastroBodiesForTests(const QJsonDocument& document,
                                                  const QString& defaultSystemName,
                                                  const std::function<void(const QString&)>& onDebugInfo) {
    return parseEdastroBodies(document, defaultSystemName, onDebugInfo);
}

EdsmApiClient::EdsmApiClient(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

void requestSpanshBodiesBySystemIndex(QNetworkAccessManager* networkManager,
                                      QObject* context,
                                      const QString& systemName,
                                      const QString& systemIndex,
                                      const std::function<void(const QVector<CelestialBody>&, const QString&)>& onFinished,
                                      const std::function<void(const QString&)>& onDebugInfo,
                                      const QString& modeLabel = QString()) {
    QUrl url(QStringLiteral("https://spansh.co.uk/api/dump/%1").arg(QUrl::toPercentEncoding(systemIndex).constData()));
    if (modeLabel.isEmpty()) {
        onDebugInfo(QStringLiteral("[SPANSH] Отправка запроса. systemName='%1', systemIndex='%2', url=%3")
                        .arg(systemName, systemIndex, url.toString()));
    } else {
        onDebugInfo(QStringLiteral("[SPANSH] Отправка запроса. mode=%1, systemName='%2', systemIndex='%3', url=%4")
                        .arg(modeLabel, systemName, systemIndex, url.toString()));
    }

    auto* reply = networkManager->get(QNetworkRequest(url));
    auto* timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(kRequestTimeoutMs);

    QObject::connect(timeoutTimer, &QTimer::timeout, context, [reply]() {
        if (!reply->isRunning()) {
            return;
        }

        reply->setProperty("timedOut", true);
        reply->abort();
    });
    timeoutTimer->start();

    QObject::connect(reply, &QNetworkReply::finished, context, [reply, timeoutTimer, onFinished, onDebugInfo, modeLabel]() {
        timeoutTimer->stop();
        reply->deleteLater();

        const int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (modeLabel.isEmpty()) {
            onDebugInfo(QStringLiteral("[SPANSH] Ответ получен. status=%1, networkError=%2")
                            .arg(httpStatusCode)
                            .arg(reply->error()));
        } else {
            onDebugInfo(QStringLiteral("[SPANSH] Ответ получен. mode=%1, status=%2, networkError=%3")
                            .arg(modeLabel)
                            .arg(httpStatusCode)
                            .arg(reply->error()));
        }

        if (reply->property("timedOut").toBool()) {
            onFinished({}, QStringLiteral("Превышено время ожидания ответа Spansh"));
            return;
        }

        if (httpStatusCode == 404 || httpStatusCode == 422) {
            onFinished({}, QStringLiteral("Система не найдена в Spansh"));
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            onFinished({}, reply->errorString());
            return;
        }

        const auto payload = reply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            onFinished({}, QStringLiteral("Ответ Spansh имеет неверный формат."));
            return;
        }

        const auto rootObject = document.object();
        if (modeLabel.isEmpty()) {
            onDebugInfo(QStringLiteral("[SPANSH] Корневые ключи JSON: %1")
                            .arg(rootObject.keys().join(QStringLiteral(", "))));
        } else {
            onDebugInfo(QStringLiteral("[SPANSH] Корневые ключи JSON. mode=%1, keys=%2")
                            .arg(modeLabel, rootObject.keys().join(QStringLiteral(", "))));
        }

        auto bodies = parseSpanshBodies(rootObject);
        reportLsToAuSanityWarnings(bodies, QStringLiteral("SPANSH"), onDebugInfo);
        if (modeLabel.isEmpty()) {
            onDebugInfo(QStringLiteral("[SPANSH] Распарсено тел: %1")
                            .arg(bodies.size()));
        } else {
            onDebugInfo(QStringLiteral("[SPANSH] Ответ обработан. mode=%1, bodies=%2")
                            .arg(modeLabel)
                            .arg(bodies.size()));
        }

        if (bodies.isEmpty()) {
            const auto apiMessage = readMessageField(rootObject);
            if (!apiMessage.isEmpty()) {
                onFinished({}, apiMessage);
                return;
            }
        }

        onFinished(bodies, QString());
    });
}

void EdsmApiClient::requestSpanshSystemBodies(const QString& systemName) {
    const auto trimmedSystemName = systemName.trimmed();
    if (trimmedSystemName.isEmpty()) {
        emit requestFailed(QStringLiteral("Название системы не может быть пустым."));
        return;
    }

    emit requestStateChanged(QStringLiteral("Получение индекса системы из EDSM для запроса к Spansh..."));

    QUrl edsmSystemUrl(QStringLiteral("https://www.edsm.net/api-v1/system"));
    QUrlQuery edsmSystemQuery;
    edsmSystemQuery.addQueryItem(QStringLiteral("systemName"), trimmedSystemName);
    edsmSystemQuery.addQueryItem(QStringLiteral("showCoordinates"), QStringLiteral("0"));
    edsmSystemQuery.addQueryItem(QStringLiteral("showId"), QStringLiteral("1"));
    edsmSystemUrl.setQuery(edsmSystemQuery);

    emit requestDebugInfo(QStringLiteral("[EDSM] Запрос индекса системы. systemName='%1', url=%2")
                              .arg(trimmedSystemName, edsmSystemUrl.toString()));

    auto* reply = m_networkManager->get(QNetworkRequest(edsmSystemUrl));

    auto* timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(kRequestTimeoutMs);

    connect(timeoutTimer, &QTimer::timeout, this, [this, reply]() {
        if (!reply->isRunning()) {
            return;
        }

        reply->setProperty("timedOut", true);
        reply->abort();
        emit requestStateChanged(QStringLiteral("Истекло время ожидания ответа EDSM при запросе индекса."));
        emit requestFailed(QStringLiteral("Превышено время ожидания ответа EDSM"));
    });
    timeoutTimer->start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, trimmedSystemName, timeoutTimer]() {
        timeoutTimer->stop();
        reply->deleteLater();

        const int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit requestDebugInfo(QStringLiteral("[EDSM] Ответ на запрос индекса получен. status=%1, networkError=%2")
                                  .arg(httpStatusCode)
                                  .arg(reply->error()));

        if (reply->property("timedOut").toBool()) {
            return;
        }

        if (httpStatusCode == 404 || httpStatusCode == 422) {
            emit requestFailed(QStringLiteral("Система не найдена в EDSM"));
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(reply->errorString());
            return;
        }

        const auto payload = reply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject() && !document.isArray()) {
            emit requestFailed(QStringLiteral("Ответ EDSM при запросе индекса имеет неверный формат."));
            return;
        }

        const auto systemIndex = parseEdsmSystemIndex(document, payload);
        if (systemIndex.isEmpty()) {
            emit requestFailed(QStringLiteral("EDSM не вернул индекс системы для запроса к Spansh."));
            return;
        }

        emit requestStateChanged(QStringLiteral("Индекс системы получен (%1). Запрос к Spansh отправлен...").arg(systemIndex));

        requestSpanshBodiesBySystemIndex(m_networkManager,
                                         this,
                                         trimmedSystemName,
                                         systemIndex,
                                         [this, trimmedSystemName](const QVector<CelestialBody>& spanshBodies,
                                                                   const QString& spanshError) {
                                             if (!spanshError.isEmpty()) {
                                                 emit requestFailed(spanshError);
                                                 return;
                                             }

                                             SystemBodiesResult result;
                                             result.systemName = trimmedSystemName;
                                             result.bodies = spanshBodies;
                                             result.selectedSource = SystemDataSource::Spansh;
                                             result.hasSpanshData = !result.bodies.isEmpty();
                                             emit systemBodiesReady(result);
                                         },
                                         [this](const QString& message) {
                                             emit requestDebugInfo(message);
                                         });
    });
}

void EdsmApiClient::requestEdastroSystemBodies(const QString& systemName) {
    const auto trimmedSystemName = systemName.trimmed();
    if (trimmedSystemName.isEmpty()) {
        emit requestFailed(QStringLiteral("Название системы не может быть пустым."));
        return;
    }

    QUrl url(QStringLiteral("https://edastro.com/api/starsystem"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), trimmedSystemName);
    url.setQuery(query);

    emit requestStateChanged(QStringLiteral("Запрос к EDAstro отправлен..."));
    emit requestDebugInfo(QStringLiteral("[EDASTRO] Отправка запроса. systemName='%1', url=%2")
                              .arg(trimmedSystemName, url.toString()));

    auto* reply = m_networkManager->get(QNetworkRequest(url));
    auto* timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(kRequestTimeoutMs);
    connect(timeoutTimer, &QTimer::timeout, this, [reply]() {
        if (!reply->isRunning()) {
            return;
        }

        reply->setProperty("timedOut", true);
        reply->abort();
    });
    timeoutTimer->start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, timeoutTimer, trimmedSystemName]() {
        timeoutTimer->stop();
        reply->deleteLater();

        if (reply->property("timedOut").toBool()) {
            emit requestFailed(QStringLiteral("Превышено время ожидания ответа EDAstro"));
            return;
        }

        const int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit requestDebugInfo(QStringLiteral("[EDASTRO] Ответ получен. status=%1, networkError=%2")
                                  .arg(httpStatusCode)
                                  .arg(reply->error()));

        if (httpStatusCode == 404 || httpStatusCode == 422) {
            emit requestFailed(QStringLiteral("Система не найдена в EDAstro"));
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(reply->errorString());
            return;
        }

        const auto payload = reply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject() && !document.isArray()) {
            emit requestFailed(QStringLiteral("Ответ EDAstro имеет неверный формат."));
            return;
        }

        auto bodies = parseEdastroBodies(document,
                                         trimmedSystemName,
                                         [this](const QString& message) { emit requestDebugInfo(message); });
        const bool hierarchyValid = prepareBodiesForGraph(&bodies,
                                                          [this](const QString& message) { emit requestDebugInfo(message); },
                                                          QStringLiteral("EDASTRO"));
        reportLsToAuSanityWarnings(bodies,
                                   QStringLiteral("EDASTRO"),
                                   [this](const QString& message) { emit requestDebugInfo(message); });
        if (bodies.isEmpty()) {
            emit requestFailed(QStringLiteral("EDAstro вернул пустой список тел или неизвестный формат полей."));
            return;
        }

        if (!hierarchyValid) {
            emit requestFailed(QStringLiteral("Иерархия системы некорректна: не для всех тел найден путь до Star:* или Null:0."));
            return;
        }

        SystemBodiesResult result;
        result.systemName = trimmedSystemName;
        result.bodies = bodies;
        result.selectedSource = SystemDataSource::Edastro;
        result.hasEdastroData = true;
        emit systemBodiesReady(result);
    });
}

void EdsmApiClient::requestSystemBodies(const QString& systemName, const SystemRequestMode mode) {
    const auto trimmedSystemName = systemName.trimmed();
    if (trimmedSystemName.isEmpty()) {
        emit requestFailed(QStringLiteral("Название системы не может быть пустым."));
        return;
    }

    if (mode == SystemRequestMode::SpanshOnly) {
        requestSpanshSystemBodies(trimmedSystemName);
        return;
    }

    if (mode == SystemRequestMode::EdastroOnly) {
        requestEdastroSystemBodies(trimmedSystemName);
        return;
    }

    struct RequestAggregationState {
        bool edsmDone = false;
        bool spanshDone = false;
        bool edsmTimedOut = false;
        bool spanshTimedOut = false;
        bool edsmParsed = false;
        bool spanshParsed = false;
        QString edsmError;
        QString spanshError;
        QVector<CelestialBody> edsmBodies;
        QVector<CelestialBody> spanshBodies;
    };

    const bool autoMergeMode = (mode == SystemRequestMode::AutoMerge);
    auto state = QSharedPointer<RequestAggregationState>::create();

    auto finalizeRequest = [this, mode, trimmedSystemName, state]() {
        const bool ready = (mode == SystemRequestMode::AutoMerge) ? (state->edsmDone && state->spanshDone) : state->edsmDone;
        if (!ready) {
            return;
        }

        SystemBodiesResult result;
        result.systemName = trimmedSystemName;
        result.hasEdsmData = !state->edsmBodies.isEmpty();
        result.hasSpanshData = !state->spanshBodies.isEmpty();

        if (mode == SystemRequestMode::AutoMerge) {
            if (state->edsmParsed && state->spanshParsed) {
                result.selectedSource = SystemDataSource::Merged;
                result.bodies = mergeBodies(state->edsmBodies, state->spanshBodies, &result.hadConflict);
            } else if (state->edsmParsed) {
                result.selectedSource = SystemDataSource::Edsm;
                result.bodies = state->edsmBodies;
            } else if (state->spanshParsed) {
                result.selectedSource = SystemDataSource::Spansh;
                result.bodies = state->spanshBodies;
            }
        } else {
            result.selectedSource = SystemDataSource::Edsm;
            result.bodies = state->edsmBodies;
        }

        const bool hierarchyValid = result.bodies.isEmpty()
            ? false
            : prepareBodiesForGraph(&result.bodies,
                                    [this](const QString& message) { emit requestDebugInfo(message); },
                                    sourceToText(result.selectedSource));

        emit requestDebugInfo(QStringLiteral("[SUMMARY] mode=%1, EDSM(done=%2, parsed=%3, timedOut=%4, bodies=%5, error='%6'), "
                                            "Spansh(done=%7, parsed=%8, timedOut=%9, bodies=%10, error='%11')")
                                  .arg(modeToText(mode))
                                  .arg(state->edsmDone)
                                  .arg(state->edsmParsed)
                                  .arg(state->edsmTimedOut)
                                  .arg(state->edsmBodies.size())
                                  .arg(state->edsmError)
                                  .arg(state->spanshDone)
                                  .arg(state->spanshParsed)
                                  .arg(state->spanshTimedOut)
                                  .arg(state->spanshBodies.size())
                                  .arg(state->spanshError));

        if (result.bodies.isEmpty()) {
            QString failureReason;
            if (!state->edsmError.isEmpty() && !state->spanshError.isEmpty()) {
                failureReason = QStringLiteral("EDSM: %1; Spansh: %2").arg(state->edsmError, state->spanshError);
            } else if (!state->edsmError.isEmpty()) {
                failureReason = QStringLiteral("EDSM: %1").arg(state->edsmError);
            } else if (!state->spanshError.isEmpty()) {
                failureReason = QStringLiteral("Spansh: %1").arg(state->spanshError);
            } else {
                failureReason = QStringLiteral("Оба источника вернули пустой список тел.");
            }

            emit requestStateChanged(QStringLiteral("Не удалось получить тела системы."));
            emit requestFailed(failureReason);
            return;
        }

        if (!hierarchyValid) {
            emit requestStateChanged(QStringLiteral("Иерархия системы некорректна."));
            emit requestFailed(QStringLiteral("Иерархия системы некорректна: не для всех тел найден путь до Star:* или Null:0."));
            return;
        }

        emit requestStateChanged(QStringLiteral("Данные получены из %1. Тел: %2 (EDSM=%3, Spansh=%4)")
                                     .arg(sourceToText(result.selectedSource))
                                     .arg(result.bodies.size())
                                     .arg(result.hasEdsmData)
                                     .arg(result.hasSpanshData));
        emit systemBodiesReady(result);
    };

    QUrl edsmUrl(QStringLiteral("https://www.edsm.net/api-system-v1/bodies"));
    QUrlQuery edsmQuery;
    edsmQuery.addQueryItem(QStringLiteral("systemName"), trimmedSystemName);
    edsmUrl.setQuery(edsmQuery);

    emit requestStateChanged(autoMergeMode
                                 ? QStringLiteral("Запрос к EDSM отправлен. Для Spansh ожидается индекс системы из EDSM...")
                                 : QStringLiteral("Запрос к EDSM отправлен..."));
    emit requestDebugInfo(QStringLiteral("[EDSM] Отправка запроса. mode=%1, systemName='%2', url=%3")
                              .arg(modeToText(mode), trimmedSystemName, edsmUrl.toString()));

    auto* edsmReply = m_networkManager->get(QNetworkRequest(edsmUrl));
    auto* edsmTimeoutTimer = new QTimer(edsmReply);
    edsmTimeoutTimer->setSingleShot(true);
    edsmTimeoutTimer->setInterval(kRequestTimeoutMs);
    connect(edsmTimeoutTimer, &QTimer::timeout, this, [this, edsmReply, mode, state]() {
        if (!edsmReply->isRunning()) {
            return;
        }

        state->edsmTimedOut = true;
        edsmReply->setProperty("timedOut", true);
        edsmReply->abort();
        emit requestDebugInfo(QStringLiteral("[EDSM] Таймаут запроса. mode=%1, url=%2")
                                  .arg(modeToText(mode), edsmReply->url().toString()));
    });
    edsmTimeoutTimer->start();

    connect(edsmReply, &QNetworkReply::finished, this, [this, edsmReply, edsmTimeoutTimer, mode, state, finalizeRequest]() {
        edsmTimeoutTimer->stop();
        edsmReply->deleteLater();
        state->edsmDone = true;

        if (edsmReply->property("timedOut").toBool()) {
            state->edsmError = QStringLiteral("Превышено время ожидания ответа EDSM");
            finalizeRequest();
            return;
        }

        if (edsmReply->error() != QNetworkReply::NoError) {
            state->edsmError = edsmReply->errorString();
            emit requestDebugInfo(QStringLiteral("[EDSM] Сетевая ошибка. mode=%1, error=%2")
                                      .arg(modeToText(mode), state->edsmError));
            finalizeRequest();
            return;
        }

        const auto payload = edsmReply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            state->edsmError = QStringLiteral("Ответ EDSM имеет неверный формат.");
            emit requestDebugInfo(QStringLiteral("[EDSM] Ошибка парсинга. mode=%1")
                                      .arg(modeToText(mode)));
            finalizeRequest();
            return;
        }

        state->edsmParsed = true;
        state->edsmBodies = parseEdsmBodies(document.object());
        reportLsToAuSanityWarnings(state->edsmBodies,
                                   QStringLiteral("EDSM"),
                                   [this](const QString& message) { emit requestDebugInfo(message); });
        emit requestDebugInfo(QStringLiteral("[EDSM] Ответ обработан. mode=%1, bodies=%2")
                                  .arg(modeToText(mode))
                                  .arg(state->edsmBodies.size()));
        finalizeRequest();
    });

    if (!autoMergeMode) {
        return;
    }

    QUrl edsmSystemUrl(QStringLiteral("https://www.edsm.net/api-v1/system"));
    QUrlQuery edsmSystemQuery;
    edsmSystemQuery.addQueryItem(QStringLiteral("systemName"), trimmedSystemName);
    edsmSystemQuery.addQueryItem(QStringLiteral("showCoordinates"), QStringLiteral("0"));
    edsmSystemQuery.addQueryItem(QStringLiteral("showId"), QStringLiteral("1"));
    edsmSystemUrl.setQuery(edsmSystemQuery);

    emit requestDebugInfo(QStringLiteral("[EDSM] Запрос индекса системы. mode=%1, systemName='%2', url=%3")
                              .arg(modeToText(mode), trimmedSystemName, edsmSystemUrl.toString()));

    auto* resolveReply = m_networkManager->get(QNetworkRequest(edsmSystemUrl));
    auto* resolveTimeoutTimer = new QTimer(resolveReply);
    resolveTimeoutTimer->setSingleShot(true);
    resolveTimeoutTimer->setInterval(kRequestTimeoutMs);
    connect(resolveTimeoutTimer, &QTimer::timeout, this, [resolveReply, state]() {
        if (!resolveReply->isRunning()) {
            return;
        }

        state->spanshTimedOut = true;
        resolveReply->setProperty("timedOut", true);
        resolveReply->abort();
    });
    resolveTimeoutTimer->start();

    connect(resolveReply, &QNetworkReply::finished, this, [this,
                                                            resolveReply,
                                                            resolveTimeoutTimer,
                                                            mode,
                                                            trimmedSystemName,
                                                            state,
                                                            finalizeRequest]() {
        resolveTimeoutTimer->stop();
        resolveReply->deleteLater();

        if (resolveReply->property("timedOut").toBool()) {
            state->spanshDone = true;
            state->spanshError = QStringLiteral("Превышено время ожидания ответа EDSM при запросе индекса для Spansh");
            emit requestDebugInfo(QStringLiteral("[EDSM] Таймаут запроса индекса. mode=%1")
                                      .arg(modeToText(mode)));
            finalizeRequest();
            return;
        }

        const int httpStatusCode = resolveReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        emit requestDebugInfo(QStringLiteral("[EDSM] Ответ на запрос индекса получен. mode=%1, status=%2, networkError=%3")
                                  .arg(modeToText(mode))
                                  .arg(httpStatusCode)
                                  .arg(resolveReply->error()));

        if (httpStatusCode == 404 || httpStatusCode == 422) {
            state->spanshDone = true;
            state->spanshError = QStringLiteral("Система не найдена в EDSM (индекс для Spansh не получен)");
            finalizeRequest();
            return;
        }

        if (resolveReply->error() != QNetworkReply::NoError) {
            state->spanshDone = true;
            state->spanshError = QStringLiteral("Не удалось получить индекс системы из EDSM: %1").arg(resolveReply->errorString());
            finalizeRequest();
            return;
        }

        const auto payload = resolveReply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject() && !document.isArray()) {
            state->spanshDone = true;
            state->spanshError = QStringLiteral("Ответ EDSM при запросе индекса имеет неверный формат.");
            finalizeRequest();
            return;
        }

        const auto systemIndex = parseEdsmSystemIndex(document, payload);
        if (systemIndex.isEmpty()) {
            state->spanshDone = true;
            state->spanshError = QStringLiteral("EDSM не вернул индекс системы для запроса к Spansh.");
            finalizeRequest();
            return;
        }

        requestSpanshBodiesBySystemIndex(m_networkManager,
                                         this,
                                         trimmedSystemName,
                                         systemIndex,
                                         [state, finalizeRequest](const QVector<CelestialBody>& spanshBodies,
                                                                  const QString& spanshError) {
                                             state->spanshDone = true;
                                             state->spanshError = spanshError;
                                             if (spanshError == QStringLiteral("Превышено время ожидания ответа Spansh")) {
                                                 state->spanshTimedOut = true;
                                             }
                                             if (spanshError.isEmpty()) {
                                                 state->spanshParsed = true;
                                                 state->spanshBodies = spanshBodies;
                                             }
                                             finalizeRequest();
                                         },
                                         [this](const QString& message) {
                                             emit requestDebugInfo(message);
                                         },
                                         modeToText(mode));
    });
}

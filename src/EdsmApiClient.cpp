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
#include <QSharedPointer>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr int kRequestTimeoutMs = 15000;

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

        *outRelationType = relationType;
        *outParentId = relationId;
        *outOrbitsBarycenter = relationType.contains(QStringLiteral("Null"), Qt::CaseInsensitive)
                               || relationType.contains(QStringLiteral("Bary"), Qt::CaseInsensitive);
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
    if (parentsArray.isEmpty() || !parentsArray.first().isObject()) {
        return false;
    }

    const auto parentObject = parentsArray.first().toObject();
    if (parentObject.isEmpty()) {
        return false;
    }

    const auto relation = parentObject.constBegin();
    *outRelationType = relation.key();
    *outParentId = relation.value().toInt(-1);
    *outOrbitsBarycenter = relation.key().contains(QStringLiteral("Null"), Qt::CaseInsensitive);
    return true;
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

        parseParentFromArray(bodyObj.value(QStringLiteral("parents")),
                             &body.parentId,
                             &body.parentRelationType,
                             &body.orbitsBarycenter);

        if (body.parentId < 0) {
            parseParentFromString(readString(bodyObj, {QStringLiteral("parents")}),
                                  &body.parentId,
                                  &body.parentRelationType,
                                  &body.orbitsBarycenter);
        }

        if (body.parentId < 0 && bodyObj.value(QStringLiteral("parent")).isObject()) {
            const auto parentObject = bodyObj.value(QStringLiteral("parent")).toObject();
            body.parentId = readInt(parentObject, {QStringLiteral("bodyId"), QStringLiteral("id")});
            body.parentRelationType = readString(parentObject,
                                                 {QStringLiteral("relationType"),
                                                  QStringLiteral("relation_type"),
                                                  QStringLiteral("type")});
            body.orbitsBarycenter = body.parentRelationType.contains(QStringLiteral("Null"), Qt::CaseInsensitive);
        }

        if (body.parentId < 0) {
            const int fallbackParentPlanetId = readInt(bodyObj,
                                                       {QStringLiteral("parentPlanetID"),
                                                        QStringLiteral("parentPlanetId"),
                                                        QStringLiteral("parent_planet_id")},
                                                       0);
            const int fallbackParentStarId = readInt(bodyObj,
                                                     {QStringLiteral("parentStarID"),
                                                      QStringLiteral("parentStarId"),
                                                      QStringLiteral("parent_star_id")},
                                                     0);

            if (fallbackParentPlanetId > 0) {
                body.parentId = fallbackParentPlanetId;
                if (body.parentRelationType.isEmpty()) {
                    body.parentRelationType = QStringLiteral("Planet");
                }
            } else if (fallbackParentStarId > 0) {
                body.parentId = fallbackParentStarId;
                if (body.parentRelationType.isEmpty()) {
                    body.parentRelationType = QStringLiteral("Star");
                }
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

QVector<CelestialBody> parseEdastroBodiesFromObject(const QJsonObject& rootObject) {
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

        constexpr double lightSecondsPerAu = 499.0047838;
        const double semiMajorAxisLs = readDouble(bodyObj,
                                                  {QStringLiteral("semiMajorAxis"),
                                                   QStringLiteral("semi_major_axis"),
                                                   QStringLiteral("semiMajorAxisLs")});
        body.semiMajorAxisAu = semiMajorAxisLs > 0.0 ? (semiMajorAxisLs / lightSecondsPerAu) : 0.0;
        body.physicalRadiusKm = readPhysicalRadiusKm(bodyObj);

        body.bodyClass = classifyEdastroBodyClass(collectionKey, bodyObj, body.type);
        body.orbitsBarycenter = (body.bodyClass == CelestialBody::BodyClass::Barycenter);

        parseParentFromArray(bodyObj.value(QStringLiteral("parents")),
                             &body.parentId,
                             &body.parentRelationType,
                             &body.orbitsBarycenter);

        if (body.parentId < 0) {
            parseParentFromString(readString(bodyObj, {QStringLiteral("parents")}),
                                  &body.parentId,
                                  &body.parentRelationType,
                                  &body.orbitsBarycenter);
        }

        if (body.parentId < 0 && bodyObj.value(QStringLiteral("parent")).isObject()) {
            const auto parentObject = bodyObj.value(QStringLiteral("parent")).toObject();
            body.parentId = readInt(parentObject, {QStringLiteral("bodyId"), QStringLiteral("id")});
            body.parentRelationType = readString(parentObject,
                                                 {QStringLiteral("relationType"),
                                                  QStringLiteral("relation_type"),
                                                  QStringLiteral("type")});
            body.orbitsBarycenter = body.parentRelationType.contains(QStringLiteral("Null"), Qt::CaseInsensitive);
        }

        if (body.parentId < 0) {
            const int fallbackParentPlanetId = readInt(bodyObj,
                                                       {QStringLiteral("parentPlanetID"),
                                                        QStringLiteral("parentPlanetId"),
                                                        QStringLiteral("parent_planet_id")},
                                                       0);
            const int fallbackParentStarId = readInt(bodyObj,
                                                     {QStringLiteral("parentStarID"),
                                                      QStringLiteral("parentStarId"),
                                                      QStringLiteral("parent_star_id")},
                                                     0);

            if (fallbackParentPlanetId > 0) {
                body.parentId = fallbackParentPlanetId;
                if (body.parentRelationType.isEmpty()) {
                    body.parentRelationType = QStringLiteral("Planet");
                }
            } else if (fallbackParentStarId > 0) {
                body.parentId = fallbackParentStarId;
                if (body.parentRelationType.isEmpty()) {
                    body.parentRelationType = QStringLiteral("Star");
                }
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

    return bodies;
}

QVector<CelestialBody> parseEdastroBodies(const QJsonDocument& document) {
    if (document.isObject()) {
        const auto rootObject = document.object();

        // /api/starsystem может вернуть данные как объект системы либо как контейнер с массивом систем.
        if (rootObject.contains(QStringLiteral("name")) || rootObject.contains(QStringLiteral("bodies"))
            || rootObject.contains(QStringLiteral("systemBodies"))) {
            const auto directBodies = parseEdastroBodiesFromObject(rootObject);
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

            const auto candidateBodies = parseEdastroBodiesFromObject(systemsArray.first().toObject());
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

    return parseEdastroBodiesFromObject(systemsArray.first().toObject());
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

        const auto bodies = parseEdastroBodies(document);
        if (bodies.isEmpty()) {
            emit requestFailed(QStringLiteral("EDAstro вернул пустой список тел или неизвестный формат полей."));
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

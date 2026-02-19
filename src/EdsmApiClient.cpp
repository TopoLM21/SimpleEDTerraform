#include "EdsmApiClient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
    }

    return QStringLiteral("Unknown");
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
        body.distanceToArrivalLs = bodyObj.value(QStringLiteral("distanceToArrival")).toDouble(0.0);
        body.semiMajorAxisAu = bodyObj.value(QStringLiteral("semiMajorAxis")).toDouble(0.0);

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
            return object.value(key).toInt(defaultValue);
        }
    }

    return defaultValue;
}

double readDouble(const QJsonObject& object, const QStringList& keys, const double defaultValue = 0.0) {
    for (const auto& key : keys) {
        if (object.contains(key)) {
            return object.value(key).toDouble(defaultValue);
        }
    }

    return defaultValue;
}

QString readString(const QJsonObject& object, const QStringList& keys) {
    for (const auto& key : keys) {
        if (object.contains(key)) {
            return object.value(key).toString();
        }
    }

    return QString();
}

QVector<CelestialBody> parseSpanshBodies(const QJsonObject& rootObject) {
    QJsonArray bodiesArray;
    if (rootObject.value(QStringLiteral("bodies")).isArray()) {
        bodiesArray = rootObject.value(QStringLiteral("bodies")).toArray();
    } else if (rootObject.value(QStringLiteral("system")).isObject()) {
        const auto systemObject = rootObject.value(QStringLiteral("system")).toObject();
        bodiesArray = systemObject.value(QStringLiteral("bodies")).toArray();
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

        parseParentFromArray(bodyObj.value(QStringLiteral("parents")),
                             &body.parentId,
                             &body.parentRelationType,
                             &body.orbitsBarycenter);

        if (body.parentId < 0 && bodyObj.value(QStringLiteral("parent")).isObject()) {
            const auto parentObject = bodyObj.value(QStringLiteral("parent")).toObject();
            body.parentId = readInt(parentObject, {QStringLiteral("bodyId"), QStringLiteral("id")});
            body.parentRelationType = readString(parentObject,
                                                 {QStringLiteral("relationType"),
                                                  QStringLiteral("relation_type"),
                                                  QStringLiteral("type")});
            body.orbitsBarycenter = body.parentRelationType.contains(QStringLiteral("Null"), Qt::CaseInsensitive);
        }

        bodies.push_back(body);
    }

    return bodies;
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

void EdsmApiClient::requestSpanshSystemBodies(const QString& systemName) {
    const auto trimmedSystemName = systemName.trimmed();
    if (trimmedSystemName.isEmpty()) {
        emit requestFailed(QStringLiteral("Название системы не может быть пустым."));
        return;
    }

    QUrl url(QStringLiteral("https://spansh.co.uk/api/dump/%1").arg(QUrl::toPercentEncoding(trimmedSystemName).constData()));
    emit requestStateChanged(QStringLiteral("Запрос к Spansh отправлен..."));
    emit requestDebugInfo(QStringLiteral("[SPANSH] Отправка запроса. systemName='%1', url=%2")
                              .arg(trimmedSystemName, url.toString()));

    QNetworkRequest request(url);
    auto* reply = m_networkManager->get(request);

    auto* timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(kRequestTimeoutMs);

    connect(timeoutTimer, &QTimer::timeout, this, [this, reply]() {
        if (!reply->isRunning()) {
            return;
        }

        reply->setProperty("timedOut", true);
        reply->abort();
        emit requestStateChanged(QStringLiteral("Истекло время ожидания ответа Spansh."));
        emit requestFailed(QStringLiteral("Превышено время ожидания ответа Spansh"));
    });
    timeoutTimer->start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, trimmedSystemName, timeoutTimer]() {
        timeoutTimer->stop();
        reply->deleteLater();

        if (reply->property("timedOut").toBool()) {
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            emit requestFailed(reply->errorString());
            return;
        }

        const auto payload = reply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            emit requestFailed(QStringLiteral("Ответ Spansh имеет неверный формат."));
            return;
        }

        SystemBodiesResult result;
        result.systemName = trimmedSystemName;
        result.bodies = parseSpanshBodies(document.object());
        result.selectedSource = SystemDataSource::Spansh;
        result.hasSpanshData = !result.bodies.isEmpty();

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

    QUrl edsmUrl(QStringLiteral("https://www.edsm.net/api-system-v1/bodies"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("systemName"), trimmedSystemName);
    edsmUrl.setQuery(query);

    emit requestStateChanged(QStringLiteral("Запрос к EDSM отправлен..."));
    emit requestDebugInfo(QStringLiteral("[EDSM] Отправка запроса. mode=%1, systemName='%2', url=%3")
                              .arg(modeToText(mode), trimmedSystemName, edsmUrl.toString()));

    auto* edsmReply = m_networkManager->get(QNetworkRequest(edsmUrl));
    auto* edsmTimeoutTimer = new QTimer(edsmReply);
    edsmTimeoutTimer->setSingleShot(true);
    edsmTimeoutTimer->setInterval(kRequestTimeoutMs);

    connect(edsmTimeoutTimer, &QTimer::timeout, this, [this, edsmReply, mode, trimmedSystemName]() {
        if (!edsmReply->isRunning()) {
            return;
        }

        edsmReply->setProperty("timedOut", true);
        edsmReply->abort();
        emit requestDebugInfo(QStringLiteral("[EDSM] Таймаут запроса. mode=%1, url=%2")
                                  .arg(modeToText(mode), edsmReply->url().toString()));

        if (mode == SystemRequestMode::AutoMerge) {
            emit requestStateChanged(QStringLiteral("EDSM не ответил вовремя, используем fallback Spansh..."));
            requestSpanshSystemBodies(trimmedSystemName);
        } else {
            emit requestStateChanged(QStringLiteral("Истекло время ожидания ответа EDSM."));
            emit requestFailed(QStringLiteral("Превышено время ожидания ответа EDSM"));
        }
    });
    edsmTimeoutTimer->start();

    connect(edsmReply, &QNetworkReply::finished, this, [this, edsmReply, edsmTimeoutTimer, mode, trimmedSystemName]() {
        edsmTimeoutTimer->stop();
        edsmReply->deleteLater();

        if (edsmReply->property("timedOut").toBool()) {
            return;
        }

        if (edsmReply->error() != QNetworkReply::NoError) {
            emit requestDebugInfo(QStringLiteral("[EDSM] Сетевая ошибка. mode=%1, error=%2")
                                      .arg(modeToText(mode), edsmReply->errorString()));

            if (mode == SystemRequestMode::AutoMerge) {
                emit requestStateChanged(QStringLiteral("EDSM недоступен, используем fallback Spansh..."));
                requestSpanshSystemBodies(trimmedSystemName);
            } else {
                emit requestFailed(edsmReply->errorString());
            }

            return;
        }

        const auto payload = edsmReply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            if (mode == SystemRequestMode::AutoMerge) {
                emit requestStateChanged(QStringLiteral("EDSM вернул некорректный ответ, используем fallback Spansh..."));
                requestSpanshSystemBodies(trimmedSystemName);
            } else {
                emit requestFailed(QStringLiteral("Ответ EDSM имеет неверный формат."));
            }

            return;
        }

        SystemBodiesResult result;
        result.systemName = trimmedSystemName;
        result.bodies = parseEdsmBodies(document.object());
        result.selectedSource = SystemDataSource::Edsm;
        result.hasEdsmData = !result.bodies.isEmpty();

        if (result.bodies.isEmpty() && mode == SystemRequestMode::AutoMerge) {
            emit requestStateChanged(QStringLiteral("EDSM не вернул тела, используем fallback Spansh..."));
            requestSpanshSystemBodies(trimmedSystemName);
            return;
        }

        emit requestStateChanged(QStringLiteral("Данные получены из %1. Тел: %2")
                                     .arg(sourceToText(result.selectedSource))
                                     .arg(result.bodies.size()));
        emit systemBodiesReady(result);
    });
}

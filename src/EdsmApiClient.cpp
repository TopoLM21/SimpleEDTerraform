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

void EdsmApiClient::requestSystemBodies(const QString& systemName) {
    const auto trimmedSystemName = systemName.trimmed();
    if (trimmedSystemName.isEmpty()) {
        emit requestFailed(QStringLiteral("Название системы не может быть пустым."));
        return;
    }

    struct RequestState {
        QString systemName;
        QVector<CelestialBody> edsmBodies;
        QVector<CelestialBody> spanshBodies;
        bool edsmDone = false;
        bool spanshDone = false;
        bool edsmFailed = false;
        bool spanshFailed = false;
        bool emittedFallback = false;
    };

    auto state = QSharedPointer<RequestState>::create();
    state->systemName = trimmedSystemName;

    QUrl edsmUrl(QStringLiteral("https://www.edsm.net/api-system-v1/bodies"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("systemName"), trimmedSystemName);
    edsmUrl.setQuery(query);

    QUrl spanshUrl(QStringLiteral("https://spansh.co.uk/api/dump/%1").arg(QUrl::toPercentEncoding(trimmedSystemName).constData()));

    emit requestStateChanged(QStringLiteral("Подготовка запроса к EDSM и Spansh..."));
    emit requestDebugInfo(QStringLiteral("[EDSM] Отправка запроса. systemName='%1', url=%2")
                              .arg(trimmedSystemName, edsmUrl.toString()));
    emit requestDebugInfo(QStringLiteral("[SPANSH] Отправка запроса. systemName='%1', url=%2")
                              .arg(trimmedSystemName, spanshUrl.toString()));

    auto finalize = [this, state]() {
        if (!state->edsmDone || !state->spanshDone) {
            return;
        }

        if (state->edsmBodies.isEmpty() && state->spanshBodies.isEmpty()) {
            emit requestFailed(QStringLiteral("Не удалось получить данные ни из EDSM, ни из Spansh."));
            return;
        }

        SystemBodiesResult result;
        result.systemName = state->systemName;
        result.hasEdsmData = !state->edsmBodies.isEmpty();
        result.hasSpanshData = !state->spanshBodies.isEmpty();

        if (result.hasEdsmData && result.hasSpanshData) {
            result.bodies = mergeBodies(state->edsmBodies, state->spanshBodies, &result.hadConflict);
            result.selectedSource = SystemDataSource::Merged;
        } else if (result.hasEdsmData) {
            result.bodies = state->edsmBodies;
            result.selectedSource = SystemDataSource::Edsm;
        } else {
            result.bodies = state->spanshBodies;
            result.selectedSource = SystemDataSource::Spansh;
        }

        emit requestStateChanged(QStringLiteral("Данные получены из %1. Тел: %2")
                                     .arg(sourceToText(result.selectedSource))
                                     .arg(result.bodies.size()));
        emit systemBodiesReady(result);
    };

    auto* edsmReply = m_networkManager->get(QNetworkRequest(edsmUrl));
    auto* edsmTimeoutTimer = new QTimer(edsmReply);
    edsmTimeoutTimer->setSingleShot(true);
    edsmTimeoutTimer->setInterval(kRequestTimeoutMs);

    connect(edsmTimeoutTimer, &QTimer::timeout, this, [this, edsmReply]() {
        if (!edsmReply->isRunning()) {
            return;
        }

        edsmReply->setProperty("timedOut", true);
        edsmReply->abort();
        emit requestDebugInfo(QStringLiteral("[EDSM] Таймаут запроса. url=%1").arg(edsmReply->url().toString()));
        emit requestStateChanged(QStringLiteral("EDSM не ответил вовремя, используем fallback Spansh..."));
    });
    edsmTimeoutTimer->start();

    connect(edsmReply, &QNetworkReply::finished, this, [this, edsmReply, edsmTimeoutTimer, state, finalize]() {
        edsmTimeoutTimer->stop();
        edsmReply->deleteLater();
        state->edsmDone = true;

        if (!edsmReply->property("timedOut").toBool() && edsmReply->error() == QNetworkReply::NoError) {
            const auto payload = edsmReply->readAll();
            const auto document = QJsonDocument::fromJson(payload);
            if (document.isObject()) {
                state->edsmBodies = parseEdsmBodies(document.object());
            }
        } else {
            state->edsmFailed = true;
        }

        finalize();
    });

    auto* spanshReply = m_networkManager->get(QNetworkRequest(spanshUrl));
    auto* spanshTimeoutTimer = new QTimer(spanshReply);
    spanshTimeoutTimer->setSingleShot(true);
    spanshTimeoutTimer->setInterval(kRequestTimeoutMs);

    connect(spanshTimeoutTimer, &QTimer::timeout, this, [this, spanshReply]() {
        if (!spanshReply->isRunning()) {
            return;
        }

        spanshReply->setProperty("timedOut", true);
        spanshReply->abort();
        emit requestDebugInfo(QStringLiteral("[SPANSH] Таймаут запроса. url=%1").arg(spanshReply->url().toString()));
    });
    spanshTimeoutTimer->start();

    connect(spanshReply, &QNetworkReply::finished, this, [this, spanshReply, spanshTimeoutTimer, state, finalize]() {
        spanshTimeoutTimer->stop();
        spanshReply->deleteLater();
        state->spanshDone = true;

        if (!spanshReply->property("timedOut").toBool() && spanshReply->error() == QNetworkReply::NoError) {
            const auto payload = spanshReply->readAll();
            const auto document = QJsonDocument::fromJson(payload);
            if (document.isObject()) {
                state->spanshBodies = parseSpanshBodies(document.object());
            }
        } else {
            state->spanshFailed = true;
        }

        finalize();
    });
}

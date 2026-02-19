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
                                 ? QStringLiteral("Параллельные запросы к EDSM и Spansh отправлены...")
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

    QUrl spanshUrl(QStringLiteral("https://spansh.co.uk/api/dump/%1").arg(QUrl::toPercentEncoding(trimmedSystemName).constData()));
    emit requestDebugInfo(QStringLiteral("[SPANSH] Отправка запроса. mode=%1, systemName='%2', url=%3")
                              .arg(modeToText(mode), trimmedSystemName, spanshUrl.toString()));

    auto* spanshReply = m_networkManager->get(QNetworkRequest(spanshUrl));
    auto* spanshTimeoutTimer = new QTimer(spanshReply);
    spanshTimeoutTimer->setSingleShot(true);
    spanshTimeoutTimer->setInterval(kRequestTimeoutMs);
    connect(spanshTimeoutTimer, &QTimer::timeout, this, [this, spanshReply, mode, state]() {
        if (!spanshReply->isRunning()) {
            return;
        }

        state->spanshTimedOut = true;
        spanshReply->setProperty("timedOut", true);
        spanshReply->abort();
        emit requestDebugInfo(QStringLiteral("[SPANSH] Таймаут запроса. mode=%1, url=%2")
                                  .arg(modeToText(mode), spanshReply->url().toString()));
    });
    spanshTimeoutTimer->start();

    connect(spanshReply, &QNetworkReply::finished, this, [this, spanshReply, spanshTimeoutTimer, mode, state, finalizeRequest]() {
        spanshTimeoutTimer->stop();
        spanshReply->deleteLater();
        state->spanshDone = true;

        if (spanshReply->property("timedOut").toBool()) {
            state->spanshError = QStringLiteral("Превышено время ожидания ответа Spansh");
            finalizeRequest();
            return;
        }

        if (spanshReply->error() != QNetworkReply::NoError) {
            state->spanshError = spanshReply->errorString();
            emit requestDebugInfo(QStringLiteral("[SPANSH] Сетевая ошибка. mode=%1, error=%2")
                                      .arg(modeToText(mode), state->spanshError));
            finalizeRequest();
            return;
        }

        const auto payload = spanshReply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            state->spanshError = QStringLiteral("Ответ Spansh имеет неверный формат.");
            emit requestDebugInfo(QStringLiteral("[SPANSH] Ошибка парсинга. mode=%1")
                                      .arg(modeToText(mode)));
            finalizeRequest();
            return;
        }

        state->spanshParsed = true;
        state->spanshBodies = parseSpanshBodies(document.object());
        emit requestDebugInfo(QStringLiteral("[SPANSH] Ответ обработан. mode=%1, bodies=%2")
                                  .arg(modeToText(mode))
                                  .arg(state->spanshBodies.size()));
        finalizeRequest();
    });
}

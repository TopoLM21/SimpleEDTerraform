#include "EdsmApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

EdsmApiClient::EdsmApiClient(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

void EdsmApiClient::requestSystemBodies(const QString& systemName) {
    if (systemName.trimmed().isEmpty()) {
        emit requestFailed(QStringLiteral("Название системы не может быть пустым."));
        return;
    }

    QUrl url(QStringLiteral("https://www.edsm.net/api-system-v1/bodies"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("systemName"), systemName.trimmed());
    url.setQuery(query);

    QNetworkRequest request(url);
    auto* reply = m_networkManager->get(request);
    auto* timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(15000);

    connect(timeoutTimer, &QTimer::timeout, this, [this, reply]() {
        if (!reply->isRunning()) {
            return;
        }

        reply->setProperty("timedOut", true);
        reply->abort();
        emit requestFailed(QStringLiteral("Превышено время ожидания ответа EDSM"));
    });
    timeoutTimer->start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, systemName, timeoutTimer]() {
        timeoutTimer->stop();
        reply->deleteLater();

        const auto timedOut = reply->property("timedOut").toBool();
        if (timedOut) {
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                emit requestFailed(QStringLiteral("Превышено время ожидания ответа EDSM"));
                return;
            }

            emit requestFailed(reply->errorString());
            return;
        }

        const auto payload = reply->readAll();
        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            emit requestFailed(QStringLiteral("Ответ EDSM имеет неверный формат."));
            return;
        }

        const auto rootObject = document.object();
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

            const auto parentsArray = bodyObj.value(QStringLiteral("parents")).toArray();
            if (!parentsArray.isEmpty()) {
                const auto parentObject = parentsArray.first().toObject();
                if (!parentObject.isEmpty()) {
                    body.parentId = parentObject.constBegin().value().toInt(-1);
                }
            }

            bodies.push_back(body);
        }

        emit systemBodiesReady(systemName.trimmed(), bodies);
    });
}

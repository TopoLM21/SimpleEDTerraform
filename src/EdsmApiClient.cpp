#include "EdsmApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

EdsmApiClient::EdsmApiClient(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

void EdsmApiClient::requestSystemBodies(const QString& systemName) {
    const auto trimmedSystemName = systemName.trimmed();
    if (trimmedSystemName.isEmpty()) {
        emit requestFailed(QStringLiteral("Название системы не может быть пустым."));
        return;
    }

    QUrl url(QStringLiteral("https://www.edsm.net/api-system-v1/bodies"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("systemName"), trimmedSystemName);
    url.setQuery(query);

    emit requestStateChanged(QStringLiteral("Подготовка запроса к EDSM..."));
    emit requestDebugInfo(QStringLiteral("[EDSM] Отправка запроса. systemName='%1', url=%2")
                              .arg(trimmedSystemName, url.toString()));

    QNetworkRequest request(url);
    auto* reply = m_networkManager->get(request);

    emit requestStateChanged(QStringLiteral("Запрос отправлен, ожидаем ответ..."));

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() {
        emit requestStateChanged(QStringLiteral("Получены заголовки ответа EDSM..."));
        emit requestDebugInfo(QStringLiteral("[EDSM] Заголовки получены. HTTP=%1, url=%2")
                                  .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                  .arg(reply->url().toString()));
    });

    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
        if (total > 0) {
            emit requestStateChanged(QStringLiteral("Загрузка данных: %1/%2 байт").arg(received).arg(total));
            emit requestDebugInfo(QStringLiteral("[EDSM] Прогресс загрузки: %1/%2 байт, url=%3")
                                      .arg(received)
                                      .arg(total)
                                      .arg(reply->url().toString()));
            return;
        }

        emit requestStateChanged(QStringLiteral("Загрузка данных: %1 байт").arg(received));
        emit requestDebugInfo(QStringLiteral("[EDSM] Прогресс загрузки: %1 байт (total неизвестен), url=%2")
                                  .arg(received)
                                  .arg(reply->url().toString()));
    });

    connect(reply, &QNetworkReply::sslErrors, this, [this, reply](const QList<QSslError>& errors) {
        QStringList sslErrorTexts;
        sslErrorTexts.reserve(errors.size());
        for (const auto& error : errors) {
            sslErrorTexts.push_back(error.errorString());
        }

        emit requestStateChanged(QStringLiteral("Проблема с SSL при соединении с EDSM."));
        emit requestDebugInfo(QStringLiteral("[EDSM] SSL ошибки (%1), url=%2: %3")
                                  .arg(errors.size())
                                  .arg(reply->url().toString(), sslErrorTexts.join(QStringLiteral(" | "))));
    });

    connect(reply,
            qOverload<QNetworkReply::NetworkError>(&QNetworkReply::errorOccurred),
            this,
            [this, reply](QNetworkReply::NetworkError errorCode) {
                emit requestStateChanged(QStringLiteral("Ошибка сети при запросе к EDSM."));
                emit requestDebugInfo(QStringLiteral("[EDSM] Ошибка сети: code=%1, text='%2', url=%3")
                                          .arg(static_cast<int>(errorCode))
                                          .arg(reply->errorString(), reply->url().toString()));
            });

    auto* timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(15000);

    connect(timeoutTimer, &QTimer::timeout, this, [this, reply]() {
        if (!reply->isRunning()) {
            return;
        }

        reply->setProperty("timedOut", true);
        reply->abort();
        emit requestStateChanged(QStringLiteral("Истекло время ожидания ответа EDSM."));
        emit requestDebugInfo(QStringLiteral("[EDSM] Таймаут запроса. url=%1").arg(reply->url().toString()));
        emit requestFailed(QStringLiteral("Превышено время ожидания ответа EDSM"));
    });
    timeoutTimer->start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, trimmedSystemName, timeoutTimer]() {
        timeoutTimer->stop();
        reply->deleteLater();

        const auto timedOut = reply->property("timedOut").toBool();
        if (timedOut) {
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                emit requestStateChanged(QStringLiteral("Запрос прерван: превышено время ожидания."));
                emit requestDebugInfo(QStringLiteral("[EDSM] Запрос прерван после таймаута. code=%1, url=%2")
                                          .arg(static_cast<int>(reply->error()))
                                          .arg(reply->url().toString()));
                emit requestFailed(QStringLiteral("Превышено время ожидания ответа EDSM"));
                return;
            }

            emit requestDebugInfo(QStringLiteral("[EDSM] Завершение с ошибкой. code=%1, text='%2', url=%3")
                                      .arg(static_cast<int>(reply->error()))
                                      .arg(reply->errorString(), reply->url().toString()));
            emit requestFailed(reply->errorString());
            return;
        }

        emit requestStateChanged(QStringLiteral("Ответ получен, разбираем данные..."));
        const auto payload = reply->readAll();
        emit requestDebugInfo(QStringLiteral("[EDSM] Получен payload: %1 байт, url=%2")
                                  .arg(payload.size())
                                  .arg(reply->url().toString()));

        const auto document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            emit requestStateChanged(QStringLiteral("Ошибка разбора ответа EDSM."));
            emit requestDebugInfo(QStringLiteral("[EDSM] Ошибка парсинга: корневой JSON не объект."));
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
                    const auto relation = parentObject.constBegin();
                    body.parentRelationType = relation.key();
                    body.parentId = relation.value().toInt(-1);
                    body.orbitsBarycenter = relation.key().contains(QStringLiteral("Null"), Qt::CaseInsensitive);
                }
            }

            bodies.push_back(body);
        }

        emit requestStateChanged(QStringLiteral("Данные EDSM успешно обработаны."));
        emit requestDebugInfo(QStringLiteral("[EDSM] Парсинг завершен. systemName='%1', bodies=%2")
                                  .arg(trimmedSystemName)
                                  .arg(bodies.size()));
        emit systemBodiesReady(trimmedSystemName, bodies);
    });
}

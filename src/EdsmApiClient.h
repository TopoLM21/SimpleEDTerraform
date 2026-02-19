#pragma once

#include <QObject>
#include <QVector>

#include "CelestialBody.h"

class QNetworkAccessManager;

class EdsmApiClient : public QObject {
    Q_OBJECT
public:
    explicit EdsmApiClient(QObject* parent = nullptr);

    void requestSystemBodies(const QString& systemName);

signals:
    void systemBodiesReady(const QString& systemName, const QVector<CelestialBody>& bodies);
    void requestFailed(const QString& reason);
    void requestStateChanged(const QString& state);
    void requestDebugInfo(const QString& message);

private:
    QNetworkAccessManager* m_networkManager = nullptr;
};

#pragma once

#include <QObject>
#include <QVector>

#include "CelestialBody.h"

class QNetworkAccessManager;

enum class SystemDataSource {
    Edsm,
    Spansh,
    Edastro,
    Merged
};

enum class SystemRequestMode {
    AutoMerge,
    EdsmOnly,
    SpanshOnly,
    EdastroOnly
};

struct SystemBodiesResult {
    QString systemName;
    QVector<CelestialBody> bodies;
    SystemDataSource selectedSource = SystemDataSource::Edsm;
    bool hasEdsmData = false;
    bool hasSpanshData = false;
    bool hasEdastroData = false;
    bool hadConflict = false;
};

class EdsmApiClient : public QObject {
    Q_OBJECT
public:
    explicit EdsmApiClient(QObject* parent = nullptr);

    void requestSystemBodies(const QString& systemName,
                             SystemRequestMode mode = SystemRequestMode::AutoMerge);
    void requestSpanshSystemBodies(const QString& systemName);
    void requestEdastroSystemBodies(const QString& systemName);

signals:
    void systemBodiesReady(const SystemBodiesResult& result);
    void requestFailed(const QString& reason);
    void requestStateChanged(const QString& state);
    void requestDebugInfo(const QString& message);

private:
    QNetworkAccessManager* m_networkManager = nullptr;
};

Q_DECLARE_METATYPE(SystemBodiesResult);

#pragma once

#include <QObject>
#include <functional>
#include <QVector>

#include "CelestialBody.h"

class QNetworkAccessManager;
class QJsonDocument;

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
    SystemDataSource selectedSource = SystemDataSource::Edastro;
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
                             SystemRequestMode mode = SystemRequestMode::EdastroOnly);
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

// Тестовый хелпер: позволяет проверять парсинг EDastro без запуска сетевых запросов.
QVector<CelestialBody> parseEdastroBodiesForTests(const QJsonDocument& document,
                                                  const QString& defaultSystemName,
                                                  const std::function<void(const QString&)>& onDebugInfo);

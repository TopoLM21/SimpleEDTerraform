#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>
#include <QtTest>

#include "CelestialBody.h"
#include "EdsmApiClient.h"

namespace {

QJsonDocument loadJson(const QString& relativePath) {
    QFile file(relativePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }

    return doc;
}

QHash<int, CelestialBody> toMap(const QVector<CelestialBody>& bodies) {
    QHash<int, CelestialBody> map;
    for (const auto& body : bodies) {
        map.insert(body.id, body);
    }
    return map;
}

} // namespace

class EdastroHierarchyTests : public QObject {
    Q_OBJECT

private slots:
    void eadstroBarycenterResolvesToStar();
    void colHierarchyResolvesThroughNull4();
};

void EdastroHierarchyTests::eadstroBarycenterResolvesToStar() {
    const auto document = loadJson(QStringLiteral("eadstro_example.json"));
    QVERIFY2(!document.isNull(), "Failed to parse eadstro_example.json");

    QStringList diagnostics;
    const auto bodies = parseEdastroBodiesForTests(document,
                                                   QStringLiteral("Anagorovici"),
                                                   [&diagnostics](const QString& message) {
                                                       diagnostics.push_back(message);
                                                   });
    const auto map = toMap(bodies);
    QVERIFY2(map.contains(1), "Expected barycenter body id=1");

    const auto barycenter = map.value(1);
    QCOMPARE(barycenter.parentId, 0);
    QCOMPARE(barycenter.parentRelationType, QStringLiteral("Star"));
}

void EdastroHierarchyTests::colHierarchyResolvesThroughNull4() {
    const auto document = loadJson(QStringLiteral("col.json"));
    QVERIFY2(!document.isNull(), "Failed to parse col.json");

    QStringList diagnostics;
    const auto bodies = parseEdastroBodiesForTests(document,
                                                   QStringLiteral("Col 285 Sector XW-G b25-1"),
                                                   [&diagnostics](const QString& message) {
                                                       diagnostics.push_back(message);
                                                   });
    const auto map = toMap(bodies);

    QVERIFY2(map.contains(1), "Expected barycenter body id=1");
    QVERIFY2(map.contains(4), "Expected barycenter body id=4");
    QCOMPARE(map.value(1).parentId, 0);
    QCOMPARE(map.value(1).parentRelationType, QStringLiteral("Null"));
    QCOMPARE(map.value(4).parentId, 0);
    QCOMPARE(map.value(4).parentRelationType, QStringLiteral("Null"));

    QVERIFY2(map.contains(5), "Expected star C id=5");
    QVERIFY2(map.contains(6), "Expected star D id=6");
    QCOMPARE(map.value(5).parentId, 4);
    QCOMPARE(map.value(5).parentRelationType, QStringLiteral("Null"));
    QCOMPARE(map.value(6).parentId, 4);
    QCOMPARE(map.value(6).parentRelationType, QStringLiteral("Null"));

    for (const int id : {22, 23, 24, 25}) {
        QVERIFY2(map.contains(id), qPrintable(QStringLiteral("Expected CD-* body id=%1").arg(id)));
        QCOMPARE(map.value(id).parentId, 4);
        QCOMPARE(map.value(id).parentRelationType, QStringLiteral("Null"));
    }

    QVERIFY2(map.contains(26), "Expected CD 4 a id=26");
    QCOMPARE(map.value(26).parentId, 25);
    QCOMPARE(map.value(26).parentRelationType, QStringLiteral("Planet"));
}

QTEST_MAIN(EdastroHierarchyTests)
#include "EdastroHierarchyTests.moc"

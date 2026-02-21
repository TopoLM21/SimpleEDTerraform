#include <algorithm>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <QtTest>

#include "CelestialBody.h"
#include "EdsmApiClient.h"
#include "SystemLayoutEngine.h"

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
    void synthesizesMissingBarycenterFromNullParentRef();
    void buildsBarycenterParentFromMoonOnlyChain();
    void parsesEdastroRootWithoutNameField();
    void parsesEdastroNestedObjectContainer();
    void binaryBarycenterChildrenUseSymmetricRadius();
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


void EdastroHierarchyTests::buildsBarycenterParentFromMoonOnlyChain() {
    QJsonObject root;

    root.insert(QStringLiteral("stars"),
                QJsonArray{QJsonObject{{QStringLiteral("id"), 0},
                                       {QStringLiteral("name"), QStringLiteral("Primary")},
                                       {QStringLiteral("type"), QStringLiteral("Star")}}});
    root.insert(QStringLiteral("planets"),
                QJsonArray{QJsonObject{{QStringLiteral("id"), 100},
                                       {QStringLiteral("name"), QStringLiteral("Planet A")},
                                       {QStringLiteral("type"), QStringLiteral("Planet")},
                                       {QStringLiteral("parents"), QStringLiteral("Star:0")}}});
    root.insert(QStringLiteral("moons"),
                QJsonArray{QJsonObject{{QStringLiteral("id"), 101},
                                       {QStringLiteral("name"), QStringLiteral("Moon A 1")},
                                       {QStringLiteral("type"), QStringLiteral("Moon")},
                                       {QStringLiteral("parents"), QStringLiteral("Planet:100;Null:7;Null:0")}}});
    root.insert(QStringLiteral("barycenters"),
                QJsonArray{QJsonObject{{QStringLiteral("id"), 7},
                                       {QStringLiteral("name"), QStringLiteral("Barycenter 7")},
                                       {QStringLiteral("type"), QStringLiteral("Barycenter")}}});

    QStringList diagnostics;
    const auto bodies = parseEdastroBodiesForTests(QJsonDocument(root),
                                                   QStringLiteral("Moon-only barycenter parent test"),
                                                   [&diagnostics](const QString& message) {
                                                       diagnostics.push_back(message);
                                                   });
    const auto map = toMap(bodies);

    QVERIFY2(map.contains(7), "Expected barycenter body id=7");
    QCOMPARE(map.value(7).parentId, 0);
    QCOMPARE(map.value(7).parentRelationType, QStringLiteral("Null"));

    const bool hasHierarchyError = std::any_of(diagnostics.cbegin(), diagnostics.cend(), [](const QString& message) {
        return message.contains(QStringLiteral("Некорректная иерархия"));
    });
    QVERIFY2(!hasHierarchyError, "Hierarchy should reach Star:* or Null:0 for all bodies");
}

void EdastroHierarchyTests::parsesEdastroRootWithoutNameField() {
    QJsonObject root;
    root.insert(QStringLiteral("stars"),
                QJsonArray{QJsonObject{{QStringLiteral("id"), 0},
                                       {QStringLiteral("name"), QStringLiteral("Primary")},
                                       {QStringLiteral("type"), QStringLiteral("Star")}}});

    QStringList diagnostics;
    const auto bodies = parseEdastroBodiesForTests(QJsonDocument(root),
                                                   QStringLiteral("Root without name"),
                                                   [&diagnostics](const QString& message) {
                                                       diagnostics.push_back(message);
                                                   });

    QVERIFY2(!bodies.isEmpty(), "Expected parser to accept root with direct stars/planets collections even without name");
    const auto map = toMap(bodies);
    QVERIFY2(map.contains(0), "Expected root star body id=0");
}

void EdastroHierarchyTests::parsesEdastroNestedObjectContainer() {
    QJsonObject systemObject;
    systemObject.insert(QStringLiteral("stars"),
                        QJsonArray{QJsonObject{{QStringLiteral("id"), 0},
                                               {QStringLiteral("name"), QStringLiteral("Primary")},
                                               {QStringLiteral("type"), QStringLiteral("Star")}}});

    QJsonObject root;
    root.insert(QStringLiteral("result"), systemObject);

    QStringList diagnostics;
    const auto bodies = parseEdastroBodiesForTests(QJsonDocument(root),
                                                   QStringLiteral("Nested object container"),
                                                   [&diagnostics](const QString& message) {
                                                       diagnostics.push_back(message);
                                                   });

    QVERIFY2(!bodies.isEmpty(), "Expected parser to accept nested object container format");
    const auto map = toMap(bodies);
    QVERIFY2(map.contains(0), "Expected root star body id=0");
}

void EdastroHierarchyTests::binaryBarycenterChildrenUseSymmetricRadius() {
    QHash<int, CelestialBody> bodyMap;

    CelestialBody barycenter;
    barycenter.id = 4;
    barycenter.name = QStringLiteral("Barycenter 4");
    barycenter.type = QStringLiteral("Barycenter");
    barycenter.bodyClass = CelestialBody::BodyClass::Barycenter;
    barycenter.parentId = -1;
    barycenter.children = {5, 6};

    CelestialBody starC;
    starC.id = 5;
    starC.name = QStringLiteral("Star C");
    starC.type = QStringLiteral("Star");
    starC.bodyClass = CelestialBody::BodyClass::Star;
    starC.parentId = 4;
    starC.semiMajorAxisAu = 0.04730196114;

    CelestialBody starD;
    starD.id = 6;
    starD.name = QStringLiteral("Star D");
    starD.type = QStringLiteral("Star");
    starD.bodyClass = CelestialBody::BodyClass::Star;
    starD.parentId = 4;
    starD.semiMajorAxisAu = 0.048077432192;

    bodyMap.insert(4, barycenter);
    bodyMap.insert(5, starC);
    bodyMap.insert(6, starD);

    const auto layout = SystemLayoutEngine::buildLayout(bodyMap, {4}, QRectF(0.0, 0.0, 800.0, 600.0));
    QVERIFY2(layout.contains(4), "Expected barycenter in layout");
    QVERIFY2(layout.contains(5), "Expected star C in layout");
    QVERIFY2(layout.contains(6), "Expected star D in layout");

    const QPointF barycenterPos = layout.value(4).position;
    const QPointF cPos = layout.value(5).position;
    const QPointF dPos = layout.value(6).position;

    const double cDx = cPos.x() - barycenterPos.x();
    const double dDx = dPos.x() - barycenterPos.x();
    const double cDy = cPos.y() - barycenterPos.y();
    const double dDy = dPos.y() - barycenterPos.y();

    QVERIFY2(qAbs(cDx + dDx) < 1e-6, "Binary stars should be placed on opposite ends of one diameter (X symmetry)");
    QVERIFY2(qAbs(cDy + dDy) < 1e-6, "Binary stars should be placed on opposite ends of one diameter (Y symmetry)");
}

void EdastroHierarchyTests::synthesizesMissingBarycenterFromNullParentRef() {
    QJsonObject root;

    QJsonArray stars;
    stars.push_back(QJsonObject{{QStringLiteral("id"), 0},
                                {QStringLiteral("name"), QStringLiteral("Primary")},
                                {QStringLiteral("type"), QStringLiteral("Star")}});
    stars.push_back(QJsonObject{{QStringLiteral("id"), 10},
                                {QStringLiteral("name"), QStringLiteral("Companion A")},
                                {QStringLiteral("type"), QStringLiteral("Star")},
                                {QStringLiteral("parents"), QStringLiteral("Null:42;Star:0")}});
    stars.push_back(QJsonObject{{QStringLiteral("id"), 11},
                                {QStringLiteral("name"), QStringLiteral("Companion B")},
                                {QStringLiteral("type"), QStringLiteral("Star")},
                                {QStringLiteral("parents"), QStringLiteral("Null:42;Star:0")}});
    root.insert(QStringLiteral("stars"), stars);
    root.insert(QStringLiteral("barycenters"), QJsonArray{});

    QStringList diagnostics;
    const auto bodies = parseEdastroBodiesForTests(QJsonDocument(root),
                                                   QStringLiteral("Synthetic test system"),
                                                   [&diagnostics](const QString& message) {
                                                       diagnostics.push_back(message);
                                                   });
    const auto map = toMap(bodies);

    QVERIFY2(map.contains(42), "Expected synthetic barycenter body id=42");
    QCOMPARE(map.value(42).bodyClass, CelestialBody::BodyClass::Barycenter);
    QCOMPARE(map.value(42).type, QStringLiteral("Barycenter"));
    QCOMPARE(map.value(42).parentId, 0);
    QCOMPARE(map.value(42).parentRelationType, QStringLiteral("Star"));

    QVERIFY2(map.contains(10), "Expected companion star id=10");
    QVERIFY2(map.contains(11), "Expected companion star id=11");
    QCOMPARE(map.value(10).parentId, 42);
    QCOMPARE(map.value(11).parentId, 42);
}

QTEST_MAIN(EdastroHierarchyTests)
#include "EdastroHierarchyTests.moc"

#pragma once

#include <QString>
#include <QVector>

inline constexpr int kExternalVirtualBarycenterMarkerId = 0;
inline constexpr int kVirtualBarycenterRootId = 0;
inline const QString kVirtualBarycenterRootType = QStringLiteral("Null");

struct CelestialBody {
    struct CompositionPart {
        QString name;
        double percent = 0.0;
    };

    enum class BodyClass {
        Unknown,
        Star,
        Planet,
        Moon,
        Barycenter
    };

    int id = -1;
    int parentId = -1;
    QString parentRelationType;
    QString name;
    QString type;
    double distanceToArrivalLs = 0.0;
    double semiMajorAxisAu = 0.0;
    double physicalRadiusKm = 0.0;
    double surfaceGravityMs2 = 0.0;
    double surfaceTemperatureK = 0.0;
    double rotationPeriodDays = 0.0;
    bool isTidallyLocked = false;
    QString atmosphereSummary;
    double atmospherePressureAtm = 0.0;
    double massEarth = 0.0;
    double massSolar = 0.0;
    double axialTiltDeg = 0.0;
    QString volcanism;
    QString terraformingState;
    QVector<CompositionPart> atmoComposition;
    QVector<CompositionPart> materials;
    bool orbitsBarycenter = false;
    BodyClass bodyClass = BodyClass::Unknown;
    QVector<int> children;
};

inline bool isVirtualBarycenterRoot(const CelestialBody& body) {
    return body.id == kVirtualBarycenterRootId
           && body.type.compare(kVirtualBarycenterRootType, Qt::CaseInsensitive) == 0;
}

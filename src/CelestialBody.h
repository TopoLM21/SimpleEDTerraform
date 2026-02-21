#pragma once

#include <QString>
#include <QVector>

inline constexpr int kExternalVirtualBarycenterMarkerId = 0;
inline constexpr int kVirtualBarycenterRootId = -1000000000;
inline const QString kVirtualBarycenterRootType = QStringLiteral("Null");

struct CelestialBody {
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
    bool orbitsBarycenter = false;
    BodyClass bodyClass = BodyClass::Unknown;
    QVector<int> children;
};

inline bool isVirtualBarycenterRoot(const CelestialBody& body) {
    return body.id == kVirtualBarycenterRootId
           && body.type.compare(kVirtualBarycenterRootType, Qt::CaseInsensitive) == 0;
}

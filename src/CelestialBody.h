#pragma once

#include <QString>
#include <QVector>

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

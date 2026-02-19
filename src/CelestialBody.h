#pragma once

#include <QString>
#include <QVector>

struct CelestialBody {
    int id = -1;
    int parentId = -1;
    QString parentRelationType;
    QString name;
    QString type;
    double distanceToArrivalLs = 0.0;
    double semiMajorAxisAu = 0.0;
    bool orbitsBarycenter = false;
    QVector<int> children;
};

#pragma once

#include <QHash>
#include <QPointF>
#include <QRectF>

#include "CelestialBody.h"

struct BodyLayout {
    QPointF position;
    double radius = 6.0;
    double orbitRadius = 0.0;
};

class SystemLayoutEngine {
public:
    static QHash<int, BodyLayout> buildLayout(const QHash<int, CelestialBody>& bodyMap,
                                              const QVector<int>& roots,
                                              const QRectF& canvasRect);

private:
    static void layoutChildrenRecursive(const QHash<int, CelestialBody>& bodyMap,
                                        QHash<int, BodyLayout>& layout,
                                        int bodyId,
                                        double pxPerAu,
                                        double fallbackDistancePx);
};

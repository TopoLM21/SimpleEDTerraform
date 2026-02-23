#pragma once

#include <QHash>
#include <QWidget>

#include "CelestialBody.h"

class QLabel;
class QToolBox;

class BodyDetailsWidget : public QWidget {
    Q_OBJECT
public:
    explicit BodyDetailsWidget(QWidget* parent = nullptr);

    void setBody(const CelestialBody& body, const QHash<int, CelestialBody>& bodyMap);
    void setPlaceholderText(const QString& text);

private:
    void setFieldValue(const QString& key, const QString& value);
    QString fallbackText(const QString& value) const;
    QString formatText(const QString& value) const;
    QString formatDouble(const double value, const int precision) const;
    QString formatDistanceLs(double value) const;
    QString formatSemiMajorAxis(double value) const;
    QString formatRadiusKm(double value) const;
    QString formatGravity(double valueMs2) const;
    QString formatTemperature(double valueK) const;
    QString formatMass(const CelestialBody& body) const;
    QString formatDayLength(double valueDays, bool tidallyLocked) const;
    QString formatAxialTilt(double valueDeg) const;
    QString formatComposition(const QVector<CelestialBody::CompositionPart>& parts) const;
    QString formatPressure(double valueAtm) const;
    QString bodyClassText(CelestialBody::BodyClass bodyClass) const;

    QLabel* m_placeholderLabel = nullptr;
    QToolBox* m_toolBox = nullptr;
    QHash<QString, QLabel*> m_fields;
};

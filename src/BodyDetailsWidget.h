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
    QString formatText(const QString& value) const;
    QString formatId(const int id) const;
    QString formatDouble(const double value, const int precision) const;
    QString bodyClassText(CelestialBody::BodyClass bodyClass) const;

    QLabel* m_placeholderLabel = nullptr;
    QToolBox* m_toolBox = nullptr;
    QHash<QString, QLabel*> m_fields;
};

#pragma once

#include <QWidget>

#include "CelestialBody.h"

class QTextEdit;

class BodyDetailsWidget : public QWidget {
    Q_OBJECT
public:
    explicit BodyDetailsWidget(QWidget* parent = nullptr);

    void setBody(const CelestialBody& body);
    void setPlaceholderText(const QString& text);

private:
    QString bodyDetailsText(const CelestialBody& body) const;

    QTextEdit* m_textEdit = nullptr;
};

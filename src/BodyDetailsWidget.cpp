#include "BodyDetailsWidget.h"

#include <QTextEdit>
#include <QVBoxLayout>

namespace {

QString yesNo(const bool value) {
    return value ? QStringLiteral("да") : QStringLiteral("нет");
}

} // namespace

BodyDetailsWidget::BodyDetailsWidget(QWidget* parent)
    : QWidget(parent) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    rootLayout->addWidget(m_textEdit);
}

void BodyDetailsWidget::setBody(const CelestialBody& body) {
    m_textEdit->setPlainText(bodyDetailsText(body));
}

void BodyDetailsWidget::setPlaceholderText(const QString& text) {
    m_textEdit->setPlainText(text);
}

QString BodyDetailsWidget::bodyDetailsText(const CelestialBody& body) const {
    QStringList lines;
    lines << QStringLiteral("Название: %1").arg(body.name);
    lines << QStringLiteral("ID: %1").arg(body.id);
    lines << QStringLiteral("Тип: %1").arg(body.type.isEmpty() ? QStringLiteral("—") : body.type);
    lines << QStringLiteral("Parent ID: %1").arg(body.parentId >= 0 ? QString::number(body.parentId) : QStringLiteral("—"));

    if (!body.parentRelationType.isEmpty()) {
        lines << QStringLiteral("Связь с родителем: %1").arg(body.parentRelationType);
    }

    lines << QStringLiteral("До точки входа: %1 ls").arg(body.distanceToArrivalLs, 0, 'f', 2);
    lines << QStringLiteral("Большая полуось: %1 AU").arg(body.semiMajorAxisAu, 0, 'f', 5);
    lines << QStringLiteral("Детей: %1").arg(body.children.size());
    lines << QStringLiteral("Орбита вокруг барицентра: %1").arg(yesNo(body.orbitsBarycenter));

    return lines.join('\n');
}

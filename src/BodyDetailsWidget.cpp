#include "BodyDetailsWidget.h"

#include "OrbitClassifier.h"

#include <QFormLayout>
#include <QLabel>
#include <QToolBox>
#include <QVBoxLayout>

namespace {

QString yesNo(const bool value) {
    return value ? QStringLiteral("да") : QStringLiteral("нет");
}

QLabel* createValueLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

QWidget* createSectionPage(QToolBox* toolBox, const QString& title) {
    auto* page = new QWidget(toolBox);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(8, 8, 8, 8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    page->setLayout(form);
    toolBox->addItem(page, title);
    return page;
}

} // namespace

BodyDetailsWidget::BodyDetailsWidget(QWidget* parent)
    : QWidget(parent) {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_placeholderLabel = new QLabel(this);
    m_placeholderLabel->setWordWrap(true);
    m_placeholderLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    rootLayout->addWidget(m_placeholderLabel);

    m_toolBox = new QToolBox(this);
    rootLayout->addWidget(m_toolBox, 1);

    auto addField = [this](QFormLayout* form, const QString& fieldKey, const QString& title) {
        auto* valueLabel = createValueLabel(form->parentWidget());
        form->addRow(title, valueLabel);
        m_fields.insert(fieldKey, valueLabel);
    };

    {
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Основное"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("name"), QStringLiteral("Имя:"));
        addField(form, QStringLiteral("type"), QStringLiteral("Тип:"));
        addField(form, QStringLiteral("id"), QStringLiteral("ID:"));
        addField(form, QStringLiteral("class"), QStringLiteral("Класс:"));
    }

    {
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Иерархия"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("parentId"), QStringLiteral("Parent ID:"));
        addField(form, QStringLiteral("parentName"), QStringLiteral("Parent name:"));
        addField(form, QStringLiteral("relation"), QStringLiteral("Связь с родителем:"));
        addField(form, QStringLiteral("children"), QStringLiteral("Детей:"));
    }

    {
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Орбита"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("distance"), QStringLiteral("До точки входа:"));
        addField(form, QStringLiteral("semiMajorAxis"), QStringLiteral("Большая полуось:"));
        addField(form, QStringLiteral("orbitsBarycenter"), QStringLiteral("Орбита вокруг барицентра:"));
    }

    {
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Физика"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("physicalRadius"), QStringLiteral("Физический радиус:"));
    }

    setPlaceholderText(QStringLiteral("Кликните по телу, чтобы увидеть параметры."));
}

void BodyDetailsWidget::setBody(const CelestialBody& body, const QHash<int, CelestialBody>& bodyMap) {
    m_placeholderLabel->hide();
    m_toolBox->show();

    setFieldValue(QStringLiteral("name"), formatText(body.name));
    setFieldValue(QStringLiteral("type"), formatText(body.type));
    setFieldValue(QStringLiteral("id"), QString::number(body.id));
    setFieldValue(QStringLiteral("class"), bodyClassText(body.bodyClass));

    setFieldValue(QStringLiteral("parentId"), formatId(body.parentId));

    QString parentName = QStringLiteral("—");
    if (body.parentId >= 0) {
        const auto parentIt = bodyMap.constFind(body.parentId);
        if (parentIt == bodyMap.constEnd()) {
            parentName = QStringLiteral("ID %1").arg(body.parentId);
        } else {
            const CelestialBody& parent = parentIt.value();
            parentName = parent.name.isEmpty()
                ? QStringLiteral("ID %1").arg(parent.id)
                : QStringLiteral("%1 (ID %2)").arg(parent.name, QString::number(parent.id));

            if (body.orbitsBarycenter && OrbitClassifier::isBarycenterType(parent.type)) {
                parentName += QStringLiteral(" — барицентр пары");
            }
        }
    }

    setFieldValue(QStringLiteral("parentName"), parentName);
    setFieldValue(QStringLiteral("relation"), formatText(body.parentRelationType));
    setFieldValue(QStringLiteral("children"), QString::number(body.children.size()));

    setFieldValue(QStringLiteral("distance"), formatDouble(body.distanceToArrivalLs, 2) + QStringLiteral(" ls"));
    setFieldValue(QStringLiteral("semiMajorAxis"), formatDouble(body.semiMajorAxisAu, 5) + QStringLiteral(" AU"));
    setFieldValue(QStringLiteral("orbitsBarycenter"), yesNo(body.orbitsBarycenter));

    setFieldValue(QStringLiteral("physicalRadius"), formatDouble(body.physicalRadiusKm, 2) + QStringLiteral(" км"));
}

void BodyDetailsWidget::setPlaceholderText(const QString& text) {
    m_placeholderLabel->setText(text);
    m_placeholderLabel->show();
    m_toolBox->hide();
}

void BodyDetailsWidget::setFieldValue(const QString& key, const QString& value) {
    auto it = m_fields.find(key);
    if (it != m_fields.end()) {
        it.value()->setText(value);
    }
}

QString BodyDetailsWidget::formatText(const QString& value) const {
    return value.trimmed().isEmpty() ? QStringLiteral("—") : value;
}

QString BodyDetailsWidget::formatId(const int id) const {
    return id >= 0 ? QString::number(id) : QStringLiteral("—");
}

QString BodyDetailsWidget::formatDouble(const double value, const int precision) const {
    return qFuzzyIsNull(value) ? QStringLiteral("—") : QString::number(value, 'f', precision);
}

QString BodyDetailsWidget::bodyClassText(const CelestialBody::BodyClass bodyClass) const {
    switch (bodyClass) {
    case CelestialBody::BodyClass::Star:
        return QStringLiteral("Star");
    case CelestialBody::BodyClass::Planet:
        return QStringLiteral("Planet");
    case CelestialBody::BodyClass::Moon:
        return QStringLiteral("Moon");
    case CelestialBody::BodyClass::Barycenter:
        return QStringLiteral("Barycenter");
    case CelestialBody::BodyClass::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

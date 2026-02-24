#include "BodyDetailsWidget.h"

#include "OrbitClassifier.h"

#include <QFormLayout>
#include <QLabel>
#include <QStringList>
#include <QToolBox>
#include <QVBoxLayout>

namespace {

constexpr double kEarthGravityMs2 = 9.80665;

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
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Кратко"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("name"), QStringLiteral("Имя:"));
        addField(form, QStringLiteral("type"), QStringLiteral("Тип:"));
        addField(form, QStringLiteral("class"), QStringLiteral("Класс:"));
    }

    {
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Орбита"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("parent"), QStringLiteral("Родительское тело:"));
        addField(form, QStringLiteral("relation"), QStringLiteral("Тип орбиты:"));
        addField(form, QStringLiteral("semiMajorAxis"), QStringLiteral("Большая полуось:"));
        addField(form, QStringLiteral("orbitsBarycenter"), QStringLiteral("Вокруг барицентра:"));
    }

    {
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Физические параметры"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("gravity"), QStringLiteral("Гравитация:"));
        addField(form, QStringLiteral("temperature"), QStringLiteral("Температура поверхности:"));
        addField(form, QStringLiteral("physicalRadius"), QStringLiteral("Радиус:"));
        addField(form, QStringLiteral("mass"), QStringLiteral("Масса:"));
        addField(form, QStringLiteral("dayLength"), QStringLiteral("Длительность суток:"));
        addField(form, QStringLiteral("axialTilt"), QStringLiteral("Осевой наклон:"));
    }

    {
        auto* page = createSectionPage(m_toolBox, QStringLiteral("Атмосфера и состав"));
        auto* form = qobject_cast<QFormLayout*>(page->layout());
        addField(form, QStringLiteral("composition"), QStringLiteral("Состав:"));
        addField(form, QStringLiteral("pressure"), QStringLiteral("Давление:"));
        addField(form, QStringLiteral("volcanism"), QStringLiteral("Вулканизм:"));
        addField(form, QStringLiteral("terraforming"), QStringLiteral("Терраформируемость:"));
    }

    setPlaceholderText(QStringLiteral("Кликните по телу, чтобы увидеть параметры."));
}

void BodyDetailsWidget::setBody(const CelestialBody& body, const QHash<int, CelestialBody>& bodyMap) {
    m_placeholderLabel->hide();
    m_toolBox->show();

    setFieldValue(QStringLiteral("name"), formatText(body.name));
    setFieldValue(QStringLiteral("type"), formatText(body.type));
    setFieldValue(QStringLiteral("class"), bodyClassText(body.bodyClass));

    QString parentName = QStringLiteral("нет данных");
    if (body.parentId >= 0) {
        const auto parentIt = bodyMap.constFind(body.parentId);
        if (parentIt == bodyMap.constEnd()) {
            parentName = QStringLiteral("ID %1").arg(body.parentId);
        } else {
            const CelestialBody& parent = parentIt.value();
            parentName = parent.name.trimmed().isEmpty()
                ? QStringLiteral("ID %1").arg(parent.id)
                : QStringLiteral("%1 (ID %2)").arg(parent.name, QString::number(parent.id));

            if (body.orbitsBarycenter && OrbitClassifier::isBarycenterType(parent.type)) {
                parentName += QStringLiteral(" — барицентр пары");
            }
        }
    }

    setFieldValue(QStringLiteral("parent"), parentName);
    setFieldValue(QStringLiteral("relation"), formatText(body.parentRelationType));
    setFieldValue(QStringLiteral("semiMajorAxis"), formatSemiMajorAxis(body.semiMajorAxisAu));
    setFieldValue(QStringLiteral("orbitsBarycenter"), yesNo(body.orbitsBarycenter));

    setFieldValue(QStringLiteral("gravity"), formatGravity(body.surfaceGravityMs2));
    setFieldValue(QStringLiteral("temperature"), formatTemperature(body.surfaceTemperatureK));
    setFieldValue(QStringLiteral("physicalRadius"), formatRadiusKm(body.physicalRadiusKm));
    setFieldValue(QStringLiteral("mass"), formatMass(body));
    setFieldValue(QStringLiteral("dayLength"), formatDayLength(body.rotationPeriodDays, body.isTidallyLocked));
    setFieldValue(QStringLiteral("axialTilt"), formatAxialTilt(body.axialTiltDeg));

    setFieldValue(QStringLiteral("composition"), formatComposition(body.atmoComposition));
    setFieldValue(QStringLiteral("pressure"), formatPressure(body.atmospherePressureAtm));
    setFieldValue(QStringLiteral("volcanism"), formatText(body.volcanism));
    setFieldValue(QStringLiteral("terraforming"), formatText(body.terraformingState));
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

QString BodyDetailsWidget::fallbackText(const QString& value) const {
    return value.trimmed().isEmpty() ? QStringLiteral("нет данных") : value;
}

QString BodyDetailsWidget::formatText(const QString& value) const {
    return fallbackText(value.trimmed());
}

QString BodyDetailsWidget::formatDouble(const double value, const int precision) const {
    return qFuzzyIsNull(value) ? QString() : QString::number(value, 'f', precision);
}

QString BodyDetailsWidget::formatSemiMajorAxis(double value) const {
    const QString number = formatDouble(value, 5);
    return fallbackText(number.isEmpty() ? QString() : number + QStringLiteral(" а.е."));
}

QString BodyDetailsWidget::formatRadiusKm(double value) const {
    const QString number = formatDouble(value, 2);
    return fallbackText(number.isEmpty() ? QString() : number + QStringLiteral(" км"));
}

QString BodyDetailsWidget::formatGravity(double valueMs2) const {
    if (qFuzzyIsNull(valueMs2)) {
        return QStringLiteral("нет данных");
    }

    // Перевод в g: отношение ускорения свободного падения к стандартной земной g0.
    const double valueInG = valueMs2 / kEarthGravityMs2;
    return QStringLiteral("%1 g (%2 м/с²)")
        .arg(QString::number(valueInG, 'f', 2), QString::number(valueMs2, 'f', 2));
}

QString BodyDetailsWidget::formatTemperature(double valueK) const {
    const QString number = formatDouble(valueK, 1);
    return fallbackText(number.isEmpty() ? QString() : number + QStringLiteral(" K"));
}

QString BodyDetailsWidget::formatMass(const CelestialBody& body) const {
    if (!qFuzzyIsNull(body.massEarth)) {
        return QStringLiteral("%1 M⊕").arg(QString::number(body.massEarth, 'f', 3));
    }

    if (!qFuzzyIsNull(body.massSolar)) {
        return QStringLiteral("%1 M☉").arg(QString::number(body.massSolar, 'f', 6));
    }

    return QStringLiteral("нет данных");
}

QString BodyDetailsWidget::formatDayLength(double valueDays, bool tidallyLocked) const {
    if (qFuzzyIsNull(valueDays)) {
        return tidallyLocked ? QStringLiteral("синхронное вращение") : QStringLiteral("нет данных");
    }

    QString text = QStringLiteral("%1 сут").arg(QString::number(valueDays, 'f', 3));
    if (tidallyLocked) {
        text += QStringLiteral(" (синхронное вращение)");
    }
    return text;
}

QString BodyDetailsWidget::formatAxialTilt(double valueDeg) const {
    const QString number = formatDouble(valueDeg, 2);
    return fallbackText(number.isEmpty() ? QString() : number + QStringLiteral("°"));
}

QString BodyDetailsWidget::formatComposition(const QVector<CelestialBody::CompositionPart>& parts) const {
    if (parts.isEmpty()) {
        return QStringLiteral("нет данных");
    }

    QStringList chunks;
    chunks.reserve(parts.size());
    for (const CelestialBody::CompositionPart& part : parts) {
        const QString name = part.name.trimmed();
        if (name.isEmpty()) {
            continue;
        }
        chunks.push_back(QStringLiteral("%1 %2%").arg(name, QString::number(part.percent, 'f', 1)));
    }

    return chunks.isEmpty() ? QStringLiteral("нет данных") : chunks.join(QStringLiteral(", "));
}

QString BodyDetailsWidget::formatPressure(double valueAtm) const {
    const QString number = formatDouble(valueAtm, 3);
    return fallbackText(number.isEmpty() ? QString() : number + QStringLiteral(" атм"));
}

QString BodyDetailsWidget::bodyClassText(const CelestialBody::BodyClass bodyClass) const {
    switch (bodyClass) {
    case CelestialBody::BodyClass::Star:
        return QStringLiteral("Звезда");
    case CelestialBody::BodyClass::Planet:
        return QStringLiteral("Планета");
    case CelestialBody::BodyClass::Moon:
        return QStringLiteral("Спутник");
    case CelestialBody::BodyClass::Barycenter:
        return QStringLiteral("Барицентр");
    case CelestialBody::BodyClass::Unknown:
    default:
        return QStringLiteral("Неизвестно");
    }
}

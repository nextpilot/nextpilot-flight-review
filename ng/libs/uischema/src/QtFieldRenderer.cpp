#include "QtFieldRenderer.h"
#include "SchemaEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>

namespace nextpilot::plotscope::uischema
{

static const char *kFieldKey = "fieldKey";

QWidget *QtFieldRenderer::render(const FormSchema &form, QFormLayout *layout, QWidget *parent)
{
    for (const FieldSpec &f : form.fields)
    {
        QWidget *w = nullptr;
        switch (f.type)
        {
        case FieldType::Bool:
        {
            auto *cb = new QCheckBox(parent);
            cb->setChecked(f.value.toBool());
            cb->setToolTip(f.description);
            w = cb;
            break;
        }
        case FieldType::Number:
        {
            const int vt = f.value.typeId();
            const bool isInteger = (vt == QMetaType::Int || vt == QMetaType::LongLong)
                || (vt == QMetaType::QString && !f.value.toString().contains(QLatin1Char('.')));
            if (isInteger)
            {
                auto *sb = new QSpinBox(parent);
                if (f.hasRange)
                {
                    sb->setRange(static_cast<int>(f.min), static_cast<int>(f.max));
                }
                sb->setValue(f.value.toInt());
                w = sb;
            }
            else
            {
                auto *sb = new QDoubleSpinBox(parent);
                if (f.hasRange)
                    sb->setRange(f.min, f.max);
                sb->setValue(f.value.toDouble());
                w = sb;
            }
            w->setToolTip(f.description);
            break;
        }
        case FieldType::Enum:
        {
            auto *combo = new QComboBox(parent);
            combo->addItems(f.options);
            const int idx = f.options.indexOf(f.value.toString());
            if (idx >= 0)
                combo->setCurrentIndex(idx);
            combo->setToolTip(f.description);
            w = combo;
            break;
        }
        case FieldType::File:
        {
            auto *le = new QLineEdit(parent);
            le->setText(f.value.toString());
            le->setPlaceholderText(f.fileFilter);
            w = le;
            break;
        }
        case FieldType::Text:
        case FieldType::ReadOnly:
        default:
        {
            auto *le = new QLineEdit(parent);
            le->setText(f.value.toString());
            le->setReadOnly(f.type == FieldType::ReadOnly);
            le->setPlaceholderText(f.description);
            w = le;
            break;
        }
        }
        w->setProperty(kFieldKey, f.key);
        w->setEnabled(f.editable && SchemaEngine::isEnabled(f, form));
        if (!f.unit.isEmpty())
        {
            auto *row = new QWidget(parent);
            auto *lay = new QHBoxLayout(row);
            lay->setContentsMargins(0, 0, 0, 0);
            lay->addWidget(w);
            lay->addWidget(new QLabel(f.unit, row));
            layout->addRow(f.label, row);
        }
        else
        {
            layout->addRow(f.label, w);
        }
    }
    return nullptr;
}

QVariant QtFieldRenderer::valueOf(QWidget *fieldWidget)
{
    if (auto *cb = qobject_cast<QCheckBox *>(fieldWidget))
        return cb->isChecked();
    if (auto *sb = qobject_cast<QSpinBox *>(fieldWidget))
        return sb->value();
    if (auto *sb = qobject_cast<QDoubleSpinBox *>(fieldWidget))
        return sb->value();
    if (auto *combo = qobject_cast<QComboBox *>(fieldWidget))
        return combo->currentText();
    if (auto *le = qobject_cast<QLineEdit *>(fieldWidget))
        return le->text();
    return QVariant();
}

void QtFieldRenderer::setValue(QWidget *fieldWidget, const QVariant &value)
{
    if (auto *cb = qobject_cast<QCheckBox *>(fieldWidget))
        cb->setChecked(value.toBool());
    else if (auto *sb = qobject_cast<QSpinBox *>(fieldWidget))
        sb->setValue(value.toInt());
    else if (auto *sb = qobject_cast<QDoubleSpinBox *>(fieldWidget))
        sb->setValue(value.toDouble());
    else if (auto *combo = qobject_cast<QComboBox *>(fieldWidget))
    {
        const int idx = combo->findText(value.toString());
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    }
    else if (auto *le = qobject_cast<QLineEdit *>(fieldWidget))
        le->setText(value.toString());
}

QString QtFieldRenderer::keyOf(QWidget *fieldWidget)
{
    return fieldWidget ? fieldWidget->property(kFieldKey).toString() : QString();
}

void QtFieldRenderer::applyEnabled(const FormSchema &form, QWidget *host)
{
    if (!host)
        return;
    const QList<QWidget *> widgets = host->findChildren<QWidget *>();
    for (const FieldSpec &f : form.fields)
        for (QWidget *w : widgets)
            if (keyOf(w) == f.key)
                w->setEnabled(f.editable && SchemaEngine::isEnabled(f, form));
}

} // namespace nextpilot::plotscope::uischema

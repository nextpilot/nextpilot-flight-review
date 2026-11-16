#include "filter/FilterParamEditor.h"

#include "filter/FilterCatalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>

FilterParamEditor::FilterParamEditor(QWidget *parent)
    : QWidget(parent)
{
    m_form = new QFormLayout(this);
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setSpacing(6);
}

void FilterParamEditor::clearForm()
{
    while (QLayoutItem *item = m_form->takeAt(0))
    {
        if (QWidget *w = item->widget())
            delete w;
        delete item;
    }
    m_fields.clear();
}

void FilterParamEditor::setFilter(const QString &id, const QJsonObject &initial)
{
    const QSignalBlocker blocker(this);
    clearForm();
    m_id = id;

    const ps::filter::Def *def = ps::filter::find(id.toUtf8().constData());
    if (!def || def->param_count == 0)
    {
        auto *tip = new QLabel(QStringLiteral("该算子无参数"));
        tip->setStyleSheet(QStringLiteral("color:#8494a0;"));
        m_form->addRow(tip);
        return;
    }

    for (int i = 0; i < def->param_count; ++i)
    {
        const ps::filter::ParamDef &p = def->params[i];
        const QJsonValue init = initial.value(QString::fromUtf8(p.key));

        if (p.type == ps::filter::ParamType::Double)
        {
            auto *spin = new QDoubleSpinBox;
            spin->setRange(p.min, p.max);
            spin->setDecimals(p.decimals);
            spin->setSingleStep(1.0);
            spin->setValue(init.isDouble() ? init.toDouble() : p.def);
            m_form->addRow(QString::fromUtf8(p.label), spin);
            m_fields.append({&p, spin});
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this] { emit changed(); });
        }
        else if (p.type == ps::filter::ParamType::Int)
        {
            auto *spin = new QSpinBox;
            spin->setRange(static_cast<int>(p.min), static_cast<int>(p.max));
            spin->setValue(init.isDouble() ? init.toInt() : static_cast<int>(p.def));
            m_form->addRow(QString::fromUtf8(p.label), spin);
            m_fields.append({&p, spin});
            connect(spin, &QSpinBox::valueChanged, this, [this] { emit changed(); });
        }
        else if (p.type == ps::filter::ParamType::Bool)
        {
            auto *check = new QCheckBox(QString::fromUtf8(p.label));
            check->setChecked(init.isBool() ? init.toBool() : p.def != 0.0);
            m_form->addRow(QString(), check);
            m_fields.append({&p, check});
            connect(check, &QCheckBox::toggled, this, [this] { emit changed(); });
        }
        else
        {
            auto *combo = new QComboBox;
            for (int k = 0; k < p.choice_count; ++k)
                combo->addItem(QString::fromUtf8(p.choices[k]), k);
            const int idx = init.isDouble() ? init.toInt() : static_cast<int>(p.def);
            combo->setCurrentIndex(qBound(0, idx, p.choice_count - 1));
            m_form->addRow(QString::fromUtf8(p.label), combo);
            m_fields.append({&p, combo});
            connect(combo, &QComboBox::currentIndexChanged, this, [this] { emit changed(); });
        }
    }
}

QJsonObject FilterParamEditor::params() const
{
    QJsonObject out;
    for (const Field &f : m_fields)
    {
        const QString key = QString::fromUtf8(f.def->key);
        switch (f.def->type)
        {
        case ps::filter::ParamType::Double:
            out.insert(key, static_cast<QDoubleSpinBox *>(f.widget)->value());
            break;
        case ps::filter::ParamType::Int:
            out.insert(key, static_cast<QSpinBox *>(f.widget)->value());
            break;
        case ps::filter::ParamType::Bool:
            out.insert(key, static_cast<QCheckBox *>(f.widget)->isChecked());
            break;
        case ps::filter::ParamType::Choice:
            out.insert(key, static_cast<QComboBox *>(f.widget)->currentData().toInt());
            break;
        }
    }
    return out;
}

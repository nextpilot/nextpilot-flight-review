#include "QtFieldRenderer.h"
#include "SchemaEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace nextpilot::plotscope::uischema
{

static const char *kFieldKey = "fieldKey";

// 建一个叶子字段的编辑控件，返回承载值的 widget（带 fieldKey）。
static QWidget *createEditor(const FieldSpec &f, QWidget *parent)
{
    QWidget *w = nullptr;
    switch (f.type)
    {
    case FieldType::Bool:
    {
        auto *cb = new QCheckBox(parent);
        cb->setChecked(f.value.toBool());
        w = cb;
        break;
    }
    case FieldType::Number:
    {
        const int vt = f.value.typeId();
        const bool isInteger = (vt == QMetaType::Int || vt == QMetaType::LongLong)
            || f.precision == 0
            || (f.precision < 0 && vt == QMetaType::QString
                && !f.value.toString().contains(QLatin1Char('.')));
        if (isInteger)
        {
            auto *sb = new QSpinBox(parent);
            int lo = f.hasRange ? static_cast<int>(f.min) : -1000000;
            int hi = f.hasRange ? static_cast<int>(f.max) : 1000000;
            if (f.nonNegative)
                lo = 0;
            sb->setRange(lo, hi);
            sb->setValue(f.value.toInt());
            w = sb;
        }
        else
        {
            auto *sb = new QDoubleSpinBox(parent);
            const double lo = f.hasRange ? f.min : -1e12;
            const double hi = f.hasRange ? f.max : 1e12;
            sb->setRange(f.nonNegative ? 0.0 : lo, hi);
            if (f.precision > 0)
                sb->setDecimals(f.precision);
            sb->setValue(f.value.toDouble());
            w = sb;
        }
        break;
    }
    case FieldType::Select:
    {
        auto *combo = new QComboBox(parent);
        for (const EnumOption &e : f.options)
            combo->addItem(e.label, e.value);
        const int idx = combo->findData(f.value);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        w = combo;
        break;
    }
    case FieldType::File:
    {
        auto *host = new QWidget(parent);
        auto *row = new QHBoxLayout(host);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        auto *line = new QLineEdit(host);
        line->setText(f.value.toString());
        line->setObjectName(QStringLiteral("uischemaFileValue"));
        auto *btn = new QPushButton(QStringLiteral("浏览…"), host);
        const QString filter = f.fileFilter.isEmpty() ? QStringLiteral("所有文件 (*.*)") : f.fileFilter;
        QObject::connect(btn, &QPushButton::clicked, host, [line, filter] {
            const QString picked = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择文件"),
                                                                QString(), filter);
            if (!picked.isEmpty())
                line->setText(picked);
        });
        row->addWidget(line, 1);
        row->addWidget(btn);
        w = host;
        break;
    }
    case FieldType::MultiLine:
    {
        auto *edit = new QPlainTextEdit(parent);
        edit->setPlainText(f.value.toString());
        w = edit;
        break;
    }
    case FieldType::Text:
    case FieldType::ReadOnly:
    default:
    {
        auto *le = new QLineEdit(parent);
        le->setText(f.value.toString());
        le->setReadOnly(f.type == FieldType::ReadOnly);
        w = le;
        break;
    }
    }
    w->setProperty(kFieldKey, f.key);
    if (!f.description.isEmpty())
        w->setToolTip(f.description);
    return w;
}

// 把单个叶子字段加进 form：一行 label + editor（带单位则并排）。
static void addLeaf(const FieldSpec &f, QFormLayout *form, QWidget *parent)
{
    QWidget *editor = createEditor(f, parent);
    if (!f.unit.isEmpty())
    {
        auto *row = new QWidget(parent);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->addWidget(editor, 1);
        lay->addWidget(new QLabel(f.unit, row));
        editor = row;
    }
    // hidden 不占 label 位，控件隐藏；启用态由 applyEnabled 统一刷新。
    form->addRow(f.hidden ? QString() : f.label, editor);
    if (f.hidden)
        editor->hide();
}

// 递归把字段树渲染进 form。布局字段建容器，作为一整行加入。
static void renderInto(const QVector<FieldSpec> &fields, QFormLayout *form, QWidget *parent)
{
    for (const FieldSpec &f : fields)
    {
        switch (f.type)
        {
        case FieldType::Tab:
        {
            auto *tabs = new QTabWidget(parent);
            for (const FieldSpec &tab : f.fields)
            {
                auto *page = new QWidget(tabs);
                auto *inner = new QFormLayout(page);
                renderInto(tab.fields, inner, page);
                tabs->addTab(page, tab.label.isEmpty() ? tab.key : tab.label);
            }
            form->addRow(tabs);
            break;
        }
        case FieldType::Section:
        {
            auto *box = new QGroupBox(f.label, parent);
            auto *inner = new QFormLayout(box);
            renderInto(f.fields, inner, box);
            form->addRow(box);
            break;
        }
        case FieldType::Horizontal:
        case FieldType::Vertical:
        {
            auto *box = new QWidget(parent);
            auto *lay = f.type == FieldType::Horizontal
                            ? static_cast<QLayout *>(new QHBoxLayout(box))
                            : static_cast<QLayout *>(new QVBoxLayout(box));
            for (const FieldSpec &child : f.fields)
            {
                auto *cell = new QWidget(box);
                auto *cellLay = new QHBoxLayout(cell);
                cellLay->setContentsMargins(0, 0, 0, 0);
                cellLay->addWidget(new QLabel(child.label, cell));
                cellLay->addWidget(createEditor(child, cell), 1);
                lay->addWidget(cell);
            }
            form->addRow(f.label.isEmpty() ? QString() : f.label, box);
            break;
        }
        default:
            addLeaf(f, form, parent);
            break;
        }
    }
}

void QtFieldRenderer::render(const FormSchema &form, QFormLayout *layout, QWidget *parent)
{
    if (!layout)
        return;
    renderInto(form.fields, layout, parent);
    applyEnabled(form, parent);
}

QVariant QtFieldRenderer::valueOf(QWidget *fieldWidget)
{
    if (!fieldWidget)
        return QVariant();
    // file 字段的 fieldKey 在容器上，值在其内部 QLineEdit。
    if (auto *line = fieldWidget->findChild<QLineEdit *>(QStringLiteral("uischemaFileValue")))
        return line->text();
    if (auto *cb = qobject_cast<QCheckBox *>(fieldWidget))
        return cb->isChecked();
    if (auto *sb = qobject_cast<QSpinBox *>(fieldWidget))
        return sb->value();
    if (auto *sb = qobject_cast<QDoubleSpinBox *>(fieldWidget))
        return sb->value();
    if (auto *combo = qobject_cast<QComboBox *>(fieldWidget))
        return combo->currentData().isValid() ? combo->currentData() : combo->currentText();
    if (auto *edit = qobject_cast<QPlainTextEdit *>(fieldWidget))
        return edit->toPlainText();
    if (auto *le = qobject_cast<QLineEdit *>(fieldWidget))
        return le->text();
    return QVariant();
}

void QtFieldRenderer::setValue(QWidget *fieldWidget, const QVariant &value)
{
    if (!fieldWidget)
        return;
    if (auto *line = fieldWidget->findChild<QLineEdit *>(QStringLiteral("uischemaFileValue")))
        line->setText(value.toString());
    else if (auto *cb = qobject_cast<QCheckBox *>(fieldWidget))
        cb->setChecked(value.toBool());
    else if (auto *sb = qobject_cast<QSpinBox *>(fieldWidget))
        sb->setValue(value.toInt());
    else if (auto *sb = qobject_cast<QDoubleSpinBox *>(fieldWidget))
        sb->setValue(value.toDouble());
    else if (auto *combo = qobject_cast<QComboBox *>(fieldWidget))
    {
        const int idx = combo->findData(value);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
    }
    else if (auto *edit = qobject_cast<QPlainTextEdit *>(fieldWidget))
        edit->setPlainText(value.toString());
    else if (auto *le = qobject_cast<QLineEdit *>(fieldWidget))
        le->setText(value.toString());
}

QString QtFieldRenderer::keyOf(QWidget *fieldWidget)
{
    return fieldWidget ? fieldWidget->property(kFieldKey).toString() : QString();
}

// 收集树里所有叶子字段（含布局容器内的后代）。
static void collectLeaves(const QVector<FieldSpec> &fields, QVector<FieldSpec> &out)
{
    for (const FieldSpec &f : fields)
    {
        if (f.type == FieldType::Tab || f.type == FieldType::Section
            || f.type == FieldType::Horizontal || f.type == FieldType::Vertical)
            collectLeaves(f.fields, out);
        else
            out.append(f);
    }
}

void QtFieldRenderer::applyEnabled(const FormSchema &form, QWidget *host)
{
    if (!host)
        return;
    QVector<FieldSpec> leaves;
    collectLeaves(form.fields, leaves);
    const QList<QWidget *> widgets = host->findChildren<QWidget *>();

    // 先从当前控件收集回传值，dependsOn 用实时值判定（切换下拉即联动）。
    QHash<QString, QVariant> current;
    for (QWidget *w : widgets)
    {
        const QString key = keyOf(w);
        if (!key.isEmpty() && !current.contains(key))
            current.insert(key, valueOf(w));
    }

    for (const FieldSpec &f : leaves)
    {
        const bool ok = f.editable && !f.hidden && SchemaEngine::isEnabled(f, current);
        for (QWidget *w : widgets)
            if (keyOf(w) == f.key)
            {
                w->setEnabled(ok);
                // dependsOn 未满足时一并隐藏，语义等同旧 visible_if。
                if (!f.dependsOn.isEmpty())
                    w->setVisible(ok);
            }
    }
}

} // namespace nextpilot::plotscope::uischema

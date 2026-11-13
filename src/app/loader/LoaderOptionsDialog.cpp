#include "loader/LoaderOptionsDialog.h"

#include "loader/LoaderPlugin.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPalette>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

QString kindOf(const QJsonObject &c)
{
    return c.value(QStringLiteral("kind")).toString();
}

void setNested(QJsonObject &o, const QString &path, const QJsonValue &value)
{
    const QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return;
    if (parts.size() == 1)
    {
        o.insert(parts.first(), value);
        return;
    }
    QJsonObject child = o.value(parts.first()).toObject();
    setNested(child, QStringList(parts.mid(1)).join(QLatin1Char('.')), value);
    o.insert(parts.first(), child);
}

} // namespace

LoaderOptionsDialog::LoaderOptionsDialog(const LoaderPlugin &plugin, const QString &path, QWidget *parent)
    : QDialog(parent), m_plugin(plugin), m_path(path)
{
    setWindowTitle(QStringLiteral("%1 · %2").arg(plugin.schema.value(QStringLiteral("title")).toString(plugin.label),
                                                 QFileInfo(path).fileName()));
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(QStringLiteral("#0d1117")));
    setPalette(pal);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#0d1117;color:#c9d1d9;}"
        "QLabel{color:#c9d1d9;}"
        "QGroupBox{background:#0d1117;border:1px solid #30363d;margin-top:8px;padding-top:6px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 4px;color:#8b949e;}"
        "QComboBox,QSpinBox,QCheckBox{background:#161b22;color:#c9d1d9;border:1px solid #30363d;"
        "padding:3px 6px;min-height:20px;}"
        "QComboBox::drop-down{border:0;}"
        "QCheckBox{padding:3px 6px;}"
        "QTableWidget{background:#0d1117;alternate-background-color:#141a21;color:#c9d1d9;"
        "gridline-color:#21262d;border:1px solid #30363d;}"
        "QHeaderView::section{background:#161b22;color:#8b949e;border:0;border-right:1px solid #21262d;"
        "padding:3px 5px;font-size:11px;}"
        "QPushButton{background:#21262d;color:#c9d1d9;border:1px solid #30363d;padding:4px 18px;border-radius:2px;}"
        "QPushButton:hover{background:#30363d;}"));
    setMinimumSize(900, 480);
    resize(980, 520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    auto *opt = new QGroupBox(QStringLiteral("解析选项"));
    m_form = new QFormLayout(opt);
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_form->setHorizontalSpacing(10);
    m_form->setVerticalSpacing(6);
    root->addWidget(opt);

    m_summary = new QLabel;
    m_summary->setStyleSheet(QStringLiteral("color:#c9d1d9;font-size:11.5px;"));
    root->addWidget(m_summary);

    m_preview = new QTableWidget;
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setAlternatingRowColors(true);
    m_preview->horizontalHeader()->setStretchLastSection(false);
    m_preview->verticalHeader()->setDefaultSectionSize(20);
    root->addWidget(m_preview);

    m_warn = new QLabel;
    m_warn->setWordWrap(true);
    m_warn->setStyleSheet(QStringLiteral(
        "color:#e3b341;background:#221c0f;border:1px solid #3a2f16;padding:4px 8px;font-size:11px;"));
    root->addWidget(m_warn);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("加载"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    buildFields();
    refreshProbe(true);
}

void LoaderOptionsDialog::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(QStringLiteral("#0d1117")));
    QDialog::paintEvent(event);
}

void LoaderOptionsDialog::buildFields()
{
    const QJsonArray fields = m_plugin.schema.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &fv : fields)
    {
        const QJsonObject f = fv.toObject();
        Field field;
        field.key = f.value(QStringLiteral("key")).toString();
        field.type = f.value(QStringLiteral("type")).toString();
        field.spec = f;
        QWidget *editor = nullptr;

        if (field.type == QLatin1String("bool"))
        {
            auto *cb = new QCheckBox;
            cb->setChecked(f.value(QStringLiteral("default")).toBool(false));
            editor = cb;
        }
        else if (field.type == QLatin1String("int"))
        {
            auto *sp = new QSpinBox;
            sp->setRange(f.value(QStringLiteral("min")).toInt(0), f.value(QStringLiteral("max")).toInt(1000));
            sp->setValue(f.value(QStringLiteral("default")).toInt(0));
            sp->setFixedWidth(80);
            editor = sp;
        }
        else
        {
            auto *cb = new QComboBox;
            cb->setMinimumWidth(240);
            const QJsonArray opts = f.value(QStringLiteral("options")).toArray();
            for (const QJsonValue &ov : opts)
            {
                const QJsonObject o = ov.toObject();
                cb->addItem(o.value(QStringLiteral("label")).toString(),
                            o.value(QStringLiteral("value")).toString());
            }
            const QString def = f.value(QStringLiteral("default")).toString();
            if (!def.isEmpty())
                cb->setCurrentIndex(qMax(0, cb->findData(def)));
            editor = cb;
        }

        auto *label = new QLabel(f.value(QStringLiteral("label")).toString());
        label->setStyleSheet(QStringLiteral("color:#8b949e;font-size:11px;"));
        m_form->addRow(label, editor);
        field.editor = editor;
        field.label = label;
        m_fields.append(field);
        m_editors.insert(field.key, editor);
        m_labels.insert(field.key, label);

        if (field.type == QLatin1String("bool"))
        {
            connect(qobject_cast<QCheckBox *>(editor), &QCheckBox::toggled, this,
                    [this] { if (!m_updating) refreshProbe(false); });
        }
        else if (field.type == QLatin1String("int"))
        {
            connect(qobject_cast<QSpinBox *>(editor), QOverload<int>::of(&QSpinBox::valueChanged), this,
                    [this] { if (!m_updating) refreshProbe(false); });
        }
        else
        {
            connect(qobject_cast<QComboBox *>(editor), QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this] {
                        if (m_updating)
                            return;
                        if (m_probe.isEmpty())
                            return;
                        refreshProbe(false);
                    });
        }
    }
    updateVisibility();
}

QComboBox *LoaderOptionsDialog::comboFor(const QString &key) const
{
    return qobject_cast<QComboBox *>(m_editors.value(key, nullptr));
}

QVariant LoaderOptionsDialog::currentValue(const QString &key) const
{
    QWidget *w = m_editors.value(key, nullptr);
    if (auto *cb = qobject_cast<QCheckBox *>(w))
        return cb->isChecked();
    if (auto *sp = qobject_cast<QSpinBox *>(w))
        return sp->value();
    if (auto *co = qobject_cast<QComboBox *>(w))
        return co->currentData();
    return QVariant();
}

void LoaderOptionsDialog::setCurrentValue(const QString &key, const QVariant &value)
{
    QWidget *w = m_editors.value(key, nullptr);
    if (!w)
        return;
    QSignalBlocker block(w);
    if (auto *cb = qobject_cast<QCheckBox *>(w))
        cb->setChecked(value.toBool());
    else if (auto *sp = qobject_cast<QSpinBox *>(w))
        sp->setValue(value.toInt());
    else if (auto *co = qobject_cast<QComboBox *>(w))
    {
        int idx = co->findData(value);
        if (idx < 0)
            idx = co->findData(value.toString());
        if (idx >= 0)
            co->setCurrentIndex(idx);
    }
}

QJsonObject LoaderOptionsDialog::currentOptions() const
{
    QJsonObject o;
    for (const Field &f : m_fields)
    {
        if (!m_editors.contains(f.key))
            continue;
        const QJsonObject vi = f.spec.value(QStringLiteral("visible_if")).toObject();
        if (!vi.isEmpty())
        {
            const QVariant cur = currentValue(vi.value(QStringLiteral("key")).toString());
            if (cur.toString() != vi.value(QStringLiteral("equals")).toString())
                continue;
        }
        const QVariant v = currentValue(f.key);
        if (f.type == QLatin1String("bool"))
            setNested(o, f.key, v.toBool());
        else if (f.type == QLatin1String("int"))
            setNested(o, f.key, v.toInt());
        else if (f.spec.value(QStringLiteral("source")).toString() == QLatin1String("columns"))
            setNested(o, f.key, v.toInt());
        else
        {
            const QString s = v.toString();
            if (!s.isEmpty())
                setNested(o, f.key, s);
        }
    }
    return o;
}

void LoaderOptionsDialog::refreshProbe(bool initial)
{
    if (!m_plugin.probe)
        return;

    QJsonObject opts = currentOptions();
    QJsonObject probe = m_plugin.probe(m_path, opts);
    if (probe.contains(QStringLiteral("error")))
    {
        m_warn->setVisible(true);
        m_warn->setText(probe.value(QStringLiteral("error")).toString());
        m_summary->setText(QStringLiteral("无法探测该文件"));
        m_preview->clear();
        return;
    }
    m_probe = probe;
    m_updating = true;

    const QJsonArray cols = probe.value(QStringLiteral("columns")).toArray();
    if (initial)
    {
        const QString d = probe.value(QStringLiteral("delimiter")).toString();
        if (!d.isEmpty())
            setCurrentValue(QStringLiteral("delimiter"), d.left(1));
        setCurrentValue(QStringLiteral("has_header"), probe.value(QStringLiteral("has_header")).toBool(true));
        if (probe.contains(QStringLiteral("suggested_time")) && cols.size() > 0)
        {
            setCurrentValue(QStringLiteral("time.mode"), QStringLiteral("column"));
            setCurrentValue(QStringLiteral("time.index"),
                            probe.value(QStringLiteral("suggested_time")).toInt());
        }
    }

    // 动态列：按 kind 过滤，保留原选择
    for (const Field &f : m_fields)
    {
        if (f.spec.value(QStringLiteral("source")).toString() != QLatin1String("columns"))
            continue;
        auto *cb = comboFor(f.key);
        if (!cb)
            continue;
        const int keep = cb->currentData().toInt();
        const QString filter = f.spec.value(QStringLiteral("column_filter")).toString();
        QSignalBlocker block(cb);
        cb->clear();
        for (int i = 0; i < cols.size(); ++i)
        {
            const QJsonObject c = cols.at(i).toObject();
            const QString kind = kindOf(c);
            if (filter == QLatin1String("date_only") && kind != QLatin1String("date_only"))
                continue;
            if (filter == QLatin1String("time_only") && kind != QLatin1String("time_only"))
                continue;
            cb->addItem(c.value(QStringLiteral("name")).toString(), i);
        }
        if (keep >= 0)
        {
            const int idx = cb->findData(keep);
            if (idx >= 0)
                cb->setCurrentIndex(idx);
        }
        if (cb->count() > 0 && cb->currentIndex() < 0)
            cb->setCurrentIndex(0);
    }

    if (initial)
    {
        const QJsonValue suggested = probe.value(QStringLiteral("suggested_time"));
        auto *timeCb = comboFor(QStringLiteral("time.index"));
        if (timeCb && suggested.isDouble())
        {
            const int idx = timeCb->findData(suggested.toInt());
            if (idx >= 0)
                timeCb->setCurrentIndex(idx);
        }
    }

    updateVisibility();
    fillPreview();
    m_updating = false;
}

void LoaderOptionsDialog::updateVisibility()
{
    for (const Field &f : m_fields)
    {
        const QJsonObject vi = f.spec.value(QStringLiteral("visible_if")).toObject();
        if (vi.isEmpty())
            continue;
        const QVariant cur = currentValue(vi.value(QStringLiteral("key")).toString());
        const bool show = cur.toString() == vi.value(QStringLiteral("equals")).toString();
        if (f.editor)
            f.editor->setVisible(show);
        if (f.label)
            f.label->setVisible(show);
    }
}

void LoaderOptionsDialog::fillPreview()
{
    const QJsonArray cols = m_probe.value(QStringLiteral("columns")).toArray();
    const QJsonArray rows = m_probe.value(QStringLiteral("preview")).toArray();
    m_summary->setText(QStringLiteral("探测：%1 列 · %2 行预览")
                           .arg(cols.size())
                           .arg(rows.size()));

    m_preview->clear();
    m_preview->setColumnCount(cols.size() + 1);
    QStringList heads{QStringLiteral("#")};
    for (const QJsonValue &cv : cols)
        heads << cv.toObject().value(QStringLiteral("name")).toString();
    m_preview->setHorizontalHeaderLabels(heads);
    m_preview->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
    {
        auto *idx = new QTableWidgetItem(QString::number(r));
        idx->setForeground(QColor(QStringLiteral("#6e7681")));
        m_preview->setItem(r, 0, idx);
        const QJsonArray cells = rows.at(r).toArray();
        for (int c = 0; c < cols.size(); ++c)
        {
            const QString v = c < cells.size() ? cells.at(c).toString() : QString();
            m_preview->setItem(r, c + 1, new QTableWidgetItem(v));
        }
    }
    m_preview->resizeColumnsToContents();
    m_preview->setColumnWidth(0, 40);
    const int rowCount = qMax(1, m_preview->rowCount());
    m_preview->setFixedHeight(m_preview->horizontalHeader()->height() + rowCount * 20 + 4);

    QStringList warns;
    const QJsonArray wa = m_probe.value(QStringLiteral("warnings")).toArray();
    for (const QJsonValue &w : wa)
        warns << w.toString();
    if (!m_probe.value(QStringLiteral("has_header")).toBool() &&
        currentValue(QStringLiteral("has_header")).toBool())
        warns << QStringLiteral("探测认为首行是数据，但勾了“第一行是表头”，请确认。");
    m_warn->setVisible(!warns.isEmpty());
    m_warn->setText(warns.join(QStringLiteral("  ·  ")));
}

QString LoaderOptionsDialog::optionsJson() const
{
    return QString::fromUtf8(QJsonDocument(currentOptions()).toJson(QJsonDocument::Compact));
}

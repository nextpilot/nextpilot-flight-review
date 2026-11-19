#include "ImportDialog.h"
#include "ThemeManager.h"

#include "ExtensionRegistry.h"
#include "IMessageParser.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace nextpilot::plotscope::gui
{

namespace {

// 把 "time.mode" 这样的 key 写成嵌套 JSON：{"time":{"mode":...}}
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
    setNested(child, parts.mid(1).join(QLatin1Char('.')), value);
    o.insert(parts.first(), child);
}

struct Field
{
    QString key;
    QString type;
    QJsonObject spec;
    QWidget *editor = nullptr;
    QLabel *label = nullptr;
    bool visibleIf = false;      // 是否带 visible_if
    QString visibleKey;          // 依赖字段 key（点路径）
    QString visibleEquals;       // 依赖值
};

QWidget *primaryWidget(QWidget *w)
{
    if (auto *line = qobject_cast<QLineEdit *>(w))
        return line;
    if (auto *cb = qobject_cast<QComboBox *>(w))
        return cb;
    if (auto *sp = qobject_cast<QSpinBox *>(w))
        return sp;
    // file 字段的容器：里面的 QLineEdit 才是值来源。
    if (auto *line = w ? w->findChild<QLineEdit *>() : nullptr)
        return line;
    return w;
}

QString fieldValueString(QWidget *w, const QString &type)
{
    if (type == QLatin1String("bool"))
    {
        auto *cb = qobject_cast<QCheckBox *>(w);
        return cb && cb->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (type == QLatin1String("int"))
    {
        auto *sp = qobject_cast<QSpinBox *>(w);
        return sp ? QString::number(sp->value()) : QString();
    }
    if (auto *cb = qobject_cast<QComboBox *>(w))
        return cb->currentData().toString();
    if (auto *line = qobject_cast<QLineEdit *>(w))
        return line->text();
    return QString();
}

} // namespace

// 用 PIMPL 收纳大量控件成员，避免头文件暴露 Qt Widgets 细节。
class ImportDialog::Private
{
public:
    QComboBox *formats = nullptr;
    QLineEdit *pathLine = nullptr;
    QPushButton *browse = nullptr;
    QWidget *fieldsHost = nullptr;
    QFormLayout *fieldsForm = nullptr;
    QLabel *summary = nullptr;
    QLabel *warning = nullptr;
    QTableWidget *preview = nullptr;

    QList<nextpilot::plotscope::core::IMessageParser *> drivers;
    nextpilot::plotscope::core::IMessageParser *current = nullptr;
    QJsonObject probeData;
    QList<Field> fields;
    QString chosenPath;
};

ImportDialog::ImportDialog(nextpilot::plotscope::core::ExtensionRegistry &registry, QWidget *parent)
    : QDialog(parent), m_registry(registry), d(new Private)
{
    setWindowTitle(QStringLiteral("打开数据"));
    setMinimumSize(620, 520);
    resize(760, 620);

    // token 上色（跟随主题）。
    const QString bg = ThemeManager::themeColor(QStringLiteral("panel")).name();
    const QString fg = ThemeManager::themeColor(QStringLiteral("text")).name();
    const QString dim = ThemeManager::themeColor(QStringLiteral("textDim")).name();
    const QString border = ThemeManager::themeColor(QStringLiteral("border")).name();
    const QString accent = ThemeManager::themeColor(QStringLiteral("accent")).name();
    setStyleSheet(QStringLiteral(
        "QDialog{background:%1;color:%2;}"
        "QLabel{color:%2;}"
        "QLabel#hint{color:%3;}"
        "QGroupBox{color:%2;border:1px solid %4;margin-top:8px;padding-top:6px;}"
        "QGroupBox::title{color:%3;}"
        "QComboBox,QSpinBox,QLineEdit{background:%1;color:%2;border:1px solid %4;padding:4px 6px;min-height:22px;}"
        "QPushButton{background:%1;color:%2;border:1px solid %4;padding:5px 16px;}"
        "QPushButton:hover{border-color:%5;}"
        "QTableWidget{background:%1;color:%2;gridline-color:%4;border:1px solid %4;}"
        "QHeaderView::section{background:%1;color:%3;border:0;border-right:1px solid %4;padding:4px;}")
        .arg(bg, fg, dim, border, accent));

    auto *root = new QVBoxLayout(this);

    // 1) 格式 + 文件
    auto *top = new QGroupBox(QStringLiteral("1 · 数据源"));
    auto *topForm = new QFormLayout(top);
    d->formats = new QComboBox;
    d->pathLine = new QLineEdit;
    d->pathLine->setReadOnly(true);
    d->pathLine->setObjectName(QStringLiteral("hint"));
    d->browse = new QPushButton(QStringLiteral("选择文件…"));
    auto *pathRow = new QWidget;
    auto *pathLay = new QHBoxLayout(pathRow);
    pathLay->setContentsMargins(0, 0, 0, 0);
    pathLay->addWidget(d->pathLine, 1);
    pathLay->addWidget(d->browse);
    topForm->addRow(QStringLiteral("格式"), d->formats);
    topForm->addRow(QStringLiteral("文件"), pathRow);
    root->addWidget(top);

    // 2) 解析选项（schema 渲染）
    auto *optBox = new QGroupBox(QStringLiteral("2 · 解析选项"));
    auto *optLay = new QVBoxLayout(optBox);
    d->fieldsHost = new QWidget;
    d->fieldsForm = new QFormLayout(d->fieldsHost);
    optLay->addWidget(d->fieldsHost);
    d->summary = new QLabel;
    d->summary->setObjectName(QStringLiteral("hint"));
    d->summary->setWordWrap(true);
    optLay->addWidget(d->summary);
    d->warning = new QLabel;
    d->warning->setWordWrap(true);
    d->warning->setStyleSheet(QStringLiteral("color:#e3b341;padding:4px 6px;"));
    optLay->addWidget(d->warning);
    root->addWidget(optBox, 1);

    // 3) 预览
    auto *prevBox = new QGroupBox(QStringLiteral("3 · 预览"));
    auto *prevLay = new QVBoxLayout(prevBox);
    d->preview = new QTableWidget;
    d->preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    d->preview->setAlternatingRowColors(true);
    prevLay->addWidget(d->preview);
    root->addWidget(prevBox, 1);

    // 按钮
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("加载"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildFormatList();
    connect(d->formats, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { onFormatChanged(); });
    connect(d->browse, &QPushButton::clicked, this, [this] { chooseFile(); });
    onFormatChanged();
}

ImportDialog::~ImportDialog()
{
    delete d;
}

void ImportDialog::setPath(const QString &path)
{
    if (path.isEmpty())
        return;
    // 按后缀选格式驱动（对话框里只列了带 schema 的驱动）。
    const QFileInfo fi(path);
    const QString suffix = fi.suffix().toLower();
    for (int i = 0; i < d->drivers.size(); ++i)
    {
        if (d->drivers.at(i)->suffixes().contains(suffix))
        {
            QSignalBlocker block(d->formats);
            d->formats->setCurrentIndex(i);
            break;
        }
    }
    d->chosenPath = path;
    d->pathLine->setText(path);
    refreshProbe();
}

void ImportDialog::rebuildFormatList()
{
    d->drivers.clear();
    d->formats->clear();
    // 只列"声明了导入 schema"的格式驱动——其它格式走简化路径（无选项）。
    for (nextpilot::plotscope::core::IMessageParser *drv : m_registry.drivers())
    {
        if (drv->configSchema().isEmpty())
            continue;
        QString filter;
        for (const QString &suf : drv->suffixes())
        {
            if (!filter.isEmpty())
                filter += QStringLiteral(" ");
            filter += QStringLiteral("*.") + suf;
        }
        d->formats->addItem(drv->displayName(), QVariant::fromValue(static_cast<void *>(drv)));
        d->drivers.append(drv);
    }
}

void ImportDialog::onFormatChanged()
{
    const int idx = d->formats->currentIndex();
    d->current = (idx >= 0 && idx < d->drivers.size()) ? d->drivers.at(idx) : nullptr;
    // 文件过滤器：当前格式后缀 + 全部。
    QString filter = QStringLiteral("所有文件 (*)");
    if (d->current)
    {
        QStringList pats;
        for (const QString &suf : d->current->suffixes())
            pats << QStringLiteral("*.") + suf;
        if (!pats.isEmpty())
            filter = QStringLiteral("数据文件 (%1);;所有文件 (*)").arg(pats.join(QLatin1Char(' ')));
    }
    d->browse->setProperty("filter", filter);
    rebuildFields();
}

void ImportDialog::chooseFile()
{
    const QString filter = d->browse->property("filter").toString();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择数据文件"),
                                                      QString(), filter);
    if (path.isEmpty())
        return;
    d->chosenPath = path;
    d->pathLine->setText(path);
    refreshProbe();
}

void ImportDialog::refreshProbe()
{
    if (!d->current || d->chosenPath.isEmpty())
        return;
    d->probeData = d->current->probeFile(d->chosenPath);
    if (d->probeData.contains(QStringLiteral("error")))
    {
        d->warning->setText(d->probeData.value(QStringLiteral("error")).toString());
        d->summary->clear();
        d->preview->clear();
        return;
    }
    rebuildFields(); // source 字段依赖 probe columns，需重建选项
    updatePreview();
}

void ImportDialog::rebuildFields()
{
    // 清空旧控件
    while (d->fieldsForm->rowCount() > 0)
        d->fieldsForm->removeRow(0);
    d->fields.clear();
    if (!d->current)
        return;

    const QJsonObject schema = d->current->configSchema();
    const QJsonArray fieldSpecs = schema.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &fv : fieldSpecs)
    {
        const QJsonObject f = fv.toObject();
        Field fld;
        fld.key = f.value(QStringLiteral("key")).toString();
        fld.type = f.value(QStringLiteral("type")).toString(QStringLiteral("string"));
        fld.spec = f;

        const QString label = f.value(QStringLiteral("label")).toString(fld.key);
        QWidget *editor = nullptr;

        if (fld.type == QLatin1String("bool"))
        {
            auto *cb = new QCheckBox(f.value(QStringLiteral("hint")).toString());
            cb->setChecked(f.value(QStringLiteral("default")).toBool());
            editor = cb;
        }
        else if (fld.type == QLatin1String("int"))
        {
            auto *sp = new QSpinBox;
            sp->setRange(f.value(QStringLiteral("min")).toInt(0), f.value(QStringLiteral("max")).toInt(1000000));
            sp->setValue(f.value(QStringLiteral("default")).toInt());
            editor = sp;
        }
        else if (fld.type == QLatin1String("enum"))
        {
            auto *cb = new QComboBox;
            // source 字段：选项来自 probe 的 columns（可按 kind 过滤）。
            const QJsonObject src = f.value(QStringLiteral("source")).toObject();
            if (!src.isEmpty())
            {
                const QJsonArray items = d->probeData.value(src.value(QStringLiteral("probe_key")).toString()).toArray();
                const QString labelKey = src.value(QStringLiteral("label_key")).toString(QStringLiteral("name"));
                const QString filterKey = src.value(QStringLiteral("filter_key")).toString(QStringLiteral("kind"));
                const QString wanted = f.value(QStringLiteral("filter_kind")).toString();
                for (int ii = 0; ii < items.size(); ++ii)
                {
                    const QJsonObject item = items.at(ii).toObject();
                    const QString kind = item.value(filterKey).toString();
                    if (wanted != QLatin1String("all") && !wanted.isEmpty() && kind != wanted)
                        continue;
                    // data 存原始列下标（suggested_time 指向原始列）。
                    cb->addItem(item.value(labelKey).toString(), ii);
                }
                // initial_from_probe：suggested_time 指向的列下标
                const QJsonObject init = f.value(QStringLiteral("initial_from_probe")).toObject();
                const int suggested = d->probeData.value(init.value(QStringLiteral("probe_key")).toString()).toInt(-1);
                if (suggested >= 0)
                {
                    const int ci = cb->findData(suggested);
                    if (ci >= 0)
                        cb->setCurrentIndex(ci);
                }
            }
            else
            {
                for (const QJsonValue &ov : f.value(QStringLiteral("options")).toArray())
                {
                    const QJsonObject o = ov.toObject();
                    cb->addItem(o.value(QStringLiteral("label")).toString(), o.value(QStringLiteral("value")).toString());
                }
                const QJsonObject init = f.value(QStringLiteral("initial_from_probe")).toObject();
                if (!init.isEmpty())
                {
                    const QString pk = init.value(QStringLiteral("probe_key")).toString();
                    if (d->probeData.contains(pk))
                    {
                        QVariant v = d->probeData.value(pk).toVariant();
                        if (init.contains(QStringLiteral("trim")) && v.canConvert<QString>())
                            v = v.toString().left(init.value(QStringLiteral("trim")).toInt());
                        const int ci = cb->findData(v.toString());
                        if (ci >= 0)
                            cb->setCurrentIndex(ci);
                    }
                }
                else
                {
                    const int ci = cb->findData(f.value(QStringLiteral("default")).toString());
                    if (ci >= 0)
                        cb->setCurrentIndex(ci);
                }
            }
            editor = cb;
        }
        else if (fld.type == QLatin1String("file"))
        {
            // 文件选择字段（如自定义协议模板 .psproto）：文本框 + 浏览按钮。
            // editor 仍是 QLineEdit，值读取/可见性逻辑与 string 完全一致；
            // 浏览按钮只是便利性，不引入新的值来源（遵守单一数据源）。
            auto *host = new QWidget;
            auto *row = new QHBoxLayout(host);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(4);
            auto *line = new QLineEdit(f.value(QStringLiteral("default")).toString());
            auto *browseBtn = new QPushButton(QStringLiteral("浏览…"));
            const QString filter = f.value(QStringLiteral("filter")).toString(QStringLiteral("所有文件 (*.*)"));
            const QString title = f.value(QStringLiteral("hint")).toString(QStringLiteral("选择文件"));
            QObject::connect(browseBtn, &QPushButton::clicked, this, [this, line, title, filter] {
                const QString picked = QFileDialog::getOpenFileName(this, title, QString(), filter);
                if (!picked.isEmpty())
                    line->setText(picked);
            });
            row->addWidget(line, 1);
            row->addWidget(browseBtn);
            editor = host; // primaryWidget 需要认识这个容器
        }
        else
        {
            auto *line = new QLineEdit(f.value(QStringLiteral("default")).toString());
            editor = line;
        }

        if (!editor)
            continue;
        fld.editor = editor;
        d->fieldsForm->addRow(label, editor);
        fld.label = qobject_cast<QLabel *>(d->fieldsForm->labelForField(editor));

        const QJsonObject vi = f.value(QStringLiteral("visible_if")).toObject();
        if (!vi.isEmpty())
        {
            fld.visibleIf = true;
            fld.visibleKey = vi.value(QStringLiteral("key")).toString();
            fld.visibleEquals = vi.value(QStringLiteral("equals")).toString();
        }

        // has_header / 分隔符变化 → 重新探测预览。
        if (fld.key == QLatin1String("has_header") || fld.key == QLatin1String("delimiter"))
        {
            if (auto *cb = qobject_cast<QCheckBox *>(editor))
                connect(cb, &QCheckBox::toggled, this, [this](bool) { updateFieldVisibility(); });
            else if (auto *combo = qobject_cast<QComboBox *>(editor))
                connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                        [this](int) { updateFieldVisibility(); });
        }
        d->fields.append(fld);
    }
    updateFieldVisibility();
}

// 解析某个字段的当前值（按点路径找依赖字段）。
QString valueForKey(const QList<Field> &fields, const QString &key)
{
    for (const Field &f : fields)
        if (f.key == key)
            return fieldValueString(primaryWidget(f.editor), f.type);
    return QString();
}

void ImportDialog::updateFieldVisibility()
{
    for (Field &f : d->fields)
    {
        bool visible = true;
        if (f.visibleIf)
            visible = (valueForKey(d->fields, f.visibleKey) == f.visibleEquals);
        if (f.editor)
            f.editor->setVisible(visible);
        if (f.label)
            f.label->setVisible(visible);
    }
}

void ImportDialog::updatePreview()
{
    const QJsonObject ui = d->current->configSchema().value(QStringLiteral("ui")).toObject();
    const QJsonObject pv = ui.value(QStringLiteral("preview")).toObject();
    if (pv.isEmpty())
    {
        d->preview->setVisible(false);
        d->summary->setVisible(false);
        d->warning->setVisible(false);
        return;
    }
    d->preview->setVisible(true);
    d->summary->setVisible(true);

    const QJsonArray cols = d->probeData.value(pv.value(QStringLiteral("columns_key")).toString()).toArray();
    const QJsonArray rows = d->probeData.value(pv.value(QStringLiteral("rows_key")).toString()).toArray();
    const QString labelKey = pv.value(QStringLiteral("label_key")).toString(QStringLiteral("name"));

    QString summary = pv.value(QStringLiteral("summary")).toString();
    summary.replace(QStringLiteral("{column_count}"), QString::number(cols.size()));
    summary.replace(QStringLiteral("{row_count}"), QString::number(rows.size()));
    d->summary->setText(summary);

    QStringList heads;
    for (const QJsonValue &cv : cols)
        heads << cv.toObject().value(labelKey).toString();
    d->preview->setColumnCount(heads.size());
    d->preview->setHorizontalHeaderLabels(heads);
    d->preview->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
    {
        const QJsonArray cells = rows.at(r).toArray();
        for (int c = 0; c < cells.size() && c < heads.size(); ++c)
            d->preview->setItem(r, c, new QTableWidgetItem(cells.at(c).toString()));
    }
    d->preview->resizeColumnsToContents();

    QStringList warns;
    const QString warnsKey = pv.value(QStringLiteral("warnings_key")).toString();
    for (const QJsonValue &w : d->probeData.value(warnsKey).toArray())
        warns << w.toString();
    d->warning->setText(warns.join(QStringLiteral("  ·  ")));
    d->warning->setVisible(!warns.isEmpty());
}

void ImportDialog::accept()
{
    if (!d->current || d->chosenPath.isEmpty())
        return;
    // 收集字段值（跳过 visible_if 不满足的字段）成嵌套 JSON。
    QJsonObject options;
    for (const Field &f : d->fields)
    {
        if (!f.editor)
            continue;
        if (f.visibleIf && valueForKey(d->fields, f.visibleKey) != f.visibleEquals)
            continue;
        const QString v = fieldValueString(primaryWidget(f.editor), f.type);
        if (f.type == QLatin1String("int"))
            setNested(options, f.key, v.toInt());
        else if (f.type == QLatin1String("bool"))
            setNested(options, f.key, v == QLatin1String("true"));
        else if (!v.isEmpty())
            setNested(options, f.key, v);
    }

    m_result.path = d->chosenPath;
    m_result.format = d->current->id();
    m_result.optionsJson = QJsonDocument(options).toJson(QJsonDocument::Compact);
    QDialog::accept();
}

ImportDialog::Result ImportDialog::run(nextpilot::plotscope::core::ExtensionRegistry &registry, QWidget *parent)
{
    ImportDialog dlg(registry, parent);
    if (dlg.exec() == QDialog::Accepted)
        return dlg.result();
    return Result{};
}

} // namespace nextpilot::plotscope::gui

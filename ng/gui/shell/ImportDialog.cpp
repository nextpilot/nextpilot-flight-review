#include "ImportDialog.h"
#include "ThemeManager.h"

#include "ExtensionRegistry.h"
#include "IMessageParser.h"
#include "QtFieldRenderer.h"
#include "SchemaEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
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

#include <functional>

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
    nextpilot::plotscope::uischema::FormSchema form;
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

    // 2) 解析选项（统一 uischema 渲染）
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
    rebuildFields(); // 动态列依赖 probe，需重建选项
    updatePreview();
}

void ImportDialog::rebuildFields()
{
    // 清空旧控件（removeRow 只断布局，需手动 delete）。
    while (d->fieldsForm->rowCount() > 0)
    {
        QLayoutItem *item = d->fieldsForm->takeAt(0);
        if (QWidget *w = item->widget())
        {
            w->setParent(nullptr);
            delete w;
        }
        delete item;
    }
    if (!d->current)
        return;

    using namespace nextpilot::plotscope::uischema;
    d->form = SchemaEngine::compileJson(d->current->configSchema(), d->probeData);
    QtFieldRenderer::render(d->form, d->fieldsForm, d->fieldsHost);

    // 任一控件变化 → 按 dependsOn 刷新启用/可见（如 time.mode 切换列选择）。
    const auto widgets = d->fieldsHost->findChildren<QWidget *>();
    for (QWidget *w : widgets)
    {
        if (auto *cb = qobject_cast<QComboBox *>(w))
            connect(cb, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this] { QtFieldRenderer::applyEnabled(d->form, d->fieldsHost); });
        else if (auto *chk = qobject_cast<QCheckBox *>(w))
            connect(chk, &QCheckBox::toggled, this,
                    [this] { QtFieldRenderer::applyEnabled(d->form, d->fieldsHost); });
    }
    updatePreview();
}

void ImportDialog::updatePreview()
{
    const QJsonObject ui = d->current ? d->current->configSchema().value(QStringLiteral("ui")).toObject()
                                      : QJsonObject();
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

    using namespace nextpilot::plotscope::uischema;
    // 收集当前控件回传值。
    QHash<QString, QVariant> current;
    const auto widgets = d->fieldsHost->findChildren<QWidget *>();
    for (QWidget *w : widgets)
    {
        const QString key = QtFieldRenderer::keyOf(w);
        if (!key.isEmpty() && !current.contains(key))
            current.insert(key, QtFieldRenderer::valueOf(w));
    }

    QJsonObject options;
    // 按字段树递归收集：跳过 dependsOn 未满足、空字符串（走解析器内置默认）。
    std::function<void(const QVector<FieldSpec> &)> collect = [&](const QVector<FieldSpec> &fields) {
        for (const FieldSpec &f : fields)
        {
            if (f.type == FieldType::Tab || f.type == FieldType::Section
                || f.type == FieldType::Horizontal || f.type == FieldType::Vertical)
            {
                collect(f.fields);
                continue;
            }
            if (!SchemaEngine::isEnabled(f, current))
                continue;
            const QVariant v = current.value(f.key);
            if (v.typeId() == QMetaType::QString && v.toString().isEmpty())
                continue;
            setNested(options, f.key, QJsonValue::fromVariant(v));
        }
    };
    collect(d->form.fields);

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

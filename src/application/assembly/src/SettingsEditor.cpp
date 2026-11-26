// 内置 Settings editor（控件 + provider）。
// provider 是壳内置 owner core.settings 的编辑器实现，经 registerEditorProvider
// 注册（M3-a 通道，无第二注册面）；控件只依赖 IConfigurationService（职责边界：
// owner 不读写配置文件，持久化归工作台 SettingsStore）。

#include "SettingsEditorWidget.h"
#include "WorkbenchAdapters.h" // BuiltinSettingsEditorProvider（provider 随 adapters 声明）

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace nextpilot::plotscope::assembly {

namespace {
using nextpilot::plotscope::registry::ConfigurationProperty;
namespace wb = nextpilot::plotscope::workbench;

// 行标题：● 标记（已修改）+ key。
QString rowTitle(const QString &key, bool modified) { return modified ? QStringLiteral("● %1").arg(key) : key; }
} // namespace

// ================= SettingsEditorWidget =================

SettingsEditorWidget::SettingsEditorWidget(wb::IConfigurationService &svc, QWidget *parent)
    : QWidget(parent), m_svc(svc)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    // ---- 顶部：搜索 + Modified 过滤 + 层级提示 ----
    auto *top = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("搜索设置（键名 / 描述）"));
    m_modifiedOnly = new QCheckBox(QStringLiteral("Modified"), this);
    m_layerBanner = new QLabel(QStringLiteral("写入层级：user"), this);
    top->addWidget(m_search, 1);
    top->addWidget(m_modifiedOnly);
    top->addWidget(m_layerBanner);
    root->addLayout(top);

    // ---- 中部：左目录 + 右内容（属性表 / JSON 视图）----
    auto *body = new QHBoxLayout;
    m_toc = new QListWidget(this);
    m_toc->setMaximumWidth(180);
    m_toc->addItem(QStringLiteral("全部"));
    for (const auto &cfg : m_svc.configurations()) // configurations() 保持声明序；
        m_toc->addItem(cfg.title);                 // 目录排序 (order,title) 归渲染层（§5）
    body->addWidget(m_toc);

    m_jsonView = new QPlainTextEdit(this);
    m_jsonView->setReadOnly(true);
    m_jsonView->setVisible(false);
    body->addWidget(m_jsonView, 1);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    auto *formHost = new QWidget(m_scroll);
    m_scroll->setWidget(formHost);
    body->addWidget(m_scroll, 1);
    root->addLayout(body);

    // 信号接线（PIMPL 无关，直接 lambda）。
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &) { rebuild(); });
    connect(m_modifiedOnly, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
    connect(m_toc, &QListWidget::currentRowChanged, this, [this](int) { rebuild(); });

    rebuild();
    rebuildJson();
}

void SettingsEditorWidget::setSearch(const QString &text)
{
    m_search->setText(text); // 触发 textChanged → rebuild
}

void SettingsEditorWidget::setModifiedFilter(bool on) { m_modifiedOnly->setChecked(on); }

void SettingsEditorWidget::setJsonView(bool on)
{
    m_jsonMode = on;
    m_jsonView->setVisible(on);
    m_scroll->setVisible(!on);
    if (on)
        rebuildJson();
}

QString SettingsEditorWidget::jsonText() const { return m_jsonView->toPlainText(); }

int SettingsEditorWidget::visibleSettingCount() const
{
    int n = 0;
    // 用 isHidden（显式隐藏位）：控件可能未 show，isVisible 会把全部行算成不可见。
    for (const Row &r : m_rows)
        if (r.widget && !r.widget->isHidden())
            ++n;
    return n;
}

void SettingsEditorWidget::setSettingValue(const QString &key, const QJsonValue &value)
{
    m_svc.setUserValue(key, value);
    rebuild(); // 刷新 ● 标记 / 来源层
    rebuildJson();
}

QString SettingsEditorWidget::layerHintOf(const QString &key) const { return firstSetLayer(key); }

void SettingsEditorWidget::refresh()
{
    rebuild();
    rebuildJson();
}

bool SettingsEditorWidget::focusSection(const QString &section)
{
    const QString group = section.trimmed();
    if (group.isEmpty())
    {
        m_focusCategory.clear();
        m_focusDiagnostic.clear();
        rebuild();
        return false;
    }

    // section 是 ConfigurationSpec.id（稳定分组 id，如 core.data），不是 category
    // 显示标题——按 id 精确匹配 spec，命中后聚焦该组（用其属性 category 过滤行）。
    bool hit = false;
    QString matchedCategory;
    QString matchedTitle;
    for (const auto &cfg : m_svc.configurations())
    {
        if (cfg.id.compare(group, Qt::CaseInsensitive) != 0)
            continue;
        hit = true;
        matchedTitle = cfg.title.isEmpty() ? cfg.id : cfg.title;
        // 该组属性的 category（设置目录分组标题）即过滤键；无 category 时用 title。
        for (const auto &prop : cfg.properties)
        {
            if (!prop.category.isEmpty())
            {
                matchedCategory = prop.category;
                break;
            }
        }
        if (matchedCategory.isEmpty())
            matchedCategory = matchedTitle;
        break;
    }

    if (hit)
    {
        m_focusCategory = matchedCategory;
        // 聚焦时清掉搜索框，避免旧的 key 子串把分组内的项也滤掉；目录回「全部」。
        m_search->clear();
        m_toc->setCurrentRow(0);
        m_focusDiagnostic = QStringLiteral("已聚焦分组：%1").arg(matchedTitle);
    }
    else
    {
        // 无命中：退回全屏列表，并给出诊断——绝不静默吞掉 section。
        m_focusCategory.clear();
        m_search->clear();
        m_toc->setCurrentRow(0);
        m_focusDiagnostic = QStringLiteral("未找到设置分组「%1」，已显示全部").arg(group);
    }
    rebuild();
    return hit;
}

bool SettingsEditorWidget::isModified(const QString &key) const
{
    const auto ins = m_svc.inspect(key);
    if (!ins.declared)
        return false;
    // 改过 = 任一层显式设置过（对位 VSCode：与默认值不同或在 JSON 中显式写入）。
    return ins.user.set || ins.workspace.set || ins.document.set;
}

QString SettingsEditorWidget::firstSetLayer(const QString &key) const
{
    const auto ins = m_svc.inspect(key);
    if (ins.document.set)
        return QStringLiteral("document");
    if (ins.workspace.set)
        return QStringLiteral("workspace");
    if (ins.user.set)
        return QStringLiteral("user");
    return QStringLiteral("默认");
}

bool SettingsEditorWidget::matchesFilter(const ConfigurationProperty &prop) const
{
    // 目录过滤：第 0 行 = 全部；否则按标题精确匹配所属组。
    const int toc = m_toc->currentRow();
    if (toc > 0)
    {
        const auto configs = m_svc.configurations();
        const auto *ownerCfg = [&]() -> const nextpilot::plotscope::registry::ConfigurationSpec * {
            for (const auto &c : configs)
                for (const auto &p : c.properties)
                    if (p.key == prop.key)
                        return &c;
            return nullptr;
        }();
        if (ownerCfg && ownerCfg->title != m_toc->item(toc)->text())
            return false;
    }
    // 分组聚焦（section → category）：命中后只显示该分组的属性。
    if (!m_focusCategory.isEmpty() && prop.category != m_focusCategory)
        return false;
    // 搜索：key + description 子串（不区分大小写）。
    const QString needle = m_search->text().trimmed();
    if (!needle.isEmpty())
    {
        const QString hay =
            (prop.key + QLatin1Char(' ') + prop.description + QLatin1Char(' ') + prop.markdownDescription).toLower();
        if (!hay.contains(needle.toLower()))
            return false;
    }
    // Modified 过滤：只显示改过的项。
    if (m_modifiedOnly->isChecked() && !isModified(prop.key))
        return false;
    return true;
}

void SettingsEditorWidget::rebuild()
{
    auto *formHost = m_scroll->widget();
    delete formHost->layout();
    auto *form = new QFormLayout(formHost);
    form->setContentsMargins(4, 4, 4, 4);
    m_rows.clear();

    // 属性按 category 分组渲染（§4.1：category = 设置目录分组标题）：
    // 同一 category 的行连续显示，切换 category 时插入一条分组标题；
    // 无 category 的属性散排在最后（保持声明序）。标题行不进 m_rows（不计可见行数）。
    struct Entry
    {
        ConfigurationProperty prop;
    };
    QList<Entry> entries;
    for (const auto &cfg : m_svc.configurations())
        for (const auto &prop : cfg.properties)
            entries.append(Entry{prop});
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &a, const Entry &b) { return a.prop.category < b.prop.category; });

    QString currentCategory;
    QLabel *currentHeader = nullptr; // 分组标题随组内行显隐（搜索过滤后组空则藏标题）
    for (const auto &entry : entries)
    {
        const auto &prop = entry.prop;
        if (!prop.category.isEmpty() && prop.category != currentCategory)
        {
            currentCategory = prop.category;
            currentHeader = new QLabel(prop.category, formHost);
            currentHeader->setStyleSheet(QStringLiteral("font-weight: bold; color: palette(mid);"));
            currentHeader->setHidden(true); // 等组内首个可见行点亮
            form->addRow(currentHeader);
        }
        {
            const bool visible = matchesFilter(prop);
            auto *row = new QWidget(formHost);
            auto *lay = new QHBoxLayout(row);
            lay->setContentsMargins(0, 0, 0, 0);

            auto *keyLabel = new QLabel(rowTitle(prop.key, isModified(prop.key)), row);
            keyLabel->setToolTip(prop.description.isEmpty() ? prop.markdownDescription : prop.description);
            lay->addWidget(keyLabel, 0);

            // 控件按元数据生成（§5）。
            const QJsonValue cur = m_svc.effectiveValue(prop.key);
            QWidget *ctl = nullptr;
            if (prop.type == QLatin1String("boolean"))
            {
                auto *cb = new QCheckBox(row);
                cb->setChecked(cur.toBool());
                connect(cb, &QCheckBox::toggled, this,
                        [this, key = prop.key](bool v) { setSettingValue(key, QJsonValue(v)); });
                ctl = cb;
            }
            else if (!prop.enumValues.isEmpty())
            {
                auto *box = new QComboBox(row);
                for (int i = 0; i < prop.enumValues.size(); ++i)
                {
                    const QString desc =
                        i < prop.enumDescriptions.size() ? prop.enumDescriptions.at(i) : prop.enumValues.at(i);
                    box->addItem(desc, prop.enumValues.at(i));
                }
                const int idx = box->findData(cur.toString());
                if (idx >= 0)
                    box->setCurrentIndex(idx);
                connect(box, &QComboBox::currentIndexChanged, this, [this, key = prop.key, box](int) {
                    setSettingValue(key, QJsonValue(box->currentData().toString()));
                });
                ctl = box;
            }
            else if (prop.type == QLatin1String("number"))
            {
                auto *spin = new QDoubleSpinBox(row);
                if (prop.hasMinimum)
                    spin->setMinimum(prop.minimum);
                if (prop.hasMaximum)
                    spin->setMaximum(prop.maximum);
                spin->setValue(cur.toDouble());
                connect(spin, &QDoubleSpinBox::valueChanged, this,
                        [this, key = prop.key](double v) { setSettingValue(key, QJsonValue(v)); });
                ctl = spin;
            }
            else if (prop.editPresentation == QLatin1String("multiline"))
            {
                auto *multi = new QPlainTextEdit(row);
                multi->setPlainText(cur.toString());
                multi->setMaximumHeight(72);
                connect(multi, &QPlainTextEdit::textChanged, this,
                        [this, key = prop.key, multi] { setSettingValue(key, QJsonValue(multi->toPlainText())); });
                ctl = multi;
            }
            else // string（含缺省类型）→ 输入框
            {
                auto *edit = new QLineEdit(row);
                edit->setText(cur.toString());
                connect(edit, &QLineEdit::textChanged, this,
                        [this, key = prop.key](const QString &v) { setSettingValue(key, QJsonValue(v)); });
                ctl = edit;
            }
            lay->addWidget(ctl, 1);

            // 层级提示（来源层）。
            auto *src = new QLabel(firstSetLayer(prop.key), row);
            lay->addWidget(src);

            // 逐项 reset。
            auto *resetBtn = new QPushButton(QStringLiteral("重置"), row);
            resetBtn->setToolTip(QStringLiteral("恢复 %1 的默认值").arg(prop.key));
            connect(resetBtn, &QPushButton::clicked, this, [this, key = prop.key]() {
                m_svc.reset(key);
                rebuild();
                rebuildJson();
            });
            lay->addWidget(resetBtn);

            form->addRow(row);
            m_rows.append(Row{prop.key, row, src, keyLabel, isModified(prop.key)});
            row->setVisible(visible);
            if (visible && currentHeader)
                currentHeader->setHidden(false);
        }
    }
}

void SettingsEditorWidget::rebuildJson()
{
    QJsonObject obj;
    for (const auto &cfg : m_svc.configurations())
        for (const auto &prop : cfg.properties)
            obj.insert(prop.key, m_svc.effectiveValue(prop.key));
    m_jsonView->setPlainText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)));
}

// ================= BuiltinSettingsEditorProvider =================

bool BuiltinSettingsEditorProvider::canOpen(const registry::ResourceRef &ref) const
{
    // 只认 settings: 伪协议 + application/json（与声明 selector 双保险）。
    return ref.mime == QLatin1String("application/json") && ref.uri.startsWith(QLatin1String("settings:"));
}

QWidget *BuiltinSettingsEditorProvider::createEditor(const registry::ResourceRef &ref, QWidget *parent)
{
    auto *page = new SettingsEditorWidget(m_svc, parent);
    page->setResourceRef(ref); // 页签能回答自己打开的是哪个 ResourceRef（§3.0.12）
    return page;
}

} // namespace nextpilot::plotscope::assembly

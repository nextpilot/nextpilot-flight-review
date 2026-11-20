#pragma once

// 能力层唯一头文件：贡献基类 + 声明式贡献（纯数据）+ 知识包聚合。
//
// 分层裁决（一件事一件事）：
//   - **声明式贡献**（本文件）：UI 动作/面板/脚本/数据/示例/文档——全是纯数据
//     struct，没有任何行为。包作者只填字段，不为每种资产写一个类。
//   - **行为式贡献**（IMessageParser，在 libs/parser）：真有行为（parse），继承
//     IToolContribution 走同一注册索引。
//   - 一个知识包（ToolPack）聚合任意数量的贡献，是装卸的原子单位。
//
// 历史：这里曾是 7 个 I*.h 头文件 + 一组 Generic* 包装类（每个几行、零行为），
// 已收敛为本文件——"身份四件套 + 一份声明"一件事只有一层。

#include "ContributionKind.h"
#include "Domain.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

namespace nextpilot::plotscope::registry
{

// === 贡献基类：身份四件套，注册表统一按 (kind, domain) 二维索引 ===

class IToolContribution
{
public:
    virtual ~IToolContribution() = default;

    // 全局唯一标识（跨所有 kind），如 "csv" / "ulog" / "ap.ui.tuning"。
    virtual QString id() const = 0;

    virtual QString displayName() const = 0;

    // 贡献类型（开放字符串，见 ContributionKind）。
    virtual QString kind() const = 0;

    // 业务领域（稳定分类学，见 Domain）。
    virtual Domain domain() const = 0;

    // 来自哪个知识包（provider pack id），用于按包原子装卸。
    // 由 ToolPack 在 add() 时写入；注册表也据此把贡献归到包。
    virtual QString provider() const { return m_provider; }
    void setProvider(const QString &packId) { m_provider = packId; }

private:
    QString m_provider;
};

// === 六类声明式贡献的 Spec（纯数据）===

// 一个 UI 入口：点一下发一个意图。真控件由外壳/器官按描述创建，本结构无指针。
struct UiAction
{
    QString id;        // 稳定标识，如 "ap.plot.attitude"
    QString label;     // 显示文本，如 "姿态曲线"
    QString area;      // 目标插槽：如 "menu.plot" / "dock.left" / "ribbon.home" / "canvas.tool"
    QString intentOp;  // 点击后发射的意图 op（空=仅占位）；如 "plot.render"
    QString data;      // 附加数据（可选，如预置的 PlotSpec JSON）
    QString icon;      // 图标名（可选），如 "plot.refresh"；由外壳解析成 QIcon

    // 画廊（Gallery）呈现：空=普通按钮；非空=画廊项，值即分组名（如"曲线"/"文件格式"）。
    // 同一 area 下 galleryGroup 非空的动作会被外壳收进一个 SARibbonGallery 按组展示，
    // 而不是逐个按钮——这是"plot 类型 / parse 类型都能用图库选"的数据来源。
    // core 只存字符串，缩略图由 GUI 层按 visual/id 解析，headless 仍可读声明。
    QString galleryGroup;
    // 缩略图种类：GUI 层据此画缩略图/取图标（如 "timeseries"/"csv"/"ulog"）。
    // 空则退回用 icon 字段。
    QString visual;
    // 快捷键（如 "Ctrl+O"）：空=不绑定。它是**动作的属性**，不是独立机制——
    // 由外壳建 QAction 时 setShortcut，于是快捷键与按钮/图库项触发同一个 action id、
    // 走同一条意图路径；headless 也能读到声明，随知识包装卸一起消失。
    QString shortcut;
    // 组内排序权重（默认 0，越小越靠前）。比较时先比 order，再比 id 保证稳定。
    int order = 0;
};

// 一个领域面板的描述——纯数据，不含 QWidget 指针。
// 形态属于知识包（CAN 报文浏览器长什么样取决于 DBC），通用视图层不认识它；
// 描述（本结构，headless 可查）与实现（GUI 侧工厂，注册进 PanelFactoryRegistry）
// 用同一个 id 关联。
struct PanelSpec
{
    QString id;       // 面板类型标识（唯一），如 "car.can-browser"
    QString label;    // 显示文本，如 "CAN 报文"
    QString area;     // 建议放置的区域，如 "dock.left"（空 = 由调用方决定）
    QString icon;     // 图标名（可选）
    QString data;     // 附加数据（可选，如预置参数 JSON）
};

// 一个可复用脚本/自动化片段：以文本随包分发，由脚本器官按语言选引擎注入执行。
struct ScriptSpec
{
    QString language;  // 脚本语言，如 "lua"；供脚本器官选择引擎
    QString source;    // 脚本正文（可内联，也可 from-file 懒加载）
    QStringList exports; // 注入后暴露的全局符号（如 "ps.ap"），便于诊断与文档
};

// 一份数据资产（样例数据 / 预置数据集 / 测试夹具）。
// 它不是"格式驱动"——格式驱动是"如何读"，数据资产是"读什么"（现成的样本）。
struct DataAsset
{
    QString id;          // 稳定标识
    QString label;       // 显示名
    QString location;    // 资源路径（相对知识包根目录 / qrc / 绝对，由实现约定）
    QString format;      // 建议的格式 kind（如 "csv"/"ulog"），供加载器参考，可空
    QString description; // 内容说明（可选）
};

// 一篇知识文档（格式规格 / 使用说明 / 领域知识）。
struct DocEntry
{
    QString id;
    QString title;
    QString location;    // 文档资源路径（md/html/pdf…）
    QString mime;        // 便于 UI 选择渲染器，可空
};

// 一个可运行示例（demo / 教程条目），通常是"脚本 + 数据 + 文档"的组合入口。
struct ExampleEntry
{
    QString id;
    QString title;
    QString location;    // 示例资源路径（脚本 / 数据 / Job JSON 等）
    QString description;
};

// === 六类声明式贡献：身份 + 对应 Spec 列表，无任何行为 ===

struct UiContribution final : IToolContribution
{
    QList<UiAction> actions;
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    QString kind() const override { return ContributionKind::Ui; }
    Domain domain() const override { return m_domain; }
    QString m_id, m_name;
    Domain m_domain = Domain::General;
};

struct PanelContribution final : IToolContribution
{
    QList<PanelSpec> panels;
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    QString kind() const override { return ContributionKind::Panel; }
    Domain domain() const override { return m_domain; }
    QString m_id, m_name;
    Domain m_domain = Domain::General;
};

struct ScriptContribution final : IToolContribution
{
    QList<ScriptSpec> scripts;
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    QString kind() const override { return ContributionKind::Script; }
    Domain domain() const override { return m_domain; }
    QString m_id, m_name;
    Domain m_domain = Domain::General;
};

struct DataContribution final : IToolContribution
{
    QList<DataAsset> assets;
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    QString kind() const override { return ContributionKind::Data; }
    Domain domain() const override { return m_domain; }
    QString m_id, m_name;
    Domain m_domain = Domain::General;
};

struct ExampleContribution final : IToolContribution
{
    QList<ExampleEntry> entries;
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    QString kind() const override { return ContributionKind::Example; }
    Domain domain() const override { return m_domain; }
    QString m_id, m_name;
    Domain m_domain = Domain::General;
};

struct DocContribution final : IToolContribution
{
    QList<DocEntry> entries;
    QString id() const override { return m_id; }
    QString displayName() const override { return m_name; }
    QString kind() const override { return ContributionKind::Doc; }
    Domain domain() const override { return m_domain; }
    QString m_id, m_name;
    Domain m_domain = Domain::General;
};

// === 知识包：装载/卸载的原子单位 ===

// 一个业务方向（或其下的一个功能单元）对外提供的全部工具贡献的聚合。
//
// 它是**装载/卸载的原子单位**："装上飞控包" = 一次性登记它的格式 + UI + 脚本 +
// 数据 + 例子 + 文档；"卸掉飞控包" = 一次性撤销全部，不留半套能力。
//
// 一个 contribution 的 provider() 必须等于它所属 pack 的 id——注册表据此把贡献
// 归到包，实现按包原子装卸；这也是"扩展之间互不牵连"的保证。
class IToolPack
{
public:
    virtual ~IToolPack() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual Domain domain() const = 0;

    // 该包提供的全部贡献（所有权归包本身，由 unique_ptr 持有）。
    virtual const std::vector<std::unique_ptr<IToolContribution>> &contributions() const = 0;

    // 可变访问：仅供注册阶段把贡献"移交"给注册表接管（转移 unique_ptr 所有权）。
    virtual std::vector<std::unique_ptr<IToolContribution>> &contributions() = 0;
};

// 通用知识包实现：声明式贡献用 addUi/addPanel/... 直接填 Spec（自动归组到同一
// 份贡献对象），行为式贡献（解析器等）用 add<T>() 自行构造。
class ToolPack final : public IToolPack
{
public:
    ToolPack(QString id, QString displayName, Domain domain)
        : m_id(std::move(id)), m_displayName(std::move(displayName)), m_domain(domain) {}

    QString id() const override { return m_id; }
    QString displayName() const override { return m_displayName; }
    Domain domain() const override { return m_domain; }

    const std::vector<std::unique_ptr<IToolContribution>> &contributions() const override
    {
        return m_contribs;
    }
    std::vector<std::unique_ptr<IToolContribution>> &contributions() override
    {
        return m_contribs;
    }

    // 便捷追加：构造期组装一个包（行为式贡献）。
    template <typename T, typename... Args>
    T *add(Args &&...args)
    {
        auto obj = std::make_unique<T>(std::forward<Args>(args)...);
        T *raw = obj.get();
        raw->setProvider(m_id); // 贡献自动记住所属包，供注册表按包原子装卸
        m_contribs.push_back(std::move(obj));
        return raw;
    }

    // === 声明式贡献便捷方法：传 Spec，包内自动归组，调用方不接触贡献类 ===

    UiContribution *addUi(const QString &id, const QString &name, Domain domain,
                          QList<UiAction> actions)
    {
        auto c = std::make_unique<UiContribution>();
        c->m_id = id;
        c->m_name = name;
        c->m_domain = domain;
        c->actions = std::move(actions);
        return finish(std::move(c));
    }

    PanelContribution *addPanel(const QString &id, const QString &name, Domain domain,
                                QList<PanelSpec> panels)
    {
        auto c = std::make_unique<PanelContribution>();
        c->m_id = id;
        c->m_name = name;
        c->m_domain = domain;
        c->panels = std::move(panels);
        return finish(std::move(c));
    }

    // 单脚本便捷重载：与"语言+正文+导出符号"的声明形态对齐。
    ScriptContribution *addScript(const QString &id, const QString &name, Domain domain,
                                  QString language, QString source, QStringList exports)
    {
        return addScript(id, name, domain,
                         QList<ScriptSpec>{{ std::move(language), std::move(source),
                                             std::move(exports) }});
    }

    ScriptContribution *addScript(const QString &id, const QString &name, Domain domain,
                                  QList<ScriptSpec> scripts)
    {
        auto c = std::make_unique<ScriptContribution>();
        c->m_id = id;
        c->m_name = name;
        c->m_domain = domain;
        c->scripts = std::move(scripts);
        return finish(std::move(c));
    }

    DataContribution *addData(const QString &id, const QString &name, Domain domain,
                              QList<DataAsset> assets)
    {
        auto c = std::make_unique<DataContribution>();
        c->m_id = id;
        c->m_name = name;
        c->m_domain = domain;
        c->assets = std::move(assets);
        return finish(std::move(c));
    }

    ExampleContribution *addExample(const QString &id, const QString &name, Domain domain,
                                    QList<ExampleEntry> entries)
    {
        auto c = std::make_unique<ExampleContribution>();
        c->m_id = id;
        c->m_name = name;
        c->m_domain = domain;
        c->entries = std::move(entries);
        return finish(std::move(c));
    }

    DocContribution *addDoc(const QString &id, const QString &name, Domain domain,
                            QList<DocEntry> entries)
    {
        auto c = std::make_unique<DocContribution>();
        c->m_id = id;
        c->m_name = name;
        c->m_domain = domain;
        c->entries = std::move(entries);
        return finish(std::move(c));
    }

private:
    template <typename T>
    T *finish(std::unique_ptr<T> c)
    {
        T *raw = c.get();
        raw->setProvider(m_id);
        m_contribs.push_back(std::move(c));
        return raw;
    }

    QString m_id;
    QString m_displayName;
    Domain m_domain;
    std::vector<std::unique_ptr<IToolContribution>> m_contribs;
};

} // namespace nextpilot::plotscope::registry

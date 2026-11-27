#pragma once

// 贡献体系 = 纯数据（uORB 式组织：类型提前定义好，其余全是 <T>，挂进去就完事）。
//
// 机制只有一种：Registry<T>（见 Registry.h）。本文件没有任何虚函数、没有任何
// 容器逻辑——只有六个数据 struct 和包的自我声明。身份（id / provider / domain）
// 是字段，不是虚函数；provider / domain 由 ToolboxHub::mount() 在挂载时
// 自动写入，包作者永远不用填。
//
// 包作者要做的事只有两件：
//   1. 填一份 Pack 清单（见 ToolboxHub.h，纯数据：六个列表 + 解析器）；
//   2. toolbox.mount(std::move(pack)) 挂进去；卸载 unmount(packId) 原子撤销。
//
// 查询方只做一件事：按类型读表——registry.uiActions / panels / docs / ...

#include "Domain.h"
#include "Region.h"
#include "ResourceRef.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

// === 六类贡献数据：提前定义好，仅此六种 + 解析器（行为式，见 libs/parser）===

// 一个 UI 入口：点一下发一个意图。真控件由外壳/模块按描述创建，本结构无指针。
struct UiAction
{
    QString id;        // 稳定标识，如 "ap.plot.attitude"
    QString label;     // 显示文本，如 "姿态曲线"
    QString slotId;   // 目标插槽 id（Ribbon 页/组等挂载点），如 "ribbon.group.plot.series"
    QString intentOp;  // 点击后发射的意图 op（空=仅占位）；如 "plot.render"
    QString data;      // 附加数据（可选，如预置的 PlotSpec JSON）
    QString icon;      // 图标名（可选），如 "plot.refresh"；由外壳解析成 QIcon

    // 画廊（Gallery）呈现：空=普通按钮；非空=画廊项，值即分组名（如"曲线"/"文件格式"）。
    // 同一 area 下 galleryGroup 非空的动作会被外壳收进一个 SARibbonGallery 按组展示，
    // 而不是逐个按钮——这是"plot 类型 / parse 类型都能用图库选"的数据来源。
    // core 只存字符串，缩略图由 GUI 层按 icon id 解析，headless 仍可读声明。
    QString galleryGroup;
    // 快捷键（如 "Ctrl+O"）：空=不绑定。它是**动作的属性**，不是独立机制——
    // 由外壳建 QAction 时 setShortcut，于是快捷键与按钮/图库项触发同一个 action id、
    // 走同一条意图路径；headless 也能读到声明，随扩展包装卸一起消失。
    QString shortcut;
    // 组内排序权重（默认 0，越小越靠前）。比较时先比 order，再比 id 保证稳定。
    int order = 0;

    // 呈现尺寸：true=竖占两行的主入口大按钮（ribbon 里的"大按钮"）；
    // false=普通小按钮。由外壳决定具体像素，声明里只描述"重不重要"。
    bool large = false;
    // 切换型动作（网格/边框/图例/工具模式）：外壳建成可勾选的按钮。
    bool checkable = false;

    // 归属（mount() 自动写入，包作者不填）。
    QString provider;                 // 来自哪个包（pack id），按包原子装卸的依据
    Domain domain = Domain::General;  // 所属领域 = 包的领域

    // 可见条件（空=恒真）：与命令层同一套白名单语法；registry 只存串，求值归外壳。
    // 放在结构末尾：既有包按位置初始化（id, label, area, intentOp, data…），
    // 新字段插在中间会让 when 吃掉 intentOp、intentOp 吃掉 data——c7 首编已验证过这个坑。
    QString when;
};

// === VS Code 对位的四个贡献点（契约 §3.8）===
//
// 这四类都是"模块/包声明、壳呈现"的纯数据；壳只按描述建控件/求 when，
// 不解释业务语义。没有它们，模块想加一个刷新按钮就得改 gui，域化就漏了。

// 1) 视图标题栏动作（VS Code 的 `view/title`）：面板自己的刷新/固定/导出/关闭。
//    commandId 指向命令层，点一下走同一条意图路径（与 Ribbon 按钮无差别）。
struct ViewTitleActionSpec
{
    QString viewId;    // 哪个视图（contributes.views 声明的 id）
    QString commandId; // 命令 id
    QString when;      // 上下文表达式（空=恒真）
    int order = 0;     // 标题栏内排序
    QString group;     // 分组（navigation / export …），壳按组排布，不逐条硬编码

    QString provider;                 // mount() 自动写入
    Domain domain = Domain::General;
};

// 2) 视图条目右键（VS Code 的 `view/item/context`）：菜单项绑定到"条目"而非视图。
//    when 里可用 item.* 键，但**只在 View 作用域解析**（D19）：
//    由持有该视图的模块在选中变化时写进 View 层，切视图即整层替换，跨域不可见。
struct ItemContextSpec
{
    QString viewId;    // 条目所属视图
    QString commandId; // 命令 id
    QString when;      // 可含 item.kind / item.id / item.multiSelect
    QString domain;    // 键空间归属域（data / plot / protocol …），跨层经 adapter 转（D19）
    int order = 0;

    QString provider;                 // mount() 自动写入
    Domain packDomain = Domain::General;
};

// 3) 主题语义色（VS Code 的 `colors`）：模块声明的语义色，壳解析成实际画笔/调色板。
//    只给"语义名 + 兜底色"，具体色值由主题决定——模块不该硬编码 RGB。
struct ThemeTokenSpec
{
    QString id;          // 语义名，如 "plot.cursor" / "plot.abMeasure"
    QString fallback;    // 主题未定义时的兜底色（#RRGGBB）
    QString description; // 说明（设置页/诊断里显示）

    QString provider;                 // mount() 自动写入
    Domain domain = Domain::General;
};

// 状态栏项（VS Code 的 `statusbar`，D8 推模式）：模块声明一项，壳按 side/priority 排布。
// 文本由模块经 IUiContext::updateStatus(id,text) 推送，壳不轮询业务数据。
// provider 由声明方（模块 id / pack id）填写，仅用于诊断。
enum class StatusSide { Left, Right };

struct StatusSpec
{
    QString id;          // 稳定标识，如 "data.source.count"
    StatusSide side = StatusSide::Left;
    int priority = 0;    // 同侧内升序排布（小的靠边）
    QString when;        // 可见性表达式（空=总是显示）
    QString icon;
    QString tooltip;
    QString commandId;   // 点击触发的命令 id（空=不可点击）
    QString provider;    // 声明方 id

    QString text;        // 当前显示文本（updateStatus 更新，壳只读）
};

// 一个 Activity Bar 入口：点击后切左栏 section 栈并同步 Ribbon 页。
// 纯数据，不持有 QWidget；实际控件由外壳按描述创建。
struct ActivitySpec
{
    QString id;          // 稳定标识，如 "script"
    QString label;       // 显示名，如 "脚本"
    QString icon;        // 图标名，如 "script"
    int order = 0;       // Activity Bar 排序
    QString ribbonPage;  // 点击后切到的 ribbon 页 area，如 "ribbon.page.script"
    QStringList panelTypes; // 左栏 section 栈里属于本 activity 的面板类型 id
    QString centralEditor;  // 点击后打开的中央编辑器 viewType（空=不动中央；A2b 笔2b 由 centralPanel 改名）
    ResourceRef centralRef; // 中央编辑器资源引用：由 owner 模块构造，壳只消费不解析（A2b 笔2b）

    QString provider; // mount() 自动写入
};

// 一个可复用脚本/自动化片段：以文本随包分发，由脚本模块按语言选引擎注入执行。
struct ScriptSpec
{    QString language;    // 脚本语言，如 "lua"；供脚本模块选择引擎
    QString source;      // 脚本正文（可内联，也可 from-file 懒加载）
    QStringList exports; // 注入后暴露的全局符号（如 "ps.ap"），便于诊断与文档

    QString id;       // 条目标识（唯一），如 "ap.script.helpers"
    QString label;    // 显示名
    QString provider; // mount() 自动写入
    Domain domain = Domain::General;
};

// 一份数据资产（样例数据 / 预置数据集 / 测试夹具）。
// 它不是"格式驱动"——格式驱动是"如何读"，数据资产是"读什么"（现成的样本）。
struct DataAsset
{
    QString id;          // 稳定标识
    QString label;       // 显示名
    QString location;    // 资源路径（相对扩展包根目录 / qrc / 绝对，由实现约定）
    QString format;      // 建议的格式 kind（如 "csv"/"ulog"），供加载器参考，可空
    QString description; // 内容说明（可选）

    QString provider; // mount() 自动写入
    Domain domain = Domain::General;
};

// 一篇知识文档（格式规格 / 使用说明 / 领域知识）。
struct DocEntry
{
    QString id;
    QString title;
    QString location;    // 文档资源路径（md/html/pdf…）
    QString mime;        // 便于 UI 选择渲染器，可空

    QString provider; // mount() 自动写入
    Domain domain = Domain::General;
};

// 一个可运行示例（demo / 教程条目），通常是"脚本 + 数据 + 文档"的组合入口。
struct ExampleEntry
{
    QString id;
    QString title;
    QString location;    // 示例资源路径（脚本 / 数据 / Job JSON 等）
    QString description;

    QString provider; // mount() 自动写入
    Domain domain = Domain::General;
};

// === 报告模板===

// 报告的一节。模板 = 节的有序列表，导出器按节拼版（一期支持四种 kind）：
//   title    —— 封面标题页（content = 副标题/摘要，可空）
//   metadata —— 元数据表（content = 逗号分隔的键名：time,file,dataset,…）
//   canvas   —— 画布截图节（content = 画布页签 id，空 = 当前全部画布依序各一节）
//   text     —— 文本段落（content = 正文，支持 \n 换行）
struct ReportSection
{
    QString kind;
    QString title;    // 节标题（可空）
    QString content;  // 内容（语义随 kind，见上）
};

// 一个报告模板：扩展包可贡献（如 PX4 调参包的"检查清单报告"），
// 导出管线按模板拼节，headless 可读声明。
struct ReportTemplate
{
    QString id;
    QString title;
    QString description;
    QList<ReportSection> sections;

    QString provider; // mount() 自动写入
    Domain domain = Domain::General;
};

// 包的自我声明（挂进 packs 表的那条数据）。
struct PackInfo
{
    QString id;      // 全局唯一，如 "pack.autopilot"
    QString name;    // 显示名，如 "飞控工具箱"
    Domain domain = Domain::General;
};

} // namespace nextpilot::plotscope::registry

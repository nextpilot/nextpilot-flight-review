#pragma once

// 贡献体系 = 纯数据（uORB 式组织：类型提前定义好，其余全是 <T>，挂进去就完事）。
//
// 机制只有一种：Registry<T>（见 Registry.h）。本文件没有任何虚函数、没有任何
// 容器逻辑——只有六个数据 struct 和包的自我声明。身份（id / provider / domain）
// 是字段，不是虚函数；provider / domain 由 ExtensionRegistry::mount() 在挂载时
// 自动写入，包作者永远不用填。
//
// 包作者要做的事只有两件：
//   1. 填一份 Pack 清单（见 ExtensionRegistry.h，纯数据：六个列表 + 解析器）；
//   2. toolbox.mount(std::move(pack)) 挂进去；卸载 unmount(packId) 原子撤销。
//
// 查询方只做一件事：按类型读表——registry.uiActions / panels / docs / ...

#include "Domain.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

// === 六类贡献数据：提前定义好，仅此六种 + 解析器（行为式，见 libs/parser）===

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

    // 呈现尺寸：true=竖占两行的主入口大按钮（ribbon 里的"大按钮"）；
    // false=普通小按钮。由外壳决定具体像素，声明里只描述"重不重要"。
    bool large = false;
    // 切换型动作（网格/边框/图例/工具模式）：外壳建成可勾选的按钮。
    bool checkable = false;

    // 归属（mount() 自动写入，包作者不填）。
    QString provider;                 // 来自哪个包（pack id），按包原子装卸的依据
    Domain domain = Domain::General;  // 所属领域 = 包的领域
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

    QString provider;                 // mount() 自动写入
    Domain domain = Domain::General;
};

// 一个可复用脚本/自动化片段：以文本随包分发，由脚本器官按语言选引擎注入执行。
struct ScriptSpec
{
    QString language;    // 脚本语言，如 "lua"；供脚本器官选择引擎
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
    QString location;    // 资源路径（相对知识包根目录 / qrc / 绝对，由实现约定）
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

// === 报告模板（M2.3）===

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

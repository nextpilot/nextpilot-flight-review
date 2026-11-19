#pragma once

#include "IToolContribution.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

// UI 贡献里描述的一个动作/入口——**纯数据**，不含 QWidget 指针。
//
// 为什么不直接给 QWidget*？
//   1) core 只链接 Qt6::Core，不能依赖 Qt Widgets；
//   2) 数据驱动让 headless 也能检查"某领域贡献了哪些 UI 入口"；
//   3) 真正的控件由器官的 UiContext 在 onAttach 阶段按这些描述创建、parent 给器官，
//      因而随知识包装卸、随器官析构自动回收，app 骨架永不改动。
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
    // 组内排序权重（默认 0，越小越靠前）。老代码 RibbonContribution 用 panel 数组顺序
    // + items 数组顺序钉死 UI 顺序；ng 用 order 字段表达同样的意图，让顺序可控、
    // 与模块装载顺序解耦。比较时先比 order，再比 id 保证稳定（同 order 按 id 字典序）。
    int order = 0;
};

// 界面贡献：一个知识包向 app 骨架贡献的菜单/面板/工具栏/画布工具入口集合。
class IUiContribution : public IToolContribution
{
public:
    QString kind() const override { return ContributionKind::Ui; }

    // 该贡献提供的 UI 入口（纯描述）。
    virtual QList<UiAction> actions() const = 0;
};

} // namespace nextpilot::plotscope::registry

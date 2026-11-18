#pragma once

#include "IToolContribution.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace ng::core
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
};

// 界面贡献：一个知识包向 app 骨架贡献的菜单/面板/工具栏/画布工具入口集合。
class IUiContribution : public IToolContribution
{
public:
    QString kind() const override { return ContributionKind::Ui; }

    // 该贡献提供的 UI 入口（纯描述）。
    virtual QList<UiAction> actions() const = 0;
};

} // namespace ng::core

#pragma once

#include "UiSlotRegistry.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <functional>

class QAction;
class QIcon;
class QMenu;
class QMainWindow;
class QWidget;

// SARibbon（vendored，MIT）：全局命名空间类。
class SARibbonBar;

namespace ads
{
class CDockAreaWidget;
class CDockManager;
class CDockWidget;
}

namespace nextpilot::plotscope::gui
{

// GUI 外壳绑定器：**唯一知道 SARibbon / ADS 的地方**。
//
// 架构定位（见 ARCHITECTURE.md §7.1）：
//   外壳层不设 "DockManager" / "RibbonManager" 这类中转类——
//   ADS 已提供 ads::CDockManager，SARibbon 已提供 SARibbonBar，
//   再包一层同名类只是零价值转发。本类把
//       registry::UiSlotRegistry（纯数据贡献）
//   一次性绑定成
//       SARibbonBar + ads::CDockManager
//   绑定规则集中于此，一处可换、可测、可整体替换第三方库。
//
// 一个类顶掉老代码的 RibbonManager + DockManager 两个 Manager。
//
// 职责：
//   · 插槽 → 第三方组件对象的映射（页签/分组/按钮、停靠面板、中央内容）
//   · 动作 → QAction 的创建与信号连接（动作语义由意图 op 决定，本类不解释）
//   · 布局持久化（ADS 原生 saveState/restoreState）
// 不做：
//   · 不认识任何领域业务、不知道动作意味着什么
//   · 不持有业务状态、不做数据处理
class ShellBinder
{
public:
    ShellBinder(QMainWindow *window, nextpilot::plotscope::registry::UiSlotRegistry &slotReg);
    ~ShellBinder();

    // 配置 ribbon 条（无边框窗体由 SARibbonMainWindow 提供 ribbon 条本体）。
    // 传入的 ribbon 由 SARibbonMainWindow 持有，本类只负责往里填内容。
    void createRibbon(SARibbonBar *ribbon);

    // 按插槽数据把内容停靠到指定区域；同区域重复停靠合并成标签页。
    void dock(const QString &area, const QString &title, QWidget *content);

    // 中央内容（画布区）。
    void setCentralContent(QWidget *content);

    // 重新绑定 ribbon（插槽或动作变化后调用）。
    void rebuildRibbon();

    // 面板显隐。
    void setPanelVisible(const QString &area, bool visible);
    bool isPanelVisible(const QString &area) const;

    // 面板内容与标题（诊断/测试用）。
    QWidget *panelWidget(const QString &area) const;

    // 中央内容（画布区）——不在 dock 体系里，故单独暴露。
    QWidget *centralContent() const { return m_centralContent; }
    QString panelTitle(const QString &area) const;
    int panelCount() const { return m_panels.size(); }

    // 动作 id → 按钮下拉菜单（由外壳之外提供内容，绑定器只负责挂到按钮上）。
    // 用于「网格」这类"一个大按钮 + 下拉选项"的入口；每次重建都会重新挂。
    using ActionMenuProvider = std::function<QMenu *(const QString &actionId)>;
    void setActionMenuProvider(ActionMenuProvider provider) { m_actionMenuProvider = std::move(provider); }

    // 动作 id → 就地改图标/文字（布局组大按钮在选出 m×n 后回写"实际分格"）。
    void updateActionStyle(const QString &actionId, const QIcon &icon, const QString &text);

    // 动作 id → 触发计数（测试用：验证点击确实路由到意图）。
    int actionTriggerCount(const QString &actionId) const;

    // 首帧后把左右 dock 收窄、中央拿大头（用户之后仍可自由拖宽）。
    void applyDockProportions();

    // 布局持久化（ADS 原生）。
    QByteArray saveLayout() const;
    bool restoreLayout(const QByteArray &state);

    // --- 窗口状态快照（声明式）---
    // 捕获：窗口几何 + ADS 布局 + ribbon 当前页签。
    // 这些都是"结果长什么样"的描述，不含操作序列——恢复时缺失的部分安全降级。
    QJsonObject captureState() const;
    bool applyState(const QJsonObject &state);

    // 应用 ribbon 主题（深浅）。主题状态归 ThemeManager，绑定器只负责把
    // SARibbon 这一类第三方控件的换肤套上去——它是唯一知道 SARibbon 的地方。
    void applyTheme(bool dark, const QString &ribbonQss);

    // 意图执行回调：外壳把动作转成意图，交给宿主执行器。
    // 两个参数各司其职，不可混用：
    //   · actionId —— UI 元素的稳定标识（如 data.action.reload），供需要 GUI
    //     交互的动作特判（打开文件对话框）与触发计数用；
    //   · intentOp —— 点击真正要发的意图 op（如 data.reload），由 UiAction 声明。
    // 曾把 actionId 当 op 发出，导致除 openData 外所有按钮都报"没有模块能处理意图"。
    using IntentDispatcher = std::function<void(const QString &actionId, const QString &intentOp, const QString &data)>;
    void setIntentDispatcher(IntentDispatcher dispatcher) { m_dispatch = std::move(dispatcher); }

private:
    bool m_proportionsApplied = false;

    // 确保 ADS 停靠体系已建立（幂等）。
    void ensureDockManager();

    // 装中央画布区。**必须早于任何侧边 dock**——ADS 的 addDockArea() 只在
    // "区域内 dock 数量 <= 1"时按插入参数重设根分割器方向，
    // 于是谁先注册谁决定最终方向：
    //   先中央区(Vertical) → 再侧边区(Horizontal) → 根分割器保持水平，
    //                          得到「左侧树 | 中央画布」；
    //   反过来则根分割器被翻成垂直，树在上、画布在下。
    // 老代码（src/application/shell/DockManager.cpp）用同一个招：
    // installCentral() 在 CDockManager 构造后立刻装中央区。
    void installCentral();

    QMainWindow *m_window = nullptr;
    nextpilot::plotscope::registry::UiSlotRegistry &m_slots;

    // SARibbon
    SARibbonBar *m_ribbon = nullptr;
    int m_currentPage = 0;   // 当前页签下标（状态快照用）

    // ADS
    ads::CDockManager *m_dock = nullptr;
    ads::CDockWidget *m_centralDock = nullptr; // 中央画布区的 dock
    QWidget *m_centralContent = nullptr;       // 装进中央区的面板
    bool m_centralInstalled = false;
    QHash<QString, ads::CDockAreaWidget *> m_lastArea; // area -> 该侧最后一个 DockArea
    QHash<QString, ads::CDockWidget *> m_panels;      // area -> 该侧主 dock

    // 动作
    QHash<QString, int> m_actionTriggers;    // actionId -> 触发次数
    QHash<QString, QAction *> m_actionById;   // actionId -> 已建动作（图标/文字就地更新用）
    ActionMenuProvider m_actionMenuProvider;  // 下拉菜单来源（可为空）

    IntentDispatcher m_dispatch;
};

} // namespace nextpilot::plotscope::gui

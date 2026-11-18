#pragma once

#include "UiSlotRegistry.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

class QAction;
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

namespace ng::gui
{

// GUI 外壳绑定器：**唯一知道 SARibbon / ADS 的地方**。
//
// 架构定位（见 ARCHITECTURE.md §7.1）：
//   外壳层不设 "DockManager" / "RibbonManager" 这类中转类——
//   ADS 已提供 ads::CDockManager，SARibbon 已提供 SARibbonBar，
//   再包一层同名类只是零价值转发。本类把
//       core::UiSlotRegistry（纯数据贡献）
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
    ShellBinder(QMainWindow *window, ng::core::UiSlotRegistry &slotReg);
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

    // 动作 id → 触发计数（测试用：验证点击确实路由到意图）。
    int actionTriggerCount(const QString &actionId) const;

    // 布局持久化（ADS 原生）。
    QByteArray saveLayout() const;
    bool restoreLayout(const QByteArray &state);

    // --- 窗口状态快照（声明式）---
    // 捕获：窗口几何 + ADS 布局 + ribbon 当前页签。
    // 这些都是"结果长什么样"的描述，不含操作序列——恢复时缺失的部分安全降级。
    QJsonObject captureState() const;
    bool applyState(const QJsonObject &state);

    // 意图执行回调：外壳只负责把动作转成意图，交给宿主执行器。
    using IntentDispatcher = std::function<void(const QString &actionId)>;
    void setIntentDispatcher(IntentDispatcher dispatcher) { m_dispatch = std::move(dispatcher); }

private:
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
    ng::core::UiSlotRegistry &m_slots;

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
    QHash<QString, int> m_actionTriggers; // actionId -> 触发次数

    IntentDispatcher m_dispatch;
};

} // namespace ng::gui

#pragma once

#include "UiSlotHub.h"

#include <QHash>
#include <QSet>
#include <QStringList>
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

class ActivityBar;

// GUI 外壳绑定器：**唯一知道 SARibbon / ADS 的地方**。
//
// 架构定位（见 ARCHITECTURE.md §7.1）：
//   外壳层不设 "DockManager" / "RibbonManager" 这类中转类——
//   ADS 已提供 ads::CDockManager，SARibbon 已提供 SARibbonBar，
//   再包一层同名类只是零价值转发。本类把
//       registry::UiSlotHub（纯数据贡献）
//   一次性绑定成
//       SARibbonBar + ads::CDockManager
//   绑定规则集中于此，一处可换、可测、可整体替换第三方库。
//
// Ribbon 绑定与 dock 绑定集中在一个类，不再分设 Manager。
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
    ShellBinder(QMainWindow *window, nextpilot::plotscope::registry::UiSlotHub &slotReg);
    ~ShellBinder();

    // 配置 ribbon 条（无边框窗体由 SARibbonMainWindow 提供 ribbon 条本体）。
    // 传入的 ribbon 由 SARibbonMainWindow 持有，本类只负责往里填内容。
    void createRibbon(SARibbonBar *ribbon);

    // 按插槽数据把内容停靠到指定区域；同区域重复停靠合并成标签页。
    // icon 为图标名（:/icons/<icon>.svg），空则用该区域的默认图标。
    // pageKey 是该 dock 页签的**稳定键**（面板类型 id，如 "dataset-tree"），
    // 视图页的面板开关按钮按它逐页定位——区级整组开关曾让按钮名称与
    // 实际效果不符（历史缺陷），语义改为"一个按钮管一个页签"。
    void dock(const QString &area, const QString &title, QWidget *content,
              const QString &icon = QString(), const QString &pageKey = QString());

    // 中央内容（画布区）。首个到达者占据中央 dock（画布）；之后到达的
    // （如脚本编辑器）并成同区页签、与画布平级——写代码需要全宽，
    // 这是"中央区页签化"（方案 A）的入口。title 为后续页签的显示文本。
    void setCentralContent(QWidget *content, const QString &title = QString(),
                           const QString &icon = QString());

    // 重新绑定 ribbon（插槽或动作变化后调用）。
    void rebuildRibbon();

    // when 求值器：由外壳注入（binder 不认识命令库）。空=所有 when 视为真。
    void setWhenEvaluator(std::function<bool(const QString &)> evaluator);
    // 上下文变化后重算可见页：只有"可见集合"真的变了才重建 ribbon（避免抖动）。
    void applyWhenVisibility();
    // 当前可见的 ribbon 页标签（诊断/测试用，口径与 rebuildRibbon 一致）。
    QStringList visibleRibbonPageLabels() const;

    // 单个 dock 页面的显隐：按 pageKey（面板类型 id）定位该页签。
    // 点击一次出现、再点一次消失，只影响自己，不动同区其它页签。
    // 返回 false 表示该 pageKey 没有对应的 dock（面板尚未创建）。
    bool setDockPageVisible(const QString &pageKey, bool visible);
    bool isDockPageVisible(const QString &pageKey) const;
    // 诊断/状态栏用：pageKey 对应页签的标题（找不到返回空串）。
    QString dockPageTitle(const QString &pageKey) const;

    // --- Activity Bar / Side Bar ---
    // 按插槽表里已声明的 ActivitySpec 重建 Activity Bar（模块声明后调用）。
    void rebuildActivityBar();
    void setActivityBadge(const QString &activityId, int count);
    int activityBadge(const QString &activityId) const;
    bool setActiveActivity(const QString &activityId);
    QString activeActivity() const { return m_activeActivity; }
    QStringList activityIds() const;
    // 把视图拖/命令挂到 Activity Bar：activityId 为空则新建图标。
    // 视图回到 Primary Side Bar，之后点击 Activity 仍能定位它。
    bool pinViewToActivity(const QString &pageKey, const QString &activityId = QString());
    void setActivityOrder(const QStringList &order);
    QStringList activityOrder() const { return m_activityOrder; }
    void setActivityBarVisible(bool visible);
    bool activityBarVisible() const;
    QWidget *activityBarWidget() const;

    // 左栏整栏显隐（Ctrl+B），section 不单独浮动。
    void setSideBarVisible(bool visible);
    bool sideBarVisible() const;
    bool toggleSideBar();
    int sideBarWidth() const;
    void setSideBarWidth(int width);

    int sideBarSectionCount() const;
    QStringList sideBarSectionIds() const;
    QWidget *sideBarSectionWidget(const QString &id) const;
    QString sideBarSectionActivity(const QString &id) const;
    bool setSideBarSectionExpanded(const QString &id, bool expanded);
    bool isSideBarSectionExpanded(const QString &id) const;

    // Activity 点击后的中央面板请求（外壳拿到 ActivitySpec 后按类型请求）。
    // 绑定器不认识具体面板类型，只把声明交回外壳。
    using ActivityActivatedHandler = std::function<void(const nextpilot::plotscope::registry::ActivitySpec &)>;
    void setActivityActivatedHandler(ActivityActivatedHandler handler) { m_activityHandler = std::move(handler); }
    using SettingsHandler = std::function<void()>;
    void setSettingsHandler(SettingsHandler handler) { m_settingsHandler = std::move(handler); }

    // 按 ribbon 页 area 切页（Activity 与外部配置共用）。
    bool setRibbonPage(const QString &area);
    QString currentRibbonPageArea() const;

    // 面板内容与标题（诊断/测试用）。
    QWidget *panelWidget(const QString &area) const;
    // 把面板页签**置前**（"再打开=聚焦"语义：重复 requestPanel 同一面板时调用；
    // 调试暂停时编辑器/模块也会用它把调试面板顶到前台）。
    void raisePanel(QWidget *panel);
    // 按区域+页签标题聚焦（用于启动默认面板：面板装配顺序不该决定初始显示）。
    bool focusPanelByTitle(const QString &area, const QString &title);
    // 聚焦面板并返回是否找到对应页签（诊断/测试用；raisePanel 走它）。
    bool focusPanel(QWidget *panel);
    // 该停靠区**全部**面板（同侧页签化的每个成员；属性/脚本调试等并列时逐个可查）。
    QList<QWidget *> panelWidgets(const QString &area) const;

    // 中央内容（画布区）——不在 dock 体系里，故单独暴露。
    QWidget *centralContent() const { return m_centralContent; }
    // 中央区**全部**页签内容（画布 + 按需创建的脚本编辑器等）。
    QList<QWidget *> centralWidgets() const;
    // 与 centralWidgets() 一一对应的页签标题（报告导出的节标题来源）。
    QStringList centralTitles() const;
    QString panelTitle(const QString &area) const;
    // 该停靠区**全部**页签标题（按放置顺序）——同区多面板时主位概念不成立，
    // 断言/诊断用列表。
    QStringList panelTitles(const QString &area) const;
    // 诊断：该区"页签选中的面板"与"当前真正显示的面板"的标题。
    // 实测出现过二者不一致（tab 高亮『脚本文件』、内容却是『数据源』），
    // 用来在无头测试里把"看起来没切换"钉成可断言的事实。
    QString activePanelTitle(const QString &area) const;
    QString visiblePanelTitle(const QString &area) const;
    // 诊断：停靠区**内容栈**的页序（与 panelTitles 的"添加顺序"对比，
    // 二者不一致就是"点页签切错/不切"的结构性原因）。
    QStringList contentsOrder(const QString &area) const;
    int panelCount() const { return m_panels.size(); }

    // 动作 id → 按钮下拉菜单（由外壳之外提供内容，绑定器只负责挂到按钮上）。
    // 用于「网格」这类"一个大按钮 + 下拉选项"的入口；每次重建都会重新挂。
    using ActionMenuProvider = std::function<QMenu *(const QString &actionId)>;
    void setActionMenuProvider(ActionMenuProvider provider) { m_actionMenuProvider = std::move(provider); }

    // 动作 id → 就地改图标/文字（布局组大按钮在选出 m×n 后回写"实际分格"）。
    void updateActionStyle(const QString &actionId, const QIcon &icon, const QString &text);

    // 动作 id → 触发计数（测试用：验证点击确实路由到意图）。
    int actionTriggerCount(const QString &actionId) const;
    // 按 id 触发 Ribbon 动作（命令面板与 ribbon 共用同一条意图路径）。
    bool triggerAction(const QString &actionId);
    // 无 Ribbon 按钮的动作/纯 op：直接走同一个意图调度器。
    bool dispatchAction(const QString &actionId, const QString &intentOp,
                        const QString &data = QString());

    // 底部 Panel 整组显隐（Ctrl+J）。
    bool bottomPanelVisible() const;
    bool toggleBottomPanel();
    // 右侧 Secondary Side Bar 整组显隐（Ctrl+Alt+B）。
    bool auxZoneVisible() const;
    bool toggleAuxZone();

    // 中央区页签循环与关闭（Ctrl+Tab / Ctrl+W）。
    int currentCentralIndex() const;
    bool activateCentralIndex(int index);
    QString currentCentralTitle() const;
    bool closeCurrentEditor();

    // 首帧后把左右 dock 收窄、中央拿大头（用户之后仍可自由拖宽）。
    void applyDockProportions();

    // 布局持久化（ADS 原生）。
    QByteArray saveLayout() const;
    bool restoreLayout(const QByteArray &state);

    // 停靠区几何快照（窗口坐标系，像素）：editor / left / right / bottom / statusBar。
    // 用于无头回归断言：左右全高、Panel 只挂 Editor 下方、状态栏全宽。
    QJsonObject geometrySnapshot() const;

    // --- 窗口状态快照（声明式）---
    // 捕获：窗口几何 + ADS 布局 + ribbon 当前页签。
    // 这些都是"结果长什么样"的描述，不含操作序列——恢复时缺失的部分安全降级。
    QJsonObject captureState() const;
    bool applyState(const QJsonObject &state);

    // ribbon 页签的恢复必须等窗口真正显示之后再切：构造期 ribbon 页签栏还没
    // 就位，setCurrentIndex 会落空（页签停在第一页），而快照却被写成目标页，
    // 形成"每次启动都记着上一页、却永远回不去"的自锁。
    // 所以 applyState 只登记待恢复页，由外壳在首次 showEvent 里调本方法。
    int pendingRibbonPage() const { return m_pendingRibbonPage; }
    bool applyPendingRibbonPage();

    // 应用 ribbon + 停靠区主题（深浅）。主题状态归 ThemeManager，绑定器只负责
    // 把 SARibbon / ADS 这两类第三方控件的换肤套上去——它是唯一知道它们的地方。
    // dockQss 是 token 化的 ADS 页签条样式，必须每次换肤都重设：ADS 自带
    // default_linux.css 是 palette 驱动的 linux 浅色规则，不随主题走。
    void applyTheme(bool dark, const QString &ribbonQss, const QString &dockQss);

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
    void ensureActivityShell();
    void activateActivity(const QString &activityId);
    QString activityForPanel(const QString &pageKey) const;
    bool isLeftPage(const QString &pageKey) const;
    QList<ads::CDockWidget *> leftDocks() const;
    void syncActivityViews(const QString &activityId);
    void applyViewDockFeatures(ads::CDockWidget *dock);
    void pinDockToActivity(ads::CDockWidget *dock, const QString &activityId);
    QString pageKeyForDock(ads::CDockWidget *dock) const;

    // 装中央画布区。**必须早于任何侧边 dock**——ADS 的 addDockArea() 只在
    // "区域内 dock 数量 <= 1"时按插入参数重设根分割器方向，
    // 于是谁先注册谁决定最终方向：
    //   先中央区(Vertical) → 再侧边区(Horizontal) → 根分割器保持水平，
    //                          得到「左侧树 | 中央画布」；
    //   反过来则根分割器被翻成垂直，树在上、画布在下。
    // installCentral() 在 CDockManager 构造后立刻装中央区。
    void installCentral();

    QMainWindow *m_window = nullptr;
    nextpilot::plotscope::registry::UiSlotHub &m_slots;
    std::function<bool(const QString &)> m_whenEval;
    QStringList m_lastVisiblePages;

    // SARibbon
    SARibbonBar *m_ribbon = nullptr;
    int m_currentPage = 0;   // 当前页签下标（状态快照用）
    int m_pendingRibbonPage = -1; // applyState 登记的待恢复页，-1 表示无

    // ADS
    ads::CDockManager *m_dock = nullptr;
    // ADS 构造时装上的内建样式表（linux 浅色 palette 规则）。换肤时以它为底，
    // 叠 dockQss，避免把 ADS 自己必须的选择器（关闭按钮图标等）一并冲掉。
    QString m_adsBaseQss;
    // ThemeManager 产出的停靠区页签样式。缓存是必需的：换肤信号可能在
    // CDockManager 创建之前就到（MainWindow 构造早期），否则这次换肤被跳过、
    // 页签退回 ADS 的 active 渐变。
    QString m_dockQss;
    ads::CDockWidget *m_centralDock = nullptr; // 中央画布区的 dock
    QWidget *m_centralContent = nullptr;       // 装进中央区的面板
    bool m_centralInstalled = false;

    // Activity Bar + 视图 dock：每个视图一个 CDockWidget，可跨左/右/底停靠。
    QWidget *m_shellFrame = nullptr;
    ActivityBar *m_activityBar = nullptr;
    QStringList m_leftPageKeys; // 初始停靠在左槽的视图 pageKey
    QSet<QString> m_collapsedViews; // 用户收起的视图 pageKey
    QHash<QString, QString> m_viewActivity; // pageKey -> 用户拖到 Activity Bar 后的归属
    QString m_activeActivity;
    QStringList m_activityOrder;
    int m_sideBarWidth = 256;
    bool m_sideBarVisible = true;
    bool m_activityBarVisible = true;
    ActivityActivatedHandler m_activityHandler;
    SettingsHandler m_settingsHandler;
    QHash<QString, ads::CDockAreaWidget *> m_lastArea; // area -> 该侧最后一个 DockArea
    QHash<QString, ads::CDockWidget *> m_panels;      // area -> 该侧主 dock
    QHash<QString, QList<ads::CDockWidget *>> m_docksByArea; // area -> 全部页签（按放置顺序）
    // pageKey（面板类型 id）-> dock 页签：视图页逐页开关按钮的定位表。
    QHash<QString, ads::CDockWidget *> m_dockByKey;
    // 面板内容 widget -> 持有它的 dock 页签（raisePanel 置前用；
    // ADS 自带的 dockWidgetsMap 按 objectName 索引，撞名会互相覆盖不可靠）。
    QHash<QWidget *, ads::CDockWidget *> m_dockByWidget;
    int m_dockSeq = 0;                        // dock objectName 消歧序号

    // 动作
    QHash<QString, int> m_actionTriggers;    // actionId -> 触发次数
    QHash<QString, QAction *> m_actionById;   // actionId -> 已建动作（图标/文字就地更新用）
    ActionMenuProvider m_actionMenuProvider;  // 下拉菜单来源（可为空）

    IntentDispatcher m_dispatch;
};

} // namespace nextpilot::plotscope::gui

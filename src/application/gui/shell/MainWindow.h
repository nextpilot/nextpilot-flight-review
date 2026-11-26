#pragma once

#include "AppContext.h"
#include "CommandPalette.h"
#include "ProductSpec.h"
#include "command/CommandRegistry.h"
#include "command/KeybindingRegistry.h"
#include <QJsonObject>
#include <QHash>
#include <QStringList>

class QLabel;

#include <memory>

// SARibbon（vendored，MIT）：提供**无边框窗体** + ribbon 条。
//
// 为什么 MainWindow 直接继承它：
//   用户明确要求「SARibbon 无边框窗体」。SARibbon 为此提供的正是
//   SARibbonMainWindow——它继承 QMainWindow，并内建：
//     · 无边框（framelesshelper 负责拖拽/缩放/贴边/最小化按钮）；
//     · ribbon 条的宿主（ribbonBar() 即 menuWidget()）；
//     · 窗口按钮组（最小化/最大化/关闭）。
//   自己再继承 QMainWindow 然后手工拼一套无边框逻辑，等于把 SARibbon
//   已经做好的轮子重造一遍——违背「已有的轮子不要重复造」。
//
// 于是外壳与第三方的关系很清晰：
//   MainWindow  继承 SARibbonMainWindow —— 窗体与 ribbon 的宿主
//   ShellBinder 是唯一知道 SARibbon/ADS **绑定规则**的地方
//
// 注意：这只影响 GUI 层。CLI（plotscope）不链接 Qt Widgets，保持纯净。
#include "SARibbonMainWindow.h"

#include <QShowEvent>

namespace nextpilot::plotscope::host
{
class RuntimeHost;
} // namespace nextpilot::plotscope::host

class QEvent;
class QMenu;
class QAction;

namespace nextpilot::plotscope::workbench
{
class IPanelsService;
class IFormatHints;
class IThemeService;
class ISlotsService;
class IViewsService;
class IEditorService;
}


namespace nextpilot::plotscope::gui
{
class KeybindingDialog;
}

namespace nextpilot::plotscope::gui
{

using command::CommandDescriptor;
using command::CommandRegistry;
using command::ContextScope;
using command::KeybindingConflict;
using command::KeybindingRegistry;

class GuiContext;
class PreferencesIntentHandler;
class ShellBinder;
class ThemeManager;

// 主窗口：**只做骨架与装配，不含任何业务**。
//
// 这是「不让 MainWindow 变大」的直接落地：
//   - 窗口骨架（ribbon / dock / 画布区）在这里创建与摆放；
//   - 第三方组件的绑定规则全部在 ShellBinder（唯一知道 SARibbon/ADS 的地方）；
//   - **它不认识任何功能视图**（不 include views/ 下任何头文件）。
//
// 视图之间的协作走 AppContext 的共享状态（星形而非网状）：
//   数据集树被双击 → 记下活动通道 → 绘图视图订阅到变化 → 取数渲染。
//   MainWindow 不参与这条链路。
//
// 装配顺序：
//   骨架 -> 声明标准插槽 -> 装模块（模块可自带界面）-> 绑定 ribbon -> 恢复上次状态。
class MainWindow : public SARibbonMainWindow
{
    Q_OBJECT
public:
    // 无边框窗体：UseRibbonFrame = SARibbon 自绘边框（ framelesshelper 接管
    // 拖拽/缩放/贴边），UseRibbonMenuBar = 用 ribbon 条而不是系统菜单栏。
    // spec：产品身份（标题/默认布局/模块清单）；壳不按产品名分支，全部从 spec 取。
    explicit MainWindow(nextpilot::plotscope::context::AppContext &appCtx,
                        const nextpilot::plotscope::registry::ProductSpec &spec = {},
                        QWidget *parent = nullptr);
    ~MainWindow() override;

    // 启动装配：建骨架、声明插槽、装载模块（GUI 模式）。
    void build(nextpilot::plotscope::host::RuntimeHost &host);

    // 状态持久化（声明式快照）：
    //   - 保存：关窗时把窗口几何 + ADS 布局 + ribbon 页签写入 Workspace 作用域；
    //   - 恢复：开窗时读回并应用；缺失的部分安全降级（不影响启动）。
    void saveState();
    void restoreState();
    bool hasSavedState() const;

    // 诊断/测试用。
    ShellBinder *binder() const { return m_binder; }
    GuiContext *context() const { return m_context; }
    ThemeManager *themeManager() const { return m_theme; }
    CommandRegistry *commandRegistry() const { return m_commands.get(); }
    KeybindingRegistry *keybindingRegistry() const { return m_keybindings.get(); }
    CommandPalette *commandPalette() const { return m_commandPalette; }
    // 诊断/测试：启动期面板引用校验未命中清单（空 = 全部命中工厂表）。
    // 三源：包 PanelSpec / Activity panelTypes+centralPanel / 产品默认布局。
    QStringList panelReferenceIssues() const { return m_panelReferenceIssues; }

    // 命令面板/快捷键（测试与 UI 驱动共用）。
    void showCommandPalette();
    void showQuickSwitch();
    bool executeCommand(const QString &id, QString *error = nullptr);
    void openKeybindingDialog();

private:
    // 声明 app 骨架提供的标准插槽（ribbon 页签/分组、停靠区、菜单）。
    void declareStandardSlots();
    void declareViewToggleSlots();

    // 打开数据对话框已归 data 模块呈现层（D18a/A2a）：由模块自注册启动器
    // （DialogLauncherHub）处理，外壳按 id 触发、不 include 业务对话框。

    // 来源库项：按指定格式驱动直接弹文件对话框（跳过格式选择步骤），
    // 选中的文件逐条走 data.load 意图——与通用导入、CLI/脚本同一条加载路径。
    void openDataWithFormat(nextpilot::plotscope::host::RuntimeHost &host, const QString &formatId);

    // 工作区预设：保存 = 输入名字后把源清单与画布布局写进 .psw；
    // 打开 = 列出现有预设选一个，按名整体重建。两步都落到 mod.workspace 的意图。
    void saveWorkspacePreset(nextpilot::plotscope::host::RuntimeHost &host);
    void openWorkspacePreset(nextpilot::plotscope::host::RuntimeHost &host);

    // 「网格」大按钮的下拉菜单：每次展开前按当前画布状态重建勾选项。
    void rebuildGridMenu();

    // 窗口首次显示后：按真实宽度重排 ribbon。
    void showEvent(QShowEvent *event) override;

    // Workspace 里的工作台 UI 配置（Activity 顺序/显隐、侧栏显隐/宽度）。
    void applyWorkspaceUiConfig();
    void saveWorkspaceUiConfig();
    void refreshActivityBadges();
    // 把包 token + 模块 onAttach 声明的语义色合并灌进主题（验收 8）。
    void refreshThemeTokens();
    void toggleSideBar();
    // 退出钩子：把当前现场（源清单 + 画布布局 + 各面板会话态）写成会话快照。
    // 落盘失败只提示不阻断——退出路径不该因为磁盘问题卡住用户。
    void closeEvent(QCloseEvent *event) override;

    // 会话快照的写与读。config 段（sources+plots）是恢复数据现场的唯一依据，
    // session 段是各面板自述的运行态；两段各自降级，互不牵连。
    void writeSessionSnapshot();
    void restoreSessionSnapshot();

    // 命令层：统一命令 + Palette + keybinding。
    void buildCommandLayer(nextpilot::plotscope::host::RuntimeHost &host);
    void registerSlotCommands();
    // 状态栏（验收 7 / D8 推模式）：订阅 GuiContext 的信号，按 side/priority 排布 QLabel。
    void setupStatusBar();
    void onStatusSpecDeclared(const registry::StatusSpec &spec);
    void onStatusTextChanged(const QString &id, const QString &text);
    void onStatusFlashed(const QString &text);
    void rebuildStatusBarLayout();

    void registerShellCommands();
    // 命令执行回调（D7）：把 CommandDescriptor 的纯数据载荷派发到 GUI 动作——
    // 先试本地 Ribbon trigger，再按 intentOp 走意图执行器。命令层本身不含代码。
    bool executeCommand(const command::CommandDescriptor &command, QString *error);
    // shell 命令 op → GUI 动作分派（壳的 UI 实现，与 PreferencesIntentHandler 同类）。
    bool runShellAction(const QString &op, QString *error);
    void updateCommandContext();
    QList<CommandPalette::Item> buildCommandItems();
    QList<CommandPalette::Item> buildQuickItems() const;
    void rebuildKeybindingActions();
    void applyKeybindingConfig();
    void saveKeybindingConfig();

    bool eventFilter(QObject *watched, QEvent *event) override;

    nextpilot::plotscope::context::AppContext &m_appCtx;
    // 产品身份快照：装配层注入，壳只读；不含任何产品分支判断。
    const nextpilot::plotscope::registry::ProductSpec m_spec;
    nextpilot::plotscope::host::RuntimeHost *m_host = nullptr; // 用于取设置（状态持久化）
    ShellBinder *m_binder = nullptr;
    QMenu *m_gridMenu = nullptr;   // 「网格」大按钮的下拉（X/Y 网格线开关）
    std::unique_ptr<CommandRegistry> m_commands;
    std::unique_ptr<KeybindingRegistry> m_keybindings;
    CommandPalette *m_commandPalette = nullptr;
    QList<QAction *> m_keybindingActions;
    QString m_pendingChordCommand;
    int m_chordToken = 0;
    GuiContext *m_context = nullptr;
    // 状态栏项控件（验收 7）：id → QLabel，按 StatusSpec.side/priority 排布。
    QHash<QString, QLabel *> m_statusLabels;
    // 启动期面板引用校验未命中清单（诊断/测试回读；空 = 全部命中工厂表）。
    QStringList m_panelReferenceIssues;
    ThemeManager *m_theme = nullptr;   // 系统级主题管理，生命周期归宿主
    // 壳读回服务（A8b-M2 裁定 52441633 §三）：包面板声明/格式提示/扩展主题 token
    // 都经 workbench 接口读回，MainWindow 不直连扩展注册表。借用指针，归装配层。
    const nextpilot::plotscope::workbench::IPanelsService *m_panelsService = nullptr;
    const nextpilot::plotscope::workbench::IFormatHints *m_formatHints = nullptr;
    const nextpilot::plotscope::workbench::IThemeService *m_themeService = nullptr;
    // 槽位表服务（裁定 52442006 Step 4）：壳读写唯一通道；未装配（bare 壳）为 nullptr。
    nextpilot::plotscope::workbench::ISlotsService *m_slotsService = nullptr;
    // 视图/编辑器打开服务（A2b 笔2b 复审必修 A）：构造 GuiContext 时注入，
    // openView/openEditor 的执行体；借用指针，归装配层，未装配为 nullptr。
    nextpilot::plotscope::workbench::IViewsService *m_viewsService = nullptr;
    nextpilot::plotscope::workbench::IEditorService *m_editorService = nullptr;
    // 系统级 preferences.open handler（打开全局偏好对话框）；随窗口存亡。
    PreferencesIntentHandler *m_preferencesHandler = nullptr;
    bool m_sessionRestored = false;   // 会话恢复只跑一次（首次显示后）
    bool m_ribbonLaidOut = false; // 是否已按真实宽度重建过 ribbon
};

} // namespace nextpilot::plotscope::gui

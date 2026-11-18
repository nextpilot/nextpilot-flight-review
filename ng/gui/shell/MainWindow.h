#pragma once

#include <QJsonObject>

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
// 注意：这只影响 GUI 层。CLI（plotscope-ng）不链接 Qt Widgets，保持纯净。
#include "SARibbonMainWindow.h"

#include <QShowEvent>

namespace ng::core
{
class AppContext;
} // namespace ng::core

namespace ng::host
{
class ModuleHost;
} // namespace ng::host

namespace ng::gui
{

class GuiContext;
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
//   骨架 -> 声明标准插槽 -> 装器官（器官可自带界面）-> 绑定 ribbon -> 恢复上次状态。
class MainWindow : public SARibbonMainWindow
{
    Q_OBJECT
public:
    // 无边框窗体：UseRibbonFrame = SARibbon 自绘边框（ framelesshelper 接管
    // 拖拽/缩放/贴边），UseRibbonMenuBar = 用 ribbon 条而不是系统菜单栏。
    explicit MainWindow(ng::core::AppContext &appCtx, QWidget *parent = nullptr);
    ~MainWindow() override;

    // 启动装配：建骨架、声明插槽、装载模块（GUI 模式）。
    void build(ng::host::ModuleHost &host);

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

private:
    // 声明 app 骨架提供的标准插槽（ribbon 页签/分组、停靠区、菜单）。
    void declareStandardSlots();

    // 骨架自带的标准动作"打开数据"：选文件 -> 走 data.load 意图。
    //
    // 为什么它在骨架而不是数据模块：
    //   - data.load 本来就是**系统级**意图（不归属任何可卸载模块），
    //     放在骨架里与意图的归属一致；
    //   - "选文件"是 GUI 交互能力，只有外壳有 QWidget 与 QFileDialog。
    // 加载完成后依然走意图层，于是 GUI 与 CLI/脚本行为一致。
    void openDataFile(ng::host::ModuleHost &host);

    // 骨架自带的"协议检查器"入口：按 IMessageParser 自述列协议、配参数、逐帧看字段。
    // 纯 GUI 检查能力（产 DecodedFrame，不入 Dataset），故按 id 特判弹对话框，
    // 与"打开数据"同级处理；不占用任何可卸载模块。
    void openProtocolInspector();

    // 窗口首次显示后：按真实宽度重排 ribbon。
    //
    // SARibbon 的面板是按 ribbon 的**当时宽度**算几何的。窗口显示前，
    // ribbon 还挂在 QMainWindow 的 menuWidget 槽里没被布局，宽度是
    // QWidget 默认的 100px——此时建好的 ribbon 内容只能挤在 100px 内，
    // 窗口拉宽后也不会自动重排（SARibbon 只在窗口激活时重排）。
    // 所以内容要在窗口布局完成之后再建一次。
    void showEvent(QShowEvent *event) override;

    ng::core::AppContext &m_appCtx;
    ng::host::ModuleHost *m_host = nullptr; // 用于取设置（状态持久化）
    ShellBinder *m_binder = nullptr;
    GuiContext *m_context = nullptr;
    ThemeManager *m_theme = nullptr;   // 系统级主题管理，生命周期归宿主
    bool m_ribbonLaidOut = false; // 是否已按真实宽度重建过 ribbon
};

} // namespace ng::gui

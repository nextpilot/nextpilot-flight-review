#pragma once

#include <QJsonObject>
#include <QMainWindow>

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
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
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

private:
    // 声明 app 骨架提供的标准插槽（ribbon 页签/分组、停靠区、菜单）。
    void declareStandardSlots();

    ng::core::AppContext &m_appCtx;
    ng::host::ModuleHost *m_host = nullptr; // 用于取设置（状态持久化）
    ShellBinder *m_binder = nullptr;
    GuiContext *m_context = nullptr;
};

} // namespace ng::gui

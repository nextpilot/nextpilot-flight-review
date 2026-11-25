#pragma once

// DialogLauncherHub —— 模态对话框启动器表（D18a）。
//
// 外壳与"业务模态对话框"之间的解耦点，与 PanelFactoryHub 同构：
//   外壳只知道一个动作 id（如 "app.action.protocolEditor"），不 include、不构造
//   任何业务对话框；对话框的呈现层在自己的 TU 里按 id 注册一个启动器。
//
// 为什么需要这张表：
//   - 壳（ps_gui / 裸壳）不许链业务模块（ShellLinkGuard / 验收 6）。对话框实现归模块后，
//     壳若直接构造就会反向依赖模块，裸壳更会断链。
//   - 模态对话框需要 QWidget* parent（模态归属）、AppContext（数据/工具箱）与意图执行器。
//     启动器签名把这三者作为参数由外壳传入，模块不持有外壳指针。
//   - 未注册（裸壳 / headless 装配没链该模块）时 launch 为 no-op，壳照常工作。
//
// 本表是纯数据（std::function + 前向声明），不 include Qt Widgets，
// 放 libs/registry 供外壳与模块呈现层双向可达。

#include "IIntentExecutor.h"
#include "StaticAutoRegistry.h"

#include <QString>

class QWidget;

namespace nextpilot::plotscope::context
{
class AppContext;
}

namespace nextpilot::plotscope::registry
{

struct DialogLauncherEntry
{
    using Launcher = std::function<void(QWidget *parent, nextpilot::plotscope::context::AppContext &appCtx,
                                        nextpilot::plotscope::intent::IIntentExecutor &executor)>;

    Launcher fn;
    bool valid() const { return fn != nullptr; }
};

class DialogLauncherHub : public StaticHub<DialogLauncherEntry>
{
public:
    using Launcher = DialogLauncherEntry::Launcher;

    static void registerLauncher(const QString &id, Launcher launcher)
    {
        insert(id, DialogLauncherEntry{std::move(launcher)});
    }

    // 按 id 启动；未注册 no-op（裸壳/headless 没链对应模块时安全）。
    static void launch(const QString &id, QWidget *parent,
                       nextpilot::plotscope::context::AppContext &appCtx,
                       nextpilot::plotscope::intent::IIntentExecutor &executor)
    {
        if (const DialogLauncherEntry *e = find(id))
            e->fn(parent, appCtx, executor);
    }

    static bool has(const QString &id) { return find(id) != nullptr; }
};

// 注册宏：模块呈现层 TU 末尾一行。
//   PS_REGISTER_DIALOG_LAUNCHER("app.action.protocolEditor", protocolEditorLauncher, var)
// Fn 签名：void(QWidget *, AppContext &, IIntentExecutor &)。
#define PS_REGISTER_DIALOG_LAUNCHER(Id, VarName, Fn)                                       \
    namespace                                                                              \
    {                                                                                      \
    const bool g_ps_dialog_launch_##VarName = [] {                                         \
        ::nextpilot::plotscope::registry::DialogLauncherHub::registerLauncher(             \
            QStringLiteral(Id), &Fn);                                                      \
        return true;                                                                       \
    }();                                                                                   \
    }

} // namespace nextpilot::plotscope::registry

#pragma once

// A8b-M1 扩展激活服务：把扩展的 contributes 命令声明落地到工作台命令面板。
// VSCode 对位 = ExtensionActivationService（激活扩展 → 注册命令/菜单/视图），
// 不是自造的 "Binder" 概念。位于装配层（application/assembly）——壳目录不放置
// 任何"装配贡献"的类，否则 A2b 的"壳零业务"边界会含混（PR147 审核 52439061 §4）。
//
// 设计约束：
//   - MainWindow.cpp 零 diff：本类从装配层拿 CommandRegistry* 与 RuntimeHost&，
//     不改 MainWindow 源码。
//   - 命令执行走既有唯一链：CommandDescriptor.intentOp = command id →
//     RuntimeHost::execute → 本类注册的单一 handler canHandle 认领 → 记录触发 + 成功。
//   - 不解释 id、不按前缀分支；unmount(owner) 从命令表整批撤，无悬空。

#include "ToolboxHub.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace nextpilot::plotscope::command
{
class CommandRegistry;
}
namespace nextpilot::plotscope::host
{
class RuntimeHost;
}
namespace nextpilot::plotscope::intent
{
class IIntentHandler;
}

namespace nextpilot::plotscope::assembly
{

class ExtensionActivationService
{
public:
    explicit ExtensionActivationService(const registry::ToolboxHub &extensions);

    // 把当前贡献命令灌进命令表 + 在 RuntimeHost 注册单一执行 handler。
    // 幂等：重复 bind 只补尚未登记的命令，不重复注册 handler。
    void bind(command::CommandRegistry *commands, host::RuntimeHost &runtime);

    // 按 owner 整批从命令表撤（handler 常驻但 canHandle 只认领当前仍存在的命令）。
    void unbind(const QString &owner, command::CommandRegistry *commands);

    // 某命令被面板触发的次数（诊断/e2e 断言）。
    int triggeredCount(const QString &commandId) const;
    // 已注入命令表的贡献命令数。
    int boundCommandCount() const { return m_bound.size(); }

private:
    const registry::ToolboxHub &m_extensions;
    class ContributedCommandHandler;
    ContributedCommandHandler *m_handler = nullptr; // 归本服务，QObject 子对象
    QHash<QString, QString> m_bound;                // command id -> owner
};

} // namespace nextpilot::plotscope::assembly

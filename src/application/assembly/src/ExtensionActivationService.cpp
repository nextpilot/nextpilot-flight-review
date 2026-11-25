#include "ExtensionActivationService.h"

#include "command/CommandRegistry.h"
#include "RuntimeHost.h"
#include "Intent.h"

#include <QPointer>

using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::command;

namespace nextpilot::plotscope::assembly
{

// 单一执行 handler：认领所有当前仍挂载的贡献命令，记录触发次数并返回成功。
// 常驻注册一次即可；unmount 后命令从扩展表消失，canHandle 自然返回 false，无悬空。
class ExtensionActivationService::ContributedCommandHandler final : public QObject,
                                                                    public IIntentHandler
{
public:
    explicit ContributedCommandHandler(const registry::ToolboxHub &extensions, QObject *parent = nullptr)
        : QObject(parent), m_extensions(extensions) {}

    bool canHandle(const Intent &intent) const override
    {
        return !intent.op.isEmpty() && m_extensions.findCommand(intent.op) != nullptr;
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        m_triggered[intent.op] += 1;
        IntentResult out;
        out.payload = intent.op; // 回显命令 id，e2e 可断言
        return Result<IntentResult>(out);
    }

    int triggeredCount(const QString &id) const { return m_triggered.value(id, 0); }

private:
    const registry::ToolboxHub &m_extensions;
    QHash<QString, int> m_triggered;
};

ExtensionActivationService::ExtensionActivationService(const registry::ToolboxHub &extensions)
    : m_extensions(extensions)
{
}

void ExtensionActivationService::bind(CommandRegistry *commands, host::RuntimeHost &runtime)
{
    if (!commands)
        return;

    if (!m_handler)
    {
        // QObject 父给 runtime（生命周期随宿主），但 handler 只借用扩展表引用——
        // 两者都在 App 生命周期内存活，本服务先于二者析构即可。
        m_handler = new ContributedCommandHandler(m_extensions, &runtime);
        runtime.registerSystemHandler(QStringLiteral("@system/extensions"), m_handler);
    }

    for (const auto &c : m_extensions.contributedCommands())
    {
        if (m_bound.contains(c.command))
            continue; // 幂等
        CommandDescriptor d;
        d.id = c.command;
        d.title = c.title.isEmpty() ? c.command : c.title;
        d.category = c.category;
        d.icon = c.icon;
        d.provider = m_extensions.ownerOfCommand(c.command);
        d.intentOp = c.command; // 执行走 RuntimeHost → 本服务 handler
        if (commands->add(d))
            m_bound.insert(c.command, d.provider);
    }
}

void ExtensionActivationService::unbind(const QString &owner, CommandRegistry *commands)
{
    if (!commands)
        return;
    // 用 bind 时记录的 owner 归属撤，不依赖扩展表当前状态（可能已 unmount）。
    for (auto it = m_bound.begin(); it != m_bound.end();)
    {
        if (it.value() == owner)
        {
            commands->remove(it.key());
            it = m_bound.erase(it);
        }
        else
            ++it;
    }
}

int ExtensionActivationService::triggeredCount(const QString &commandId) const
{
    return m_handler ? m_handler->triggeredCount(commandId) : 0;
}

} // namespace nextpilot::plotscope::assembly

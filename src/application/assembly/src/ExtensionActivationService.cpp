#include "ExtensionActivationService.h"

#include "command/CommandRegistry.h"
#include "RuntimeHost.h"
#include "Intent.h"

#include <QPointer>

using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::command;
using namespace nextpilot::plotscope::registry;

namespace nextpilot::plotscope::assembly
{
namespace wb = nextpilot::plotscope::workbench;
namespace registry = nextpilot::plotscope::registry;

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
        if (intent.op.isEmpty())
            return false;
        // workbench.* 保留前缀归工作台内置 handler（内置命令
        // workbench.action.openSettings 等有自己的执行体），扩展 handler 不抢。
        if (intent.op.startsWith(QLatin1String("workbench.")))
            return false;
        return m_extensions.findCommand(intent.op) != nullptr;
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

ExtensionActivationService::ExtensionActivationService(nextpilot::plotscope::context::AppContext &appCtx)
    : m_extensions(appCtx.toolbox()) // 注册表获取留在装配层（裁定 52441633 §四）
{
}

void ExtensionActivationService::attachViewRegistry(host::RuntimeHost &runtime)
{
    // App 构造先注册 views.registry 再 bind（时序由组合根保证）；取不到保持 nullptr。
    if (auto *services = runtime.services())
        m_views =
            services->getService<registry::ViewRegistry>(QStringLiteral("views.registry"));
}

void ExtensionActivationService::bind(CommandRegistry *commands, host::RuntimeHost &runtime)
{
    if (!commands)
        return;

    attachViewRegistry(runtime);
    // 扩展包的 viewsContainers/views（Step 5）：mount 时登记在唯一扩展注册表，
    // 这里按 owner 灌进 ViewRegistry——模块 contributes 与扩展包 manifest 汇同一张表。
    if (m_views)
    {
        for (const auto &owned : m_extensions.contributedViewContainers())
        {
            registry::ContributesDoc doc;
            doc.owner = owned.owner;
            doc.viewContainers.append(owned.item);
            m_views->declare(doc);
        }
        for (const auto &owned : m_extensions.contributedViews())
        {
            registry::ContributesDoc doc;
            doc.owner = owned.owner;
            doc.views.append(owned.item);
            m_views->declare(doc);
        }
        for (const auto &owned : m_extensions.contributedCustomEditors())
        {
            registry::ContributesDoc doc;
            doc.owner = owned.owner;
            doc.customEditors.append(owned.item);
            m_views->declare(doc);
        }
    }

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
    if (m_views)
        m_views->undeclare(owner);
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

// ---- ContributionsInstaller（裁定 52442006 Step 3）：灌回唯一实现。
// 声明式灌回：与原 IUiContext::installContributions 落点完全一致，只是写入方
// 从壳（GuiContext + UiSlotHub 引用）换成装配层（经 ISlotsService 写、壳只渲染）。
// owner 用模块 id（裁定 §4 isBuiltin），不再写死 "module"。
void ContributionsInstaller::install(const registry::ModuleContributions &c)
{
    // contributes.viewsContainers / views（Step 5）：声明灌 ViewRegistry，壳经
    // IViewsService 读回——壳不认识视图数据来源（模块 contributes / 扩展包 manifest
    // 都汇到同一张表）。
    if (!c.viewContainers.isEmpty() || !c.views.isEmpty() || !c.customEditors.isEmpty())
    {
        registry::ContributesDoc doc;
        doc.owner = c.owner.isEmpty() ? QStringLiteral("module") : c.owner;
        doc.viewContainers = c.viewContainers;
        doc.views = c.views;
        doc.customEditors = c.customEditors;
        m_views.declare(doc);
    }

    const QString owner = c.owner.isEmpty() ? QStringLiteral("module") : c.owner;

    for (const RibbonPageDecl &p : c.ribbonPages)
        m_slots.declareSlot(p.id, p.label, owner, registry::SlotKind::RibbonPage, QString(), p.order);
    for (const RibbonGroupDecl &g : c.ribbonGroups)
        m_slots.declareSlot(g.id, g.label, owner, registry::SlotKind::RibbonGroup, g.pageId, g.order);
    for (const ActivitySpec &a : c.activities)
    {
        ActivitySpec spec = a;
        if (spec.provider.isEmpty())
            spec.provider = owner;
        m_slots.registerActivity(spec);
    }
    for (const UiAction &action : c.actions)
        m_slots.bindLocal(action.slotId, action, owner);
    for (const ThemeTokenSpec &t : c.themeTokens)
    {
        ThemeTokenSpec spec = t;
        if (spec.provider.isEmpty())
            spec.provider = owner;
        m_slots.registerModuleThemeToken(spec); // 同 id 先到者为准（去重在 hub 内）
    }
    for (const StatusSpec &s : c.statuses)
        m_appCtx.status().registerSpec(s);

    // 动态动作提供器（裁定 §2）：provider 对象注册，运行时条目懒求值，
    // 变化由 provider 的订阅通知外壳只重建该 slot（不经壳写壳读的隐藏通道）。
    for (const ActionProviderDecl &p : c.actionProviders)
    {
        if (!p.provider)
            continue;
        m_slots.bindActionProvider(p.slotId, p.owner,
                                   [p]() { return p.provider->actionsFor(p.slotId); });
    }
}

} // namespace nextpilot::plotscope::assembly

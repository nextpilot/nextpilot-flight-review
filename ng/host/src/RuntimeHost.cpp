#include "RuntimeHost.h"
#include "SkeletonSlots.h"

#include <QSet>

namespace nextpilot::plotscope::host
{

void ModuleContext::registerIntentHandler(nextpilot::plotscope::intent::IIntentHandler *handler)
{
    // 依赖具体宿主实现：通过 host 转发注册。
    m_host.registerIntentHandler(handler);
}

nextpilot::plotscope::intent::IIntentExecutor &ModuleContext::executor()
{
    return m_host; // RuntimeHost 实现了 IIntentExecutor
}

nextpilot::plotscope::intent::JobRecorder *ModuleContext::recorder()
{
    return m_host.recorder();
}

RuntimeHost::RuntimeHost(nextpilot::plotscope::context::AppContext &appCtx, QObject *parent)
    : QObject(parent), m_appCtx(appCtx),
      // data.load 走统一摄取管道（读字节 -> 扇出 -> 落库）：
      // 格式驱动只声明 parserId，解析实现由 SPI 解析器提供。
      m_dataLoadHandler(appCtx.toolbox(), appCtx.data(), appCtx.sources(),
                         &appCtx.ingest(), &appCtx.settings()),
      m_dataCommandsHandler(appCtx, *this),
      m_aiToolsHandler(appCtx, *this, [this]() { return registeredOps(); })
{
    // 系统级 data.load 入口：不归属任何可卸载模块，任何模块装卸都不影响它。
    // 用一个保留 key 注册，避免与模块 handler 的 "%id/%index" 冲突。
    m_handlers.insert(QStringLiteral("@system/data.load"), &m_dataLoadHandler);

    // data 派生能力（reload/export/recent/clearCache）与 load 同属系统级，
    // 归口一处——所有 data.* 意图都在宿主，模块装卸不影响，也不建第二条路。
    m_handlers.insert(QStringLiteral("@system/data.commands"), &m_dataCommandsHandler);

    // AI/MCP 工具层：ai.* 系统级 handler，暴露工具目录与调用入口，供后续 MCP bridge 接。
    m_handlers.insert(QStringLiteral("@system/ai.tools"), &m_aiToolsHandler);

    // app 骨架的标准 UI 插槽（容器）在**唯一一处**声明：SkeletonSlots（host 层）。
    // 骨架槽清单是 app 的配置，不写死在 core——core 只有 declareSlot 机制。
    // headless 下这些槽也是纯数据，不创建 QWidget。
    declareSkeletonSlots(m_uiSlotRegistry);
}

RuntimeHost::~RuntimeHost()
{
    deactivateAll();
}

void RuntimeHost::registerModule(ModuleInfo info, ModuleFactory factory)
{
    Node node;
    node.reg = ModuleRegistration{info, std::move(factory)};
    m_nodes.insert(info.id, node);
}

void RuntimeHost::registerIntentHandler(nextpilot::plotscope::intent::IIntentHandler *handler)
{
    if (!handler || m_initializing.isEmpty())
        return;
    // 归属到当前正在初始化的模块；一个 handler 可处理多个 op，
    // 执行时遍历所有 handler 询问 canHandle，无需按 op 建索引。
    const QString key = QStringLiteral("%1/%2").arg(m_initializing).arg(m_handlers.size());
    m_handlers.insert(key, handler);
    m_nodes[m_initializing].handlers.append(handler);
}

void RuntimeHost::registerSystemHandler(const QString &key, nextpilot::plotscope::intent::IIntentHandler *handler)
{
    // 系统级：不经 m_initializing、不计入任何模块的 handlers，模块装卸不移除它。
    if (!handler || key.isEmpty())
        return;
    m_handlers.insert(key, handler);
}

QHash<QString, QString> RuntimeHost::activate(const QStringList &disabledIds, UiContext *ui)
{
    QHash<QString, QString> errors;

    // 构建能力 -> 模块 id 映射，供依赖解析。
    QHash<QString, QStringList> provides;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it)
    {
        if (disabledIds.contains(it.key()))
            continue;
        for (const QString &cap : it.value().reg.info.provides)
            provides[cap].append(it.key());
    }

    m_order = resolveOrder(provides, errors);

    ModuleContext ctx(m_appCtx, *this);
    for (const QString &id : m_order)
    {
        Node &node = m_nodes[id];
        if (!node.reg.factory)
        {
            errors.insert(id, QStringLiteral("工厂为空"));
            continue;
        }
        IModule *module = node.reg.factory();
        if (!module)
        {
            errors.insert(id, QStringLiteral("工厂返回空"));
            continue;
        }
        module->setParent(this); // 所有权归宿主，delete 时整棵子树回收
        node.instance = module;
        m_initializing = id;
        module->initialize(ctx);
        m_initializing.clear();
        node.active = true;
        if (ui)
            module->onAttach(*ui); // headless 时 ui 为空，不建 UI
    }
    return errors;
}

bool RuntimeHost::deactivate(const QString &moduleId)
{
    auto it = m_nodes.find(moduleId);
    if (it == m_nodes.end() || !it->instance)
        return false;

    // 先移除该模块注册的意图处理器，避免执行器回调已析构的对象。
    for (nextpilot::plotscope::intent::IIntentHandler *h : it->handlers)
    {
        for (auto hit = m_handlers.begin(); hit != m_handlers.end(); ++hit)
            if (hit.value() == h)
            {
                m_handlers.erase(hit);
                break;
            }
    }
    it->handlers.clear();

    it->instance->onDetach();
    delete it->instance; // QObject 子树自动销毁其创建的 UI 与信号连接
    it->instance = nullptr;
    it->active = false;
    return true;
}

void RuntimeHost::deactivateAll()
{
    const QStringList ids = activeIds();
    for (const QString &id : ids)
        deactivate(id);
    // 只清模块注册的 handler；系统级 data.load 入口保留。
    for (auto it = m_handlers.begin(); it != m_handlers.end(); )
        if (it.key().startsWith(QStringLiteral("@system/")))
            ++it;
        else
            it = m_handlers.erase(it);
}

const nextpilot::plotscope::registry::UiSlotRegistry &RuntimeHost::uiSlots()
{
    // 每次访问都从当前工具箱重新绑定，装卸知识包后立即反映，无需手动同步。
    m_uiSlotRegistry.refreshFrom(m_appCtx.toolbox());
    return m_uiSlotRegistry;
}

nextpilot::plotscope::registry::UiSlotRegistry &RuntimeHost::uiSlotsMutable()
{
    m_uiSlotRegistry.refreshFrom(m_appCtx.toolbox());
    return m_uiSlotRegistry;
}

nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> RuntimeHost::execute(const nextpilot::plotscope::intent::Intent &intent)
{
    for (auto it = m_handlers.constBegin(); it != m_handlers.constEnd(); ++it)
    {
        nextpilot::plotscope::intent::IIntentHandler *handler = it.value();
        if (handler && handler->canHandle(intent))
            return handler->execute(intent);
    }
    return nextpilot::plotscope::foundation::fail(QStringLiteral("eunsupported"),
                          QStringLiteral("没有模块能处理意图: %1").arg(intent.op));
}

bool RuntimeHost::isActive(const QString &moduleId) const
{
    const auto it = m_nodes.constFind(moduleId);
    return it != m_nodes.constEnd() && it->active;
}

QStringList RuntimeHost::activeIds() const
{
    QStringList ids;
    for (const QString &id : m_order)
        if (isActive(id))
            ids << id;
    return ids;
}

QList<ModuleInfo> RuntimeHost::availableModules() const
{
    QList<ModuleInfo> infos;
    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it)
        infos << it.value().reg.info;
    return infos;
}

QList<QString> RuntimeHost::resolveOrder(const QHash<QString, QStringList> &provides,
                                        QHash<QString, QString> &errors) const
{
    // Kahn 拓扑排序：节点是模块 id，边由 requires -> provides 产生。
    QHash<QString, int> indegree;
    QHash<QString, QStringList> edges; // provider -> consumers

    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it)
        if (!errors.contains(it.key()))
        {
            indegree[it.key()] = 0;
            edges[it.key()] = {};
        }

    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it)
    {
        const QString consumer = it.key();
        if (errors.contains(consumer))
            continue;
        for (const QString &need : it.value().reg.info.requires)
        {
            const QStringList candidates = provides.value(need);
            if (candidates.isEmpty())
            {
                errors.insert(consumer, QStringLiteral("缺少能力: %1").arg(need));
                continue;
            }
            // 取第一个提供者的边。
            const QString provider = candidates.first();
            if (provider != consumer)
            {
                edges[provider].append(consumer);
                indegree[consumer] = indegree.value(consumer) + 1;
            }
        }
    }

    QList<QString> order;
    QList<QString> queue;
    for (auto it = indegree.constBegin(); it != indegree.constEnd(); ++it)
        if (it.value() == 0)
            queue.append(it.key());

    while (!queue.isEmpty())
    {
        const QString cur = queue.takeFirst();
        order.append(cur);
        for (const QString &next : edges.value(cur))
        {
            indegree[next] = indegree.value(next) - 1;
            if (indegree[next] == 0)
                queue.append(next);
        }
    }

    // 剩余的即有环或依赖被错误模块阻塞。
    for (auto it = indegree.constBegin(); it != indegree.constEnd(); ++it)
        if (it.value() > 0 && !errors.contains(it.key()))
            errors.insert(it.key(), QStringLiteral("依赖成环或依赖缺失"));

    return order;
}

} // namespace nextpilot::plotscope::host

#include "RuntimeHost.h"
#include "IContributionsInstaller.h"
#include "ErrorCodes.h"
#include "RegistryNode.h"
#include "RegistryTree.h"
#include "ShellContributions.h"
#include "StaticRegistryNodes.h"

#include <QSet>

namespace nextpilot::plotscope::host
{
namespace err = nextpilot::plotscope::foundation::err;

RuntimeHost::RuntimeHost(nextpilot::plotscope::context::AppContext &appCtx, QObject *parent)
    : QObject(parent), m_appCtx(appCtx),
      // UI 插槽注册表构造即上树。
      m_uiSlotHub(appCtx.registryTree()),
      // data.load 走统一摄取管道（读字节 -> 扇出 -> 落库）：
      // 格式驱动只声明 parserId，解析实现由 SPI 解析器提供。
      m_dataLoadHandler(appCtx, appCtx.toolbox(), appCtx.data(), appCtx.sources(),
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

    // 壳级 Ribbon 页/组在**唯一一处**声明：ShellContributions（host 层）。
    // 业务页/组由各模块 onAttach 自声明；区域是壳代码常量（Region 枚举），不进注册表。
    // headless 下这些槽也是纯数据，不创建 QWidget。
    declareShellContributions(m_uiSlotHub);

    // M2.8 R4：static 注册表的节点宿主在装配期显式构造上树（QPointer 弱引用，
    // 析构自动摘除；节点实例归 RuntimeHost 父子树——树不托管 static 节点生命周期，
    // 红线 4；libs 禁 Meyers 单例，节点宿主按契约放 host 层）。
    m_panelFactoryNode = new PanelFactoryNode(m_appCtx.registryTree(), this);
    m_builtinModuleNode = new BuiltinModuleNode(m_appCtx.registryTree(), this);
    // M2.8 遗留 4 收口：Parser / RecordingFormat / RecordingBackend 三张存量静态表
    // 全量上树（与 R4 两节点同一装配路径，diag.registries 无盲区）。
    m_parserNode = new ParserNode(m_appCtx.registryTree(), this);
    m_recordingFormatNode = new RecordingFormatNode(m_appCtx.registryTree(), this);
    m_recordingBackendNode = new RecordingBackendNode(m_appCtx.registryTree(), this);

    // M2.8 diag.registries：全系统注册树的全景 dump（每节点一项）。
    // 这是整改的核心验收物——整改前不存在任何途径可以获得这张清单。
    // 数据来自组合根树（appCtx.registryTree()），GUI/headless/脚本同一条路：
    // 意图通道 eunsupported 时 app.call 回退 view action（ScriptFacade）。
    m_appCtx.interactions().registerViewAction(
        QStringLiteral("diag.registries"),
        [this](const QVariantMap &) -> nextpilot::plotscope::foundation::Result<QVariant>
        {
            QVariantList list;
            for (nextpilot::plotscope::registry::RegistryNode *node :
                 m_appCtx.registryTree().nodes())
            {
                QVariantMap item;
                item.insert(QStringLiteral("kind"),
                            nextpilot::plotscope::registry::kindName(node->kind()));
                item.insert(QStringLiteral("name"), node->name());
                item.insert(QStringLiteral("count"), node->count());
                item.insert(QStringLiteral("keys"), node->keysAsStrings());
                list.append(item);
            }
            return QVariant(list);
        });
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

QHash<QString, QString> RuntimeHost::activate(const QStringList &disabledIds, IUiContext *ui)
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

    for (const QString &id : m_order)
    {
        Node &node = m_nodes[id];
        if (!node.reg.factory)
        {
            errors.insert(id, QStringLiteral("工厂为空"));
            continue;
        }
        IWorkbenchContribution *module = node.reg.factory();
        if (!module)
        {
            errors.insert(id, QStringLiteral("工厂返回空"));
            continue;
        }
        module->setParent(this); // 所有权归宿主，delete 时整棵子树回收
        node.instance = module;
        m_initializing = id;
        // A1b §1.4.4：模块只收服务容器，不再收宿主本体。
        if (m_services)
            module->initialize(*m_services);
        m_initializing.clear();
        node.active = true;

        // A8b-M2 裁定 52441074：收集声明式 contributes（initialize 后数据已就绪）。
        // 不依赖 ui——headless 也能收集（纯数据）；owner 用模块 id（§4）。
        auto contrib = module->contributes();
        if (!contrib.isEmpty())
        {
            contrib.owner = id;
            m_contributions.append(std::move(contrib));
        }

        if (ui)
            module->onAttach(*ui); // headless 时 ui 为空，不建 UI
    }

    // A8b-M2 裁定 52442006 Step 3：灌回不再经壳（原 ui->installContributions），
    // 改调装配层 ContributionsInstaller（写 ISlotsService/StatusHub，壳只渲染）。
    // 语义与原 if(ui) 一致：headless（ui 空）不灌 UI 声明；bare 壳未接 installer
    // （nullptr）→ 也不灌，与"零模块"装配一致。
    if (ui && m_contribInstaller)
        for (const auto &c : m_contributions)
            m_contribInstaller->install(c);

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

    // 摘除本模块的声明式贡献：重激活时会重新收集，避免菜单/工具栏重复灌入。
    for (auto ci = m_contributions.begin(); ci != m_contributions.end();)
    {
        if (ci->owner == moduleId)
            ci = m_contributions.erase(ci);
        else
            ++ci;
    }
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

const nextpilot::plotscope::registry::UiSlotHub &RuntimeHost::uiSlots()
{
    // 每次访问都从当前工具箱重新绑定，装卸扩展包后立即反映，无需手动同步。
    m_uiSlotHub.refreshFrom(m_appCtx.toolbox());
    return m_uiSlotHub;
}

nextpilot::plotscope::registry::UiSlotHub &RuntimeHost::uiSlotsMutable()
{
    m_uiSlotHub.refreshFrom(m_appCtx.toolbox());
    return m_uiSlotHub;
}

nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult> RuntimeHost::execute(const nextpilot::plotscope::intent::Intent &intent)
{
    for (auto it = m_handlers.constBegin(); it != m_handlers.constEnd(); ++it)
    {
        nextpilot::plotscope::intent::IIntentHandler *handler = it.value();
        if (handler && handler->canHandle(intent))
            return handler->execute(intent);
    }
    return nextpilot::plotscope::foundation::fail(err::Unsupported,
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

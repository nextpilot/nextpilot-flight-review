#include "ToolboxHub.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace nextpilot::plotscope::registry
{

namespace
{

// 内置包工厂静态表：函数内 static，避免静态初始化顺序问题；保序（登记顺序即装载顺序）。
struct BuiltinPackStore
{
    QList<QPair<QString, ToolboxHub::PackFactory>> ordered;
    QHash<QString, int> indexById;
};

BuiltinPackStore &builtinPacks()
{
    // [静态纪律·合法] 进程一份、无生命周期、自注册表（PS_REGISTER_PACK TU 工厂）。
    static BuiltinPackStore s;
    return s;
}

bool producesFrames(const IMessageParser *p)
{
    return p && p->outputs().contains(ParseOutput::FrameBatch);
}

} // namespace

ToolboxHub::ToolboxHub(RegistryTree &tree)
    : RegistryNode(tree, RegistryKind::Extension, QStringLiteral("扩展包"))
{
}

// RegistryNode 可观测：扩展 id = 内置包 id ∪ contributes owner（去重）。
// 内置包与外部 manifest 是同一个 ExtensionRegistry 的两种来源，diagnostics 必须
// 同时看见——只装外置 manifest 时 extension 行 count 也应 >0（审核 52440654 §3）。
QStringList ToolboxHub::keysAsStrings() const
{
    QSet<QString> seen;
    for (const QString &id : packs.keys())
        seen.insert(id);
    for (const QString &owner : m_contribByOwner.keys())
        seen.insert(owner);
    return seen.values();
}

int ToolboxHub::count() const
{
    return keysAsStrings().size();
}

bool ToolboxHub::registerBuiltinPack(const QString &id, PackFactory factory)
{
    if (id.isEmpty() || !factory)
        return false;
    BuiltinPackStore &r = builtinPacks();
    if (r.indexById.contains(id))
        return false; // 重复 id：先到者为准
    r.indexById.insert(id, r.ordered.size());
    r.ordered.append({id, std::move(factory)});
    return true;
}

QStringList ToolboxHub::installBuiltinPacks()
{
    QStringList installed;
    for (const auto &entry : builtinPacks().ordered)
    {
        auto pack = entry.second();
        if (!pack)
            continue;
        const QString pid = pack->id;
        if (mount(std::move(*pack)))
            installed.append(pid);
    }
    return installed;
}

void ToolboxHub::resetBuiltinPacks()
{
    BuiltinPackStore &r = builtinPacks();
    r.ordered.clear();
    r.indexById.clear();
}

bool ToolboxHub::mount(Pack pack)
{
    if (pack.id.isEmpty())
        return false;

    // 校验：包 id 与全部条目 id 全局唯一（先到者为准）。
    if (packs.contains(pack.id))
        return false;
    QSet<QString> seen;
    seen.insert(pack.id);
    const auto checkId = [&seen](const QString &id) {
        if (id.isEmpty() || seen.contains(id))
            return false;
        seen.insert(id);
        return true;
    };
    for (const UiAction &a : pack.uiActions)
        if (!checkId(a.id) || uiActions.contains(a.id))
            return false;
    for (const PanelSpec &p : pack.panels)
        if (!checkId(p.id) || panels.contains(p.id))
            return false;
    for (const ScriptSpec &s : pack.scripts)
        if (!checkId(s.id) || scripts.contains(s.id))
            return false;
    for (const DataAsset &d : pack.dataAssets)
        if (!checkId(d.id) || dataAssets.contains(d.id))
            return false;
    for (const DocEntry &d : pack.docs)
        if (!checkId(d.id) || docs.contains(d.id))
            return false;
    for (const ExampleEntry &e : pack.examples)
        if (!checkId(e.id) || examples.contains(e.id))
            return false;
    for (const ReportTemplate &r : pack.reports)
        if (!checkId(r.id) || reports.contains(r.id))
            return false;
    for (const ViewTitleActionSpec &t : pack.viewTitleActions)
        if (!checkId(t.commandId + QLatin1Char('@') + t.viewId) || viewTitleActions.contains(t.commandId + QLatin1Char('@') + t.viewId))
            return false;
    for (const ItemContextSpec &c : pack.itemContexts)
        if (!checkId(c.commandId + QLatin1Char('@') + c.viewId) || itemContexts.contains(c.commandId + QLatin1Char('@') + c.viewId))
            return false;
    for (const SettingsPageSpec &p : pack.settingsPages)
        if (!checkId(p.id) || settingsPages.contains(p.id))
            return false;
    for (const ThemeTokenSpec &t : pack.themeTokens)
        if (!checkId(t.id) || themeTokens.contains(t.id))
            return false;
    for (const auto &p : pack.parsers)
        if (!p || !checkId(p->id()) || parser(p->id()) != nullptr)
            return false;

    // 挂载：写归属字段，各列表分插进对应类型的表。
    const QString provider = pack.id;
    const Domain domain = pack.domain;
    const auto stamp = [provider, domain](auto &entry) {
        entry.provider = provider;
        entry.domain = domain;
    };
    for (UiAction &a : pack.uiActions)
    {
        stamp(a);
        uiActions.insert(a.id, a);
    }
    for (PanelSpec &p : pack.panels)
    {
        stamp(p);
        panels.insert(p.id, p);
    }
    for (ScriptSpec &s : pack.scripts)
    {
        stamp(s);
        scripts.insert(s.id, s);
    }
    for (DataAsset &d : pack.dataAssets)
    {
        stamp(d);
        dataAssets.insert(d.id, d);
    }
    for (DocEntry &d : pack.docs)
    {
        stamp(d);
        docs.insert(d.id, d);
    }
    for (ExampleEntry &e : pack.examples)
    {
        stamp(e);
        examples.insert(e.id, e);
    }
    for (ReportTemplate &r : pack.reports)
    {
        stamp(r);
        reports.insert(r.id, r);
    }
    // 四个新贡献点：键用 `命令@视图`（同一命令可挂在多个视图的标题栏/右键上）。
    for (ViewTitleActionSpec &t : pack.viewTitleActions)
    {
        stamp(t);
        viewTitleActions.insert(t.commandId + QLatin1Char('@') + t.viewId, t);
    }
    for (ItemContextSpec &c : pack.itemContexts)
    {
        // 注意：本结构有**两个 domain**——`domain` 是键空间归属（字符串，D19），
        // `packDomain` 是包的领域枚举。stamp() 只写后者，前者是作者意图，不改写。
        c.provider = provider;
        c.packDomain = domain;
        itemContexts.insert(c.commandId + QLatin1Char('@') + c.viewId, c);
    }
    for (SettingsPageSpec &p : pack.settingsPages)
    {
        stamp(p);
        settingsPages.insert(p.id, p);
    }
    for (ThemeTokenSpec &t : pack.themeTokens)
    {
        stamp(t);
        themeTokens.insert(t.id, t);
    }
    for (auto &p : pack.parsers)
    {
        p->setProvider(provider);
        m_parsers.push_back(std::move(p));
    }
    packs.insert(pack.id, PackInfo{pack.id, pack.name, pack.domain});
    // 上树节点的统一信号：包粒度（画布联动/诊断广播消费）。
    Q_EMIT objectAdded(QVariant(pack.id));
    return true;
}

bool ToolboxHub::unmount(const QString &packId)
{
    if (!packs.contains(packId))
        return false;
    // 按包原子撤销：从各表移除 provider == packId 的全部条目，扩展之间互不牵连。
    const auto dropByProvider = [packId](auto &table) {
        for (const QString &id : table.keys())
        {
            const auto *e = table.find(id);
            if (e && e->provider == packId)
                table.remove(id);
        }
    };
    dropByProvider(uiActions);
    dropByProvider(panels);
    dropByProvider(scripts);
    dropByProvider(dataAssets);
    dropByProvider(docs);
    dropByProvider(examples);
    dropByProvider(reports);
    dropByProvider(viewTitleActions);
    dropByProvider(itemContexts);
    dropByProvider(settingsPages);
    dropByProvider(themeTokens);
    m_parsers.erase(std::remove_if(m_parsers.begin(), m_parsers.end(),
                                   [&packId](const auto &p) {
                                       return p && p->provider() == packId;
                                   }),
                    m_parsers.end());
    packs.remove(packId);
    Q_EMIT objectRemoved(QVariant(packId));
    return true;
}

// === 壳的查询面（按视图裁剪 + 排序，壳不直接翻表）===

QList<ViewTitleActionSpec> ToolboxHub::titleActionsFor(const QString &viewId) const
{
    QList<ViewTitleActionSpec> out;
    for (const ViewTitleActionSpec &t : viewTitleActions.values())
        if (t.viewId == viewId)
            out.append(t);
    std::stable_sort(out.begin(), out.end(), [](const ViewTitleActionSpec &a, const ViewTitleActionSpec &b) {
        return a.order != b.order ? a.order < b.order : a.commandId < b.commandId;
    });
    return out;
}

QList<ItemContextSpec> ToolboxHub::itemContextsFor(const QString &viewId) const
{
    QList<ItemContextSpec> out;
    for (const ItemContextSpec &c : itemContexts.values())
        if (c.viewId == viewId)
            out.append(c);
    std::stable_sort(out.begin(), out.end(), [](const ItemContextSpec &a, const ItemContextSpec &b) {
        return a.order != b.order ? a.order < b.order : a.commandId < b.commandId;
    });
    return out;
}

QList<SettingsPageSpec> ToolboxHub::settingsPagesSorted() const
{
    QList<SettingsPageSpec> out = settingsPages.values();
    std::stable_sort(out.begin(), out.end(), [](const SettingsPageSpec &a, const SettingsPageSpec &b) {
        return a.order != b.order ? a.order < b.order : a.id < b.id;
    });
    return out;
}

QList<ThemeTokenSpec> ToolboxHub::themeTokensAll() const
{
    QList<ThemeTokenSpec> out = themeTokens.values();
    std::stable_sort(out.begin(), out.end(), [](const ThemeTokenSpec &a, const ThemeTokenSpec &b) {
        return a.id < b.id;
    });
    return out;
}

QString ToolboxHub::themeFallback(const QString &tokenId) const
{
    const ThemeTokenSpec *t = themeTokens.find(tokenId);
    return t ? t->fallback : QString();
}

// === 解析器视图（行为式条目）===

QList<IMessageParser *> ToolboxHub::parsers() const
{
    QList<IMessageParser *> out;
    out.reserve(static_cast<int>(m_parsers.size()));
    for (const auto &p : m_parsers)
        out.append(p.get());
    return out;
}

QList<IMessageParser *> ToolboxHub::parsersByDomain(Domain domain) const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (p->domain() == domain)
            out.append(p);
    return out;
}

IMessageParser *ToolboxHub::parser(const QString &id) const
{
    for (const auto &p : m_parsers)
        if (p && p->id() == id)
            return p.get();
    return nullptr;
}

IMessageParser *ToolboxHub::parserForProtocol(const QString &protocolId) const
{
    const QString key = protocolId.trimmed().toLower();
    for (IMessageParser *p : parsers())
        if (p->id().toLower() == key || p->protocolName().toLower() == key)
            return p;
    return nullptr;
}

QList<IMessageParser *> ToolboxHub::parsersByTransport(const QString &transport) const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (p->supportedTransports().contains(transport, Qt::CaseInsensitive))
            out.append(p);
    return out;
}

// === 旧 API 兼容别名：筛 outputs() 含 FrameBatch 的解析器 ===

bool ToolboxHub::registerDriver(std::unique_ptr<IMessageParser> driver)
{
    if (!driver)
        return false;
    Pack pack;
    pack.id = QStringLiteral("auto.") + driver->id();
    pack.name = driver->displayName();
    pack.domain = driver->domain();
    pack.parsers.push_back(std::move(driver));
    return mount(std::move(pack));
}

bool ToolboxHub::unregisterDriver(const QString &id)
{
    IMessageParser *p = parser(id);
    return p ? unmount(p->provider()) : false;
}

IMessageParser *ToolboxHub::driverForFormat(const QString &format) const
{
    if (format.isEmpty())
        return nullptr;
    for (IMessageParser *p : parsers())
        if (producesFrames(p) && p->acceptsFormat(format))
            return p;
    return nullptr;
}

IMessageParser *ToolboxHub::driverForPath(const QString &path) const
{
    for (IMessageParser *p : parsers())
        if (producesFrames(p) && p->acceptsSuffix(path))
            return p;
    return nullptr;
}

QList<IMessageParser *> ToolboxHub::drivers() const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (producesFrames(p))
            out.append(p);
    return out;
}

QList<IMessageParser *> ToolboxHub::driversByDomain(Domain domain) const
{
    QList<IMessageParser *> out;
    for (IMessageParser *p : parsers())
        if (producesFrames(p) && p->domain() == domain)
            out.append(p);
    return out;
}

QList<Domain> ToolboxHub::domains() const
{
    QList<Domain> out;
    for (IMessageParser *p : drivers())
        if (!out.contains(p->domain()))
            out.append(p->domain());
    return out;
}

QStringList ToolboxHub::ids() const
{
    QStringList out;
    for (IMessageParser *p : drivers())
        out.append(p->id());
    return out;
}

bool ToolboxHub::contains(const QString &id) const
{
    return parser(id) != nullptr;
}

// count() 改为 RegistryNode::count override（头文件内联，packs 表口径）。

// === vscode contributes 贡献面（A8b-M1 / IKXXJ2）===
// 与 mount(Pack) 并存于同一 Extension 节点：贡献是扩展的属性，不是第二个宿主。

bool ToolboxHub::mountContribution(const ContributesDoc &doc)
{
    if (doc.owner.isEmpty())
    {
        recordContributionIssue(QStringLiteral("<empty>"), QStringLiteral("owner"),
                                QStringLiteral("owner 为空，拒载"));
        return false;
    }
    if (m_contribByOwner.contains(doc.owner))
    {
        recordContributionIssue(doc.owner, QStringLiteral("owner"),
                                QStringLiteral("owner 已挂载，重复 mount 拒（请先 unmount）"));
        return false;
    }

    m_contribByOwner.insert(doc.owner, QStringList());
    m_contribOwnerOrder.append(doc.owner);

    QStringList ownedIds;
    for (const auto &c : doc.commands)
    {
        if (m_contribCommands.contains(c.command))
        {
            recordContributionIssue(doc.owner, QStringLiteral("commands.") + c.command,
                                    QStringLiteral("命令 id 已被其他 owner 占用，跳过（先到者保留）"));
            continue;
        }
        m_contribCommands.insert(c.command, c);
        m_contribCommandOwner.insert(c.command, doc.owner);
        ownedIds.append(c.command);
    }
    m_contribByOwner.insert(doc.owner, ownedIds);

    for (const auto &item : doc.commandPalette)
        m_contribPalette.append(PaletteEntry{item, doc.owner});

    // M2：其余键同样按 owner 归属存表，unmount 时整批撤。
    for (const auto &item : doc.viewContainers)
        m_contribViewContainers.append(OwnedViewContainer{item, doc.owner});
    for (const auto &item : doc.views)
        m_contribViews.append(OwnedView{item, doc.owner});
    for (const auto &item : doc.menuItems)
        m_contribMenuItems.append(OwnedMenuItem{item, doc.owner});
    for (const auto &item : doc.colors)
        m_contribColors.append(OwnedColor{item, doc.owner});
    for (const auto &item : doc.statuses)
        m_contribStatuses.append(OwnedStatus{item, doc.owner});
    // M3-a：customEditors 按 owner 归属存表；viewType 跨 owner 全局唯一，重复跳过 + 诊断（含双方 owner）。
    for (const auto &item : doc.customEditors)
    {
        if (m_contribEditorViewTypes.contains(item.viewType))
        {
            recordContributionIssue(
                doc.owner, QStringLiteral("customEditors.") + item.viewType,
                QStringLiteral("owner %2 的 viewType 已被 owner %1 声明，跳过（先到者保留）")
                    .arg(m_contribEditorViewTypes.value(item.viewType), doc.owner));
            continue;
        }
        m_contribEditorViewTypes.insert(item.viewType, doc.owner);
        m_contribCustomEditors.append(OwnedCustomEditor{item, doc.owner});
    }

    Q_EMIT objectAdded(doc.owner);
    return true;
}

QList<ToolboxHub::OwnedMenuItem> ToolboxHub::menuItemsIn(const QString &place) const
{
    QList<OwnedMenuItem> out;
    for (const auto &it : m_contribMenuItems)
        if (it.item.place == place)
            out.append(it);
    return out;
}

bool ToolboxHub::unmountContribution(const QString &owner)
{
    if (!m_contribByOwner.contains(owner))
        return false;

    for (int i = m_contribPalette.size() - 1; i >= 0; --i)
        if (m_contribPalette.at(i).owner == owner)
            m_contribPalette.removeAt(i);
    // M2 其余键整批撤（倒序删，避开索引失效）。
    auto removeOwned = [&owner](auto &list) {
        for (int i = list.size() - 1; i >= 0; --i)
            if (list.at(i).owner == owner)
                list.removeAt(i);
    };
    removeOwned(m_contribViewContainers);
    removeOwned(m_contribViews);
    removeOwned(m_contribMenuItems);
    removeOwned(m_contribColors);
    removeOwned(m_contribStatuses);
    removeOwned(m_contribCustomEditors);
    for (auto it = m_contribEditorViewTypes.begin(); it != m_contribEditorViewTypes.end();)
    {
        if (it.value() == owner)
            it = m_contribEditorViewTypes.erase(it);
        else
            ++it;
    }

    const QStringList ids = m_contribByOwner.value(owner);
    for (const QString &id : ids)
    {
        m_contribCommands.remove(id);
        m_contribCommandOwner.remove(id);
    }
    m_contribByOwner.remove(owner);
    m_contribOwnerOrder.removeAll(owner);

    Q_EMIT objectRemoved(owner);
    return true;
}

const ContributedCommand *ToolboxHub::findCommand(const QString &commandId) const
{
    return m_contribCommands.find(commandId);
}

QStringList ToolboxHub::commandIdsOf(const QString &owner) const
{
    return m_contribByOwner.value(owner);
}

void ToolboxHub::recordContributionIssue(const QString &owner, const QString &keyPath,
                                         const QString &message)
{
    m_contribDiagnostics.append(ContributionDiagnostic{owner, keyPath, message});
}

} // namespace nextpilot::plotscope::registry

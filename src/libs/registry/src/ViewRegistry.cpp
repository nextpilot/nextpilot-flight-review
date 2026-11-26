#include "ViewRegistry.h"

#include "IDisposable.h" // workbench（Handle 基类需要完整定义）
#include "IEditorProvider.h"
#include "ResourceRef.h"

#include <algorithm>
#include <limits>

namespace nextpilot::plotscope::registry
{

bool ViewRegistry::registerViewProvider(const QString &viewId, ViewFactory factory,
                                        const QString &owner)
{
    if (viewId.isEmpty() || !factory)
        return false;
    if (m_providers.contains(viewId))
        return false; // 先到者为准
    m_providers.insert(viewId, ProviderEntry{std::move(factory), owner});
    return true;
}

int ViewRegistry::unregisterOwner(const QString &owner)
{
    int removed = 0;
    for (auto it = m_providers.begin(); it != m_providers.end();)
    {
        if (it.value().owner == owner)
        {
            it = m_providers.erase(it);
            ++removed;
        }
        else
            ++it;
    }
    return removed;
}

QWidget *ViewRegistry::create(const QString &viewId, context::AppContext &appCtx,
                              intent::IIntentExecutor &executor, QWidget *parent) const
{
    const ProviderEntry *e = provider(viewId);
    return e ? e->factory(appCtx, executor, parent) : nullptr;
}

bool ViewRegistry::registerActionProvider(const QString &owner, const QString &slotId,
                                          IActionProvider *p)
{
    if (owner.isEmpty() || slotId.isEmpty() || p == nullptr)
        return false;
    // 同 owner 同 slot 只允许一个：先到者为准，与 registerViewProvider 口径一致。
    for (const auto &e : m_actionProviders)
        if (e.owner == owner && e.slotId == slotId)
            return false;
    m_actionProviders.append(ActionProviderEntry{p, owner, slotId});
    return true;
}

QList<ViewRegistry::ActionProviderEntry> ViewRegistry::actionProviders() const
{
    return m_actionProviders;
}

QList<ViewRegistry::ActionProviderEntry> ViewRegistry::actionProvidersFor(const QString &slotId) const
{
    QList<ActionProviderEntry> out;
    for (const auto &e : m_actionProviders)
        if (e.slotId == slotId)
            out.append(e);
    return out;
}

void ViewRegistry::declare(const ContributesDoc &doc)
{
    if (doc.owner.isEmpty())
        return;
    if (!m_ownerOrder.contains(doc.owner))
        m_ownerOrder.append(doc.owner);
    for (auto c : doc.viewContainers)
    {
        c.owner = doc.owner;
        m_containers.append(c);
    }
    for (auto v : doc.views)
    {
        v.owner = doc.owner;
        m_views.append(v);
    }
    for (const auto &e : doc.customEditors)
    {
        auto withOwner = e;
        withOwner.owner = doc.owner;
        m_editors.append(withOwner);
    }
}

void ViewRegistry::undeclare(const QString &owner)
{
    m_ownerOrder.removeAll(owner);
    for (int i = m_containers.size() - 1; i >= 0; --i)
        if (m_containers.at(i).owner == owner)
            m_containers.removeAt(i);
    for (int i = m_views.size() - 1; i >= 0; --i)
        if (m_views.at(i).owner == owner)
            m_views.removeAt(i);
    for (int i = m_editors.size() - 1; i >= 0; --i)
        if (m_editors.at(i).owner == owner)
            m_editors.removeAt(i);
    unregisterOwner(owner);
    for (int i = m_actionProviders.size() - 1; i >= 0; --i)
        if (m_actionProviders.at(i).owner == owner)
            m_actionProviders.removeAt(i);
    // 编辑器 provider 借用指针随 owner 整批撤，无悬空。
    for (auto it = m_editorProviders.begin(); it != m_editorProviders.end();)
    {
        if (it.value().owner == owner)
            it = m_editorProviders.erase(it);
        else
            ++it;
    }
}

QList<ContributedViewContainer> ViewRegistry::containersOf(const QString &owner) const
{
    QList<ContributedViewContainer> out;
    for (const auto &c : m_containers)
        if (c.owner == owner)
            out.append(c);
    return out;
}

QList<ContributedView> ViewRegistry::viewsOf(const QString &owner) const
{
    QList<ContributedView> out;
    for (const auto &v : m_views)
        if (v.owner == owner)
            out.append(v);
    return out;
}

QList<ContributedView> ViewRegistry::viewsIn(const QString &containerId) const
{
    QList<ContributedView> out;
    for (const auto &v : m_views)
        if (v.container == containerId)
            out.append(v);
    std::stable_sort(out.begin(), out.end(), [](const ContributedView &a, const ContributedView &b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.id < b.id;
    });
    return out;
}

// ---- 编辑器（M3-a 第 2 笔）----

namespace
{
// 简单 glob：仅支持 '*'（任意段）与 '?'（单字符），大小写不敏感。
bool globMatch(const QString &pattern, const QString &text)
{
    auto match = [](auto self, int pi, int ti, const QString &p, const QString &t) -> bool {
        while (pi < p.size())
        {
            const QChar pc = p.at(pi);
            if (pc == QLatin1Char('*'))
            {
                if (pi + 1 == p.size())
                    return true;
                for (int k = ti; k <= t.size(); ++k)
                    if (self(self, pi + 1, k, p, t))
                        return true;
                return false;
            }
            if (ti >= t.size())
                return false;
            const QChar tc = t.at(ti);
            const bool ok = (pc == QLatin1Char('?')) ||
                            pc.toLower() == tc.toLower();
            if (!ok)
                return false;
            ++pi;
            ++ti;
        }
        return ti == t.size();
    };
    return match(match, 0, 0, pattern, text);
}
} // namespace

std::unique_ptr<IDisposable>
ViewRegistry::registerEditorProvider(const QString &viewType, IEditorProvider *provider,
                                     const QString &owner)
{
    if (viewType.isEmpty() || provider == nullptr || owner.isEmpty())
        return nullptr;
    // 防冒名（正文 §3.0.4）：owner 必须等于该 viewType 的声明者；未声明一律拒。
    if (editorOwner(viewType) != owner)
        return nullptr;
    if (m_editorProviders.contains(viewType))
        return nullptr; // 先到者为准
    m_editorProviders.insert(viewType, EditorProviderEntry{provider, owner, false});

    class Handle final : public IDisposable
    {
    public:
        Handle(ViewRegistry *reg, QString viewType)
            : m_reg(reg), m_viewType(std::move(viewType))
        {
        }
        void dispose() override
        {
            if (m_reg && !m_viewType.isEmpty())
            {
                m_reg->m_editorProviders.remove(m_viewType);
                m_viewType.clear();
            }
        }

    private:
        ViewRegistry *m_reg;
        QString m_viewType;
    };
    return std::make_unique<Handle>(this, viewType);
}

IEditorProvider *ViewRegistry::editorProvider(const QString &viewType) const
{
    auto it = m_editorProviders.constFind(viewType);
    return it == m_editorProviders.constEnd() ? nullptr : it.value().provider;
}

QString ViewRegistry::editorViewTypeFor(const ResourceRef &ref) const
{
    // priority 降序取第一个「有声明且有运行层 provider」的 viewType。
    // 哨兵必须是 int 最小值：-1 会把 priority < -1 的声明（如 settings -10）整段排除。
    QString best;
    int bestPriority = std::numeric_limits<int>::min();
    for (const auto &e : m_editors)
    {
        if (e.priority < bestPriority)
            continue;
        bool hit = false;
        for (const auto &sel : e.selector)
        {
            if (!sel.mime.isEmpty() && sel.mime == ref.mime)
            {
                hit = true;
                break;
            }
            if (!sel.filenamePattern.isEmpty() && globMatch(sel.filenamePattern, ref.uri))
            {
                hit = true;
                break;
            }
        }
        if (!hit || editorProvider(e.viewType) == nullptr)
            continue;
        best = e.viewType;
        bestPriority = e.priority;
    }
    return best;
}

QList<ContributedCustomEditor> ViewRegistry::customEditorsOf(const QString &owner) const
{
    QList<ContributedCustomEditor> out;
    for (const auto &e : m_editors)
        if (e.owner == owner)
            out.append(e);
    return out;
}

QString ViewRegistry::editorOwner(const QString &viewType) const
{
    for (const auto &e : m_editors)
        if (e.viewType == viewType)
            return e.owner;
    return QString();
}

} // namespace nextpilot::plotscope::registry

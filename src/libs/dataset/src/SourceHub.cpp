#include "SourceHub.h"
#include "ErrorCodes.h"

#include <algorithm>

namespace nextpilot::plotscope::dataset
{
using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;
using nextpilot::plotscope::registry::RegistryNode;
using nextpilot::plotscope::registry::RegistryKind;
using nextpilot::plotscope::registry::RegistryTree;
namespace err = nextpilot::plotscope::foundation::err;

SourceHub::SourceHub(RegistryTree &tree, QObject *parent)
    : RegistryNode(tree, RegistryKind::Source, QStringLiteral("数据源"), parent)
{
}

int SourceHub::add(DataSourceInfo info)
{
    const int id = m_nextId++;
    info.id = id;
    m_sources.insert(id, info);
    Q_EMIT sourceAdded(id);
    Q_EMIT objectAdded(id); // M2.8：统一节点信号（带 key）
    return id;
}

Result<int> SourceHub::addWithStableId(DataSourceInfo info, int id)
{
    if (id < 0)
        return fail(err::Id, QStringLiteral("数据源 id 非法"));
    if (m_sources.contains(id))
        return fail(err::DupId, QStringLiteral("数据源 id 已存在: %1").arg(id));
    info.id = id;
    m_sources.insert(id, info);
    if (id >= m_nextId)
        m_nextId = id + 1;
    Q_EMIT sourceAdded(id);
    Q_EMIT objectAdded(id); // M2.8：统一节点信号（带 key）
    return id;
}

QStringList SourceHub::keysAsStrings() const
{
    QStringList out;
    out.reserve(m_sources.count());
    for (int id : m_sources.keys())
    {
        const DataSourceInfo *info = m_sources.find(id);
        out.append(info && !info->name.isEmpty() ? info->name : QString::number(id));
    }
    return out;
}

bool SourceHub::update(int id, const DataSourceInfo &info)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    DataSourceInfo updated = info;
    updated.id = id; // id 不可变
    *cur = updated;
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceHub::setState(int id, SourceState state, const QString &error)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    cur->state = state;
    cur->error = error;
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceHub::bindParser(int id, const ParserBinding &binding)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    auto &list = cur->parsers;
    // 同解析器覆盖旧绑定（改配置，不新增一路）
    for (ParserBinding &b : list)
    {
        if (b.parserId == binding.parserId)
        {
            b = binding;
            Q_EMIT sourceChanged(id);
            return true;
        }
    }
    // 不同解析器 = 新增一路扇出
    list.append(binding);
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceHub::unbindParser(int id, const QString &parserId)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    auto &list = cur->parsers;
    const auto before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(),
                               [&parserId](const ParserBinding &b) { return b.parserId == parserId; }),
               list.end());
    if (list.size() == before)
        return false;
    Q_EMIT sourceChanged(id);
    return true;
}

const ParserBinding *SourceHub::parserFor(int id, const QString &parserId) const
{
    const DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return nullptr;
    for (const ParserBinding &b : cur->parsers)
        if (b.parserId == parserId)
            return &b;
    return nullptr;
}

QVector<ParserBinding> SourceHub::parsersOf(int id) const
{
    const DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return {};
    return cur->parsers;
}

bool SourceHub::addDataset(int id, int datasetId)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    if (!cur->datasetIds.contains(datasetId))
        cur->datasetIds.append(datasetId);
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceHub::removeDataset(int id, int datasetId)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    const bool removed = cur->datasetIds.removeAll(datasetId) > 0;
    if (removed)
        Q_EMIT sourceChanged(id);
    return removed;
}

bool SourceHub::remove(int id, QVector<int> *datasetIds)
{
    const DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    if (datasetIds)
        *datasetIds = cur->datasetIds;
    m_sources.remove(id);
    Q_EMIT sourceRemoved(id);
    Q_EMIT objectRemoved(id); // M2.8：统一节点信号（带 key）
    return true;
}

const DataSourceInfo *SourceHub::get(int id) const
{
    return m_sources.find(id);
}

int SourceHub::sourceOfDataset(int datasetId) const
{
    if (datasetId < 0)
        return -1;
    for (const DataSourceInfo &info : m_sources.values())
    {
        if (info.datasetIds.contains(datasetId))
            return info.id;
    }
    return -1;
}

int SourceHub::sourceByOrigin(const QString &origin) const
{
    if (origin.isEmpty())
        return -1;
    for (const DataSourceInfo &info : m_sources.values())
    {
        if (info.origin == origin)
            return info.id;
    }
    return -1;
}

QVector<int> SourceHub::ids() const
{
    QVector<int> out = m_sources.keys().toVector();
    std::sort(out.begin(), out.end());
    return out;
}

void SourceHub::clear()
{
    const auto all = ids();
    for (int id : all)
        remove(id);
}

void SourceHub::demandOrigin(const QString &origin)
{
    if (origin.isEmpty())
        return;
    for (auto &d : m_demands)
    {
        if (d.first == origin)
        {
            ++d.second;
            return;
        }
    }
    m_demands.append({origin, 1});
}

void SourceHub::releaseOrigin(const QString &origin)
{
    for (int i = 0; i < m_demands.size(); ++i)
    {
        if (m_demands.at(i).first == origin)
        {
            if (--m_demands[i].second <= 0)
                m_demands.removeAt(i);
            return;
        }
    }
}

int SourceHub::demandCount(const QString &origin) const
{
    for (const auto &d : m_demands)
    {
        if (d.first == origin)
            return d.second;
    }
    return 0;
}

QStringList SourceHub::demandedOrigins() const
{
    QStringList out;
    for (const auto &d : m_demands)
        out << d.first;
    return out;
}

QStringList SourceHub::unsatisfiedDemands() const
{
    QStringList out;
    for (const auto &d : m_demands)
    {
        if (sourceByOrigin(d.first) < 0)
            out << d.first;
    }
    return out;
}

} // namespace nextpilot::plotscope::dataset

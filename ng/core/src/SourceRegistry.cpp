#include "SourceRegistry.h"

#include <algorithm>

namespace nextpilot::plotscope::core
{

SourceRegistry::SourceRegistry(QObject *parent) : QObject(parent) {}

int SourceRegistry::add(DataSourceInfo info)
{
    const int id = m_nextId++;
    info.id = id;
    m_sources.insert(id, info);
    Q_EMIT sourceAdded(id);
    return id;
}

Result<int> SourceRegistry::addWithStableId(DataSourceInfo info, int id)
{
    if (id < 0)
        return fail(QStringLiteral("eid"), QStringLiteral("数据源 id 非法"));
    if (m_sources.contains(id))
        return fail(QStringLiteral("edupid"), QStringLiteral("数据源 id 已存在: %1").arg(id));
    info.id = id;
    m_sources.insert(id, info);
    if (id >= m_nextId)
        m_nextId = id + 1;
    Q_EMIT sourceAdded(id);
    return id;
}

bool SourceRegistry::update(int id, const DataSourceInfo &info)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return false;
    DataSourceInfo updated = info;
    updated.id = id; // id 不可变
    it.value() = updated;
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceRegistry::setState(int id, SourceState state, const QString &error)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return false;
    it.value().state = state;
    it.value().error = error;
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceRegistry::bindParser(int id, const ParserBinding &binding)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return false;
    auto &list = it.value().parsers;
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

bool SourceRegistry::unbindParser(int id, const QString &parserId)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return false;
    auto &list = it.value().parsers;
    const auto before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(),
                               [&parserId](const ParserBinding &b) { return b.parserId == parserId; }),
               list.end());
    if (list.size() == before)
        return false;
    Q_EMIT sourceChanged(id);
    return true;
}

const ParserBinding *SourceRegistry::parserFor(int id, const QString &parserId) const
{
    const auto it = m_sources.constFind(id);
    if (it == m_sources.constEnd())
        return nullptr;
    for (const ParserBinding &b : it.value().parsers)
        if (b.parserId == parserId)
            return &b;
    return nullptr;
}

QVector<ParserBinding> SourceRegistry::parsersOf(int id) const
{
    const auto it = m_sources.constFind(id);
    if (it == m_sources.constEnd())
        return {};
    return it.value().parsers;
}

bool SourceRegistry::addDataset(int id, int datasetId)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return false;
    if (!it.value().datasetIds.contains(datasetId))
        it.value().datasetIds.append(datasetId);
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceRegistry::removeDataset(int id, int datasetId)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return false;
    const bool removed = it.value().datasetIds.removeAll(datasetId) > 0;
    if (removed)
        Q_EMIT sourceChanged(id);
    return removed;
}

bool SourceRegistry::remove(int id, QVector<int> *datasetIds)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return false;
    if (datasetIds)
        *datasetIds = it.value().datasetIds;
    m_sources.erase(it);
    Q_EMIT sourceRemoved(id);
    return true;
}

const DataSourceInfo *SourceRegistry::get(int id) const
{
    const auto it = m_sources.constFind(id);
    return it == m_sources.constEnd() ? nullptr : &it.value();
}

int SourceRegistry::sourceOfDataset(int datasetId) const
{
    if (datasetId < 0)
        return -1;
    for (const DataSourceInfo &info : m_sources)
    {
        if (info.datasetIds.contains(datasetId))
            return info.id;
    }
    return -1;
}

int SourceRegistry::sourceByOrigin(const QString &origin) const
{
    if (origin.isEmpty())
        return -1;
    for (const DataSourceInfo &info : m_sources)
    {
        if (info.origin == origin)
            return info.id;
    }
    return -1;
}

QVector<int> SourceRegistry::ids() const
{
    QVector<int> out = m_sources.keys();
    std::sort(out.begin(), out.end());
    return out;
}

int SourceRegistry::count() const
{
    return m_sources.size();
}

void SourceRegistry::clear()
{
    const auto all = ids();
    for (int id : all)
        remove(id);
}

} // namespace nextpilot::plotscope::core

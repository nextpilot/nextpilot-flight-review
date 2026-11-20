#include "SourceRegistry.h"
#include "ErrorCodes.h"

#include <algorithm>

namespace nextpilot::plotscope::dataset
{
using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;
namespace err = nextpilot::plotscope::foundation::err;

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
        return fail(err::Id, QStringLiteral("数据源 id 非法"));
    if (m_sources.contains(id))
        return fail(err::DupId, QStringLiteral("数据源 id 已存在: %1").arg(id));
    info.id = id;
    m_sources.insert(id, info);
    if (id >= m_nextId)
        m_nextId = id + 1;
    Q_EMIT sourceAdded(id);
    return id;
}

bool SourceRegistry::update(int id, const DataSourceInfo &info)
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

bool SourceRegistry::setState(int id, SourceState state, const QString &error)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    cur->state = state;
    cur->error = error;
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceRegistry::bindParser(int id, const ParserBinding &binding)
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

bool SourceRegistry::unbindParser(int id, const QString &parserId)
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

const ParserBinding *SourceRegistry::parserFor(int id, const QString &parserId) const
{
    const DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return nullptr;
    for (const ParserBinding &b : cur->parsers)
        if (b.parserId == parserId)
            return &b;
    return nullptr;
}

QVector<ParserBinding> SourceRegistry::parsersOf(int id) const
{
    const DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return {};
    return cur->parsers;
}

bool SourceRegistry::addDataset(int id, int datasetId)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    if (!cur->datasetIds.contains(datasetId))
        cur->datasetIds.append(datasetId);
    Q_EMIT sourceChanged(id);
    return true;
}

bool SourceRegistry::removeDataset(int id, int datasetId)
{
    DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    const bool removed = cur->datasetIds.removeAll(datasetId) > 0;
    if (removed)
        Q_EMIT sourceChanged(id);
    return removed;
}

bool SourceRegistry::remove(int id, QVector<int> *datasetIds)
{
    const DataSourceInfo *cur = m_sources.find(id);
    if (!cur)
        return false;
    if (datasetIds)
        *datasetIds = cur->datasetIds;
    m_sources.remove(id);
    Q_EMIT sourceRemoved(id);
    return true;
}

const DataSourceInfo *SourceRegistry::get(int id) const
{
    return m_sources.find(id);
}

int SourceRegistry::sourceOfDataset(int datasetId) const
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

int SourceRegistry::sourceByOrigin(const QString &origin) const
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

QVector<int> SourceRegistry::ids() const
{
    QVector<int> out = m_sources.keys().toVector();
    std::sort(out.begin(), out.end());
    return out;
}

int SourceRegistry::count() const
{
    return m_sources.count();
}

void SourceRegistry::clear()
{
    const auto all = ids();
    for (int id : all)
        remove(id);
}

} // namespace nextpilot::plotscope::dataset

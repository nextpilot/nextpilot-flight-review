#include "DataRepository.h"

namespace nextpilot::plotscope::core
{

DataRepository::DataRepository(QObject *parent) : QObject(parent) {}

DatasetId DataRepository::add(Dataset dataset)
{
    const DatasetId id = m_nextId++;
    m_datasets.insert(id, std::move(dataset));
    Q_EMIT datasetAdded(id);
    return id;
}

Result<DatasetId> DataRepository::addWithId(Dataset dataset, const QString &requestedName)
{
    if (requestedName.isEmpty())
        return fail(QStringLiteral("ename"), QStringLiteral("数据集名为空"));
    if (!dataset.name().isEmpty() && dataset.name() != requestedName)
        dataset = Dataset(requestedName);
    return add(std::move(dataset));
}

Result<DatasetId> DataRepository::addWithStableId(Dataset dataset, DatasetId id)
{
    if (m_datasets.contains(id))
        return fail(QStringLiteral("edupid"), QStringLiteral("数据集 id 已存在: %1").arg(id));
    m_datasets.insert(id, std::move(dataset));
    if (id >= m_nextId)
        m_nextId = id + 1;
    Q_EMIT datasetAdded(id);
    return id;
}

bool DataRepository::remove(DatasetId id)
{
    if (!m_datasets.remove(id))
        return false;
    Q_EMIT datasetRemoved(id);
    return true;
}

const Dataset *DataRepository::get(DatasetId id) const
{
    const auto it = m_datasets.constFind(id);
    return it == m_datasets.constEnd() ? nullptr : &it.value();
}

QVector<DatasetId> DataRepository::ids() const
{
    QVector<DatasetId> result;
    result.reserve(m_datasets.size());
    for (auto it = m_datasets.constBegin(); it != m_datasets.constEnd(); ++it)
        result.push_back(it.key());
    return result;
}

int DataRepository::count() const
{
    return m_datasets.size();
}

void DataRepository::clear()
{
    const auto keys = m_datasets.keys();
    m_datasets.clear();
    for (DatasetId id : keys)
        Q_EMIT datasetRemoved(id);
}

} // namespace nextpilot::plotscope::core

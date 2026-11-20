#include "DatasetRegistry.h"
#include "ErrorCodes.h"

#include <algorithm>

namespace nextpilot::plotscope::dataset
{
using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;
namespace err = nextpilot::plotscope::foundation::err;

DatasetRegistry::DatasetRegistry(QObject *parent) : QObject(parent) {}

DatasetId DatasetRegistry::add(Dataset dataset)
{
    const DatasetId id = m_nextId++;
    m_datasets.insert(id, std::move(dataset));
    Q_EMIT datasetAdded(id);
    return id;
}

Result<DatasetId> DatasetRegistry::addWithId(Dataset dataset, const QString &requestedName)
{
    if (requestedName.isEmpty())
        return fail(err::Name, QStringLiteral("数据集名为空"));
    if (!dataset.name().isEmpty() && dataset.name() != requestedName)
        dataset = Dataset(requestedName);
    return add(std::move(dataset));
}

Result<DatasetId> DatasetRegistry::addWithStableId(Dataset dataset, DatasetId id)
{
    if (m_datasets.contains(id))
        return fail(err::DupId, QStringLiteral("数据集 id 已存在: %1").arg(id));
    m_datasets.insert(id, std::move(dataset));
    if (id >= m_nextId)
        m_nextId = id + 1;
    Q_EMIT datasetAdded(id);
    return id;
}

bool DatasetRegistry::remove(DatasetId id)
{
    if (!m_datasets.remove(id))
        return false;
    Q_EMIT datasetRemoved(id);
    return true;
}

const Dataset *DatasetRegistry::get(DatasetId id) const
{
    return m_datasets.find(id);
}

Dataset *DatasetRegistry::getMutable(DatasetId id)
{
    return m_datasets.find(id);
}

QVector<DatasetId> DatasetRegistry::ids() const
{
    QVector<DatasetId> result = m_datasets.keys().toVector();
    std::sort(result.begin(), result.end());
    return result;
}

int DatasetRegistry::count() const
{
    return m_datasets.count();
}

void DatasetRegistry::clear()
{
    const QVector<DatasetId> all = m_datasets.keys().toVector();
    m_datasets.clear();
    for (DatasetId id : all)
        Q_EMIT datasetRemoved(id);
}

} // namespace nextpilot::plotscope::dataset

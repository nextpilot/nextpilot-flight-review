#include "DatasetHub.h"
#include "ErrorCodes.h"

#include <algorithm>

namespace nextpilot::plotscope::dataset
{
using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;
namespace err = nextpilot::plotscope::foundation::err;

DatasetHub::DatasetHub(RegistryTree &tree, QObject *parent)
    : TypedRegistry<Dataset, DatasetId>(
          tree, nextpilot::plotscope::registry::RegistryKind::Dataset,
          QStringLiteral("数据集"), parent)
{
}

DatasetId DatasetHub::add(Dataset dataset)
{
    // id 分配与基类信号（objectAdded）由 TypedRegistry 上收；这里只补领域信号。
    const DatasetId id = TypedRegistry::add(std::move(dataset));
    Q_EMIT datasetAdded(id);
    return id;
}

Result<DatasetId> DatasetHub::addWithId(Dataset dataset, const QString &requestedName)
{
    if (requestedName.isEmpty())
        return fail(err::Name, QStringLiteral("数据集名为空"));
    if (!dataset.name().isEmpty() && dataset.name() != requestedName)
        dataset = Dataset(requestedName);
    return add(std::move(dataset));
}

Result<DatasetId> DatasetHub::addWithStableId(Dataset dataset, DatasetId id)
{
    // 重复 id 的错误文案在领域层保持旧口径（TypedRegistry 通用版只兜底）。
    if (TypedRegistry::get(id) != nullptr)
        return fail(err::DupId, QStringLiteral("数据集 id 已存在: %1").arg(id));
    return TypedRegistry::addWithStableId(std::move(dataset), id);
}

bool DatasetHub::remove(DatasetId id)
{
    if (!TypedRegistry::remove(id))
        return false;
    Q_EMIT datasetRemoved(id);
    return true;
}

const Dataset *DatasetHub::get(DatasetId id) const
{
    return TypedRegistry::get(id);
}

Dataset *DatasetHub::getMutable(DatasetId id)
{
    return TypedRegistry::get(id);
}

QVector<DatasetId> DatasetHub::ids() const
{
    QVector<DatasetId> result = TypedRegistry::ids();
    std::sort(result.begin(), result.end());
    return result;
}

void DatasetHub::clear()
{
    const QVector<DatasetId> all = TypedRegistry::ids(); // 插入序（旧口径）
    TypedRegistry::clear();
    for (DatasetId id : all)
        Q_EMIT datasetRemoved(id);
}

} // namespace nextpilot::plotscope::dataset

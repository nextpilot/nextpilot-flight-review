#pragma once

#include "Dataset.h"
#include "Registry.h"
#include "Result.h"

#include <QObject>
#include <QVector>

namespace nextpilot::plotscope::dataset
{

using nextpilot::plotscope::foundation::Result;

// Dataset 的唯一所有者。模块与扩展都不拥有数据，只通过 repo 访问。
// 模块卸载时数据不受影响——这是"装卸不牵连"的根本保证。
class DatasetRegistry : public QObject
{
    Q_OBJECT
public:
    explicit DatasetRegistry(QObject *parent = nullptr);

    DatasetId add(Dataset dataset);
    Result<DatasetId> addWithId(Dataset dataset, const QString &requestedName);

    // 以显式稳定 id 存入：录制/回放时意图携带同一个 id，避免会话内自增 id 漂移。
    // 若 id 已被占用则失败。会同步推进 m_nextId 防止后续自增冲突。
    Result<DatasetId> addWithStableId(Dataset dataset, DatasetId id);

    bool remove(DatasetId id);
    const Dataset *get(DatasetId id) const;
    // 可变访问：仅限宿主级"数据集维护"操作——增量派生刷新前的 refreshSchema、
    // 录制回放的 appendFrame 等（Dataset 是共享句柄，可变方法作用于 kernel 本体）。
    Dataset *getMutable(DatasetId id);
    QVector<DatasetId> ids() const;
    int count() const;

    void clear();

Q_SIGNALS:
    void datasetAdded(DatasetId id);
    void datasetRemoved(DatasetId id);

private:
    // 容器本体委托泛型注册表；本壳只保留领域语义：id 分配、生命周期信号。
    nextpilot::plotscope::registry::Registry<Dataset, DatasetId> m_datasets;
    DatasetId m_nextId = 0;
};

} // namespace nextpilot::plotscope::dataset

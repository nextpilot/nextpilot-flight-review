#pragma once

#include "Dataset.h"
#include "RegistryTree.h"
#include "Result.h"
#include "TypedRegistry.h"

#include <QObject>
#include <QVector>

namespace nextpilot::plotscope::dataset
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::registry::RegistryTree;
using nextpilot::plotscope::registry::TypedRegistry;

// Dataset 的唯一所有者。模块与扩展都不拥有数据，只通过 repo 访问。
// 模块卸载时数据不受影响——这是"装卸不牵连"的根本保证。
//
// M2.8 R2 瘦身：壳改为 TypedRegistry<Dataset, DatasetId> 薄壳继承——
// 通用增删查/id 分配/基类信号（objectAdded/objectRemoved）上收，本壳只留领域语义：
// datasetAdded/datasetRemoved 具名信号（旧测试零改动）、ids() 升序、addWithId 名字语义。
class DatasetHub : public TypedRegistry<Dataset, DatasetId>
{
    Q_OBJECT
public:
    // 组合根/单测统一用：构造即上树（不存在不上树形态，M2.8 遗留2 收口拍板）。
    explicit DatasetHub(RegistryTree &tree, QObject *parent = nullptr);

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
    // 升序（TypedRegistry ids() 是插入序，本壳保留旧排序口径，测试零改动）。
    QVector<DatasetId> ids() const;

    void clear();

Q_SIGNALS:
    void datasetAdded(DatasetId id);
    void datasetRemoved(DatasetId id);
};

} // namespace nextpilot::plotscope::dataset

#pragma once

#include "Dataset.h"
#include "Result.h"

#include <QHash>
#include <QObject>
#include <QVector>

namespace ng::core
{

// Dataset 的唯一所有者。模块与扩展都不拥有数据，只通过 repo 访问。
// 模块卸载时数据不受影响——这是"装卸不牵连"的根本保证。
class DataRepository : public QObject
{
    Q_OBJECT
public:
    explicit DataRepository(QObject *parent = nullptr);

    DatasetId add(Dataset dataset);
    Result<DatasetId> addWithId(Dataset dataset, const QString &requestedName);

    // 以显式稳定 id 存入：录制/回放时意图携带同一个 id，避免会话内自增 id 漂移。
    // 若 id 已被占用则失败。会同步推进 m_nextId 防止后续自增冲突。
    Result<DatasetId> addWithStableId(Dataset dataset, DatasetId id);

    bool remove(DatasetId id);
    const Dataset *get(DatasetId id) const;
    QVector<DatasetId> ids() const;
    int count() const;

    void clear();

Q_SIGNALS:
    void datasetAdded(DatasetId id);
    void datasetRemoved(DatasetId id);

private:
    QHash<DatasetId, Dataset> m_datasets;
    DatasetId m_nextId = 0;
};

} // namespace ng::core

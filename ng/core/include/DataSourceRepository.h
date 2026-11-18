#pragma once

#include "DataSource.h"
#include "Result.h"

#include <QHash>
#include <QObject>
#include <QVector>

namespace ng::core
{

// 数据源登记表：**运行时实例**的容器。
//
// 架构定位（见 ARCHITECTURE.md §3）：
//   - 解析器、录制格式是**知识资产**，进程内一份，由注册表（extension 提供）持有；
//   - 数据集是**产物**，由 Rust 数据仓库持有；
//   - 数据源是**运行时实例**，持有「来源 + 通道→解析器 的绑定 + 产出的数据集」。
//
// 本类只登记实例与它们的绑定关系，不拥有解析器、不拥有数据集、不碰字节。
// 这样一个数据源用哪个解析器，是**引用**资产而非复制资产——
// 同一份 ULog 解析逻辑被 N 个数据源共享，状态互不干扰（解析器实例是每次打开时新建的）。
//
// 需求「同时打开多个数据源」的直接落点：
//   每个数据源有稳定 id，各有独立状态与推进节奏，互不牵连。
class DataSourceRepository : public QObject
{
    Q_OBJECT
public:
    explicit DataSourceRepository(QObject *parent = nullptr);

    // 登记一个新数据源（分配 id）。
    int add(DataSourceInfo info);

    // 以显式 id 登记（Job 回放需要稳定 id）。
    Result<int> addWithStableId(DataSourceInfo info, int id);

    // 更新整体描述（保留 id）。
    bool update(int id, const DataSourceInfo &info);

    // 仅更新状态与错误（高频路径）。
    bool setState(int id, SourceState state, const QString &error = QString());

    // 绑定/解绑解析器（**扇出**：一个源可同时绑定多个解析器）。
    //
    // 语义（需求）：「数据解析，一般只能选择一个前端（文件或流），但是可以
    //   选择多种解析器……也就是将这份数据同时喂给多个 parse」。
    // 所以绑定以 **parserId 为键**（同解析器重复绑定 = 改配置），
    // topic 降级为可选过滤器，不再是路由键。
    bool bindParser(int id, const ParserBinding &binding);
    bool unbindParser(int id, const QString &parserId);
    const ParserBinding *parserFor(int id, const QString &parserId) const;

    // 该源绑定的全部解析器（扇出的各路）。
    QVector<ParserBinding> parsersOf(int id) const;

    // 登记该数据源产出的数据集。
    bool addDataset(int id, int datasetId);
    bool removeDataset(int id, int datasetId);

    // 移除数据源。返回它产出的数据集 id（调用方负责清理）。
    bool remove(int id, QVector<int> *datasetIds = nullptr);

    const DataSourceInfo *get(int id) const;
    // 反查：某个数据集属于哪个源（数据集 id → 源 id）。
    //
    // 为什么需要：源与数据集是**两套 id 空间**，一个源可扇出多个解析器、
    // 产出多个数据集。用户点了某张表，UI 需要知道该去重载哪个源。
    // 没有反查就只能让调用方遍历比对——而这里是最该提供该关系的地方。
    // 找不到时返回 -1。
    int sourceOfDataset(int datasetId) const;
    // 按来源标识（文件路径/端点）反查源 id。恢复重放时按配置里的 origin
    // 找回刚载入的源，再取其数据集去重放派生配方。找不到返回 -1。
    int sourceByOrigin(const QString &origin) const;
    QVector<int> ids() const;
    int count() const;
    void clear();

Q_SIGNALS:
    void sourceAdded(int id);
    void sourceRemoved(int id);
    void sourceChanged(int id);

private:
    QHash<int, DataSourceInfo> m_sources;
    int m_nextId = 1;
};

} // namespace ng::core

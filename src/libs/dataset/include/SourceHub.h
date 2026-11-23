#pragma once

#include "DataSource.h"
#include "Registry.h"
#include "RegistryNode.h"
#include "RegistryTree.h"
#include "Result.h"

#include <QObject>
#include <QStringList>
#include <QVector>

namespace nextpilot::plotscope::dataset
{

using nextpilot::plotscope::foundation::Result;

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
// 每个数据源有稳定 id，各有独立状态与推进节奏，互不牵连；
// 多源打开即多实例，源间无共享可变状态。
class SourceHub : public nextpilot::plotscope::registry::RegistryNode
{
    Q_OBJECT
public:
    // 构造即上树（kind=Source，name=数据源；M2.8 遗留5：推迟池收口）。
    explicit SourceHub(nextpilot::plotscope::registry::RegistryTree &tree,
                            QObject *parent = nullptr);

    // ---- RegistryNode 可观测（diag.registries 用）----
    int count() const override { return m_sources.count(); }
    // keys = 各源的显示名（与树上 dataset 的 id 串口径不同：源以名字给人看）。
    QStringList keysAsStrings() const override;

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
    void clear();

    // ---- origin 需求引用（demand reference，对标 PJ4 PendingDisplayBinder 语义）----
    //
    // 为什么需要：恢复布局时数据源可能还没到（换文件前先加载 layout、live 流未连），
    // pending binder 在工作区挂起等待。这些挂起条目对"某个 origin 的源必须存在"
    // 构成**需求**——语义上等价于 PJ4 的 demand reference：谁持有需求，谁声明
    // "这个源来了我要接上它，别把它当无人引用的资源丢弃/清走"。
    //
    // 我们的形态与 PJ4 不同：源是被动接收 + 建流即登记，不存在"不订阅就不发"的
    // quiet stream，所以 demand 不驱动订阅，而是提供三件事：
    //   1. 诊断口径：demandedOrigins() 报告尚未被满足的需求（preflight 用）；
    //   2. 生命周期锚点：源被显式移除后，需求仍在——同 origin 的源再次登记
    //      （重新载入同一文件 / 重连同一端点）时 pending 自动接回；
    //   3. 未来接入资源预算时的保留依据（当前 clearCache 只清磁盘缓存，不驱逐源）。
    //
    // 计数语义：同一 origin 可被多个 pending 画布需求，按引用计数；release 减到 0 即消除。
    void demandOrigin(const QString &origin);
    void releaseOrigin(const QString &origin);
    // 该 origin 当前被需求次数（0 = 无需求）。
    int demandCount(const QString &origin) const;
    // 全部被需求的 origin（含已满足与未满足的，按登记序）。
    QStringList demandedOrigins() const;
    // 被需求但当前没有对应源的 origin（preflight/诊断用）。
    QStringList unsatisfiedDemands() const;

Q_SIGNALS:
    void sourceAdded(int id);
    void sourceRemoved(int id);
    void sourceChanged(int id);

private:
    // 容器本体委托泛型注册表（保序：登记顺序即遍历顺序）；本壳只保留领域语义：
    // id 分配、变更信号、解析器绑定关系、dataset 反查。
    nextpilot::plotscope::registry::Registry<DataSourceInfo, int> m_sources;
    // origin → 需求引用计数（demand reference，保序便于诊断输出稳定）。
    // 用 QVector<pair> 而非 QHash：需求集合通常很小，保序比 O(1) 查找重要。
    QVector<QPair<QString, int>> m_demands;
    int m_nextId = 1;
};

} // namespace nextpilot::plotscope::dataset

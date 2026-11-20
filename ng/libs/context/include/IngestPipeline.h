#pragma once

#include "DataSource.h"
#include "DatasetRegistry.h"
#include "Result.h"
#include "ps_parser_spi.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <vector>

namespace nextpilot::plotscope::context
{

using nextpilot::plotscope::dataset::DatasetRegistry;
using nextpilot::plotscope::dataset::DataSourceInfo;
using nextpilot::plotscope::dataset::ParserBinding;
using nextpilot::plotscope::dataset::DatasetId;
using nextpilot::plotscope::dataset::kInvalidDataset;
using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;


// 摄取管道：**一个数据源（单前端）的字节流 → 多个解析器 → 多个数据集**。
//
// 需求原文：
//   「数据解析，一般只能选择一个前端（文件或流），但是可以选择多种解析器
//     （因为源，比如 udp 中可能包含多种格式数据），也就是将这份数据同时喂给
//     多个 parse，出的结果落库 dataset。」
//
// 这条需求决定了管道形态：
//
//   字节流 ──┬──▶ parser A（实例 a1）──▶ dataset A
//            ├──▶ parser B（实例 b1）──▶ dataset B
//            └──▶ parser C（实例 c1）──▶ dataset C
//
// 三个要点：
//  1. **扇出而非分流**：同一份字节喂给所有绑定的解析器，不是"按 topic 分给一个"。
//     每个解析器自己 sniff / 识别自己关心的部分——因为一个 udp 源里可能同时
//     混着多种格式（MAVLink 与 GNSS NMEA 交织）。
//  2. **实例隔离**：每个 (数据源, 解析器) 组合一个独立的解析器实例（create/destroy），
//     状态互不干扰；同一份 ULog 解析逻辑被 N 个数据源共享的是**代码**，不是状态。
//  3. **产出隔离**：每个解析器的帧落到**自己的数据集**，不混表、不互相覆盖。
//
// 内存纪律：C++ 侧只保留"一个批次缓冲"，不物化全量；帧立即 appendFrame 进 Rust。
class IngestPipeline : public QObject
{
    Q_OBJECT
public:
    // 一个解析器通道：一个解析器实例 + 它自己的 sink + 它产出的数据集。
    struct ParserChannel
    {
        QString parserId;
        QString topicFilter;       // 可选通道过滤（空 = 全部）
        const PsParserVtable *vtable = nullptr;
        void *ctx = nullptr;       // 解析器实例（opaque）
        int datasetId = -1;        // 该通道产出落到的数据集
        bool active = true;
        QString error;             // 最近一次错误
    };

    explicit IngestPipeline(DatasetRegistry &repo, QObject *parent = nullptr);
    ~IngestPipeline() override;

    // 绑定一个解析器到指定数据源（扇出的一路）。
    // 绑定即创建独立实例与**独立数据集**，数据集立即登记进仓库。
    //
    // preferredDatasetId != kInvalidDataset 时，数据集以该**稳定 id** 入仓
    // （录制/回放需要：会话内自增 id 会漂移，脚本引用就断了）。
    Result<int> bindParser(int sourceId, const ParserBinding &binding,
                           DatasetId preferredDatasetId = kInvalidDataset);

    // 为某一路指定**表名改写**（宿主策略）。
    //
    // 为什么需要：SPI 解析器只认字节，不知道"这张表该叫什么"——
    // 表名对 CSV 是"文件名或 intent 参数"，对 ULog 是"topic 名"。
    // 前者是展示策略（归扩展层声明），后者由解析器给出。
    //
    // 语义：把该通道产出的**所有**表都改名为 newName。
    // 于是宿主必须只在**单表格式**上声明它（CSV）——
    // 多表格式（ULog 的多 topic）不声明，表名仍由解析器按内容决定。
    //
    // 为什么不写成"from -> to"的字符串配对：那要求宿主知道解析器内部
    // 用什么表名，解析器一改内部名，宿主的映射就静默失效。
    // 而"单表格式由宿主命名"是**格式的稳定属性**，不会随实现变。
    //
    // 为什么必须在这里（appendFrame 时）改：Rust 侧的表名在数据写入
    // 时就定死了，没有事后改名的 API。
    void setTableRename(int sourceId, const QString &parserId, const QString &newName);

    // 解除绑定（销毁实例；数据集本身仍由仓库持有，可另行清理）。
    bool unbindParser(int sourceId, const QString &parserId);

    // 喂字节：同时推给该源所有活跃解析器。
    // 返回消费的字节数；任一解析器报错时只记在该通道上并继续其它通道
    // （一个格式坏了不该拖垮整条源）。
    qint64 feed(int sourceId, const uint8_t *data, qint64 size);

    // 收尾：让所有解析器 flush 残留（文件尾部半帧等）。
    void finish(int sourceId);

    // 该源当前的解析器通道列表。
    QVector<ParserChannel> channels(int sourceId) const;
    int channelCount(int sourceId) const;

    // 诊断：某通道最近一次错误。
    QString channelError(int sourceId, const QString &parserId) const;

private:
    // sink 回调：把一批行落进该通道自己的数据集。
    static int32_t onFrameBatch(void *ctx, const PsFrameBatch *batch);
    static void onProgress(void *ctx, uint64_t done, uint64_t total);

    // sink 回调的旁路上下文：定位到具体通道与它的数据集。
    struct SinkContext
    {
        IngestPipeline *pipeline = nullptr;
        int sourceId = -1;
        QString parserId;
        int datasetId = -1;
        bool dirty = false;   // 有新数据灌入，待收尾时刷新 schema
        // 表名改写（宿主策略）：该通道所有表都用这个名字。
        // 空 = 沿用解析器给出的表名。
        QString tableName;
    };

    DatasetRegistry &m_repo;
    QHash<int, QVector<ParserChannel>> m_channels; // sourceId -> 通道
    // 每个通道一个 sink 上下文（与通道同寿命）。
    // 用 std::map 而非 QHash：QHash 的值类型不支持 move-only（Qt6.2 拷贝语义），
    // 而 unique_ptr 必须是 move-only。
    std::map<int, std::vector<std::unique_ptr<SinkContext>>> m_sinkCtx;
};

} // namespace nextpilot::plotscope::context

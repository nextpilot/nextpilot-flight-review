#pragma once

#include "DataRepository.h"
#include "DataSourceRepository.h"
#include "IByteSource.h"
#include "IngestPipeline.h"
#include "ParserRegistry.h"
#include "Result.h"

#include <QString>

namespace ng::core
{

// 摄取编排器：**读字节 -> 扇出给解析器 -> 落库**。
//
// 架构定位（ARCHITECTURE.md §3.3 / AGENTS.md §3 P0-1）：
//   需求原文：
//     「数据解析，一般只能选择一个前端（文件或流），但是可以选择多种解析器
//       （因为源，比如 udp 中可能包含多种格式数据），也就是将这份数据同时喂给
//       多个 parse，出的结果落库 dataset。」
//
// 于是"读字节"这件事必须**只做一次**，且由宿主统一编排：
//
//   ┌── IngestOrchestrator ────────────────────────────────────┐
//   │ 1) 打开字节来源（文件 / 未来：UDP、串口）                 │
//   │ 2) 建 DataSource（运行时实例：来源 + 解析绑定 + 数据集）  │
//   │ 3) 按绑定逐个创建解析器实例（扇出）                        │
//   │ 4) 循环：read 一块 -> feed 给所有活跃通道                 │
//   │ 5) finish：冲刷残留（文件尾半帧、索引表、参数段）          │
//   │ 6) 收尾：刷新 schema 投影，返回数据集 id                   │
//   └──────────────────────────────────────────────────────────┘
//
// 与旧通路的区别：
//   过去每个格式驱动自己 QFile 自己读自己写 Dataset，于是
//   每加一个格式要写一份读文件的代码，且录制旁路覆盖不到它们。
//   现在格式知识归解析器（SPI），编排归宿主，两者都不重复。
//
// 内存纪律：常驻内存 = 一个读取缓冲 + 每个解析器一批，与文件大小无关。
class IngestOrchestrator
{
public:
    IngestOrchestrator(DataRepository &repo, DataSourceRepository &sources,
                       IngestPipeline &pipeline);

    // 打开一个文件源，按 parserIds 绑定解析器，读完全部字节。
    //
    // parserIds 为空时，按 path 的后缀自动选择一个解析器
    // （后缀匹配 -> 内容嗅探），这正是"打开一个 .ulg 就自动解析"的路径。
    //
    // 返回主数据集 id（第一个解析器产出）。失败时 err 说明原因。
    Result<int> openFile(const QString &path, const QStringList &parserIds = QStringList(),
                         const QString &topicFilter = QString());

    // 从任意字节来源载入（文件 / 流 / 内存片段共用同一编排）。
    // source 由调用方持有，其生命周期须覆盖本调用。
    Result<int> openSource(IByteSource &source, const QStringList &parserIds,
                            const QString &topicFilter = QString(),
                            const QString &origin = QString());

    // 上一次 open 产出的**全部**数据集 id（扇出的各路）。
    const QVector<int> &datasetIds() const { return m_datasetIds; }

    // 上一次 open 使用的源 id。
    int sourceId() const { return m_sourceId; }

private:
    // 打开 + 绑定 + 读取 + 收尾的共同实现。
    Result<int> ingest(IByteSource &source, const QStringList &parserIds,
                       const QString &topicFilter, const QString &origin);

    DataRepository &m_repo;
    DataSourceRepository &m_sources;
    IngestPipeline &m_pipeline;
    int m_sourceId = -1;
    QVector<int> m_datasetIds;
};

} // namespace ng::core

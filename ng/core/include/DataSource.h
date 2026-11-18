#pragma once

#include "Source.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace ng::core
{

// 广义完整的数据源：**来源 + 解析绑定 + 录制配置 + 数据集**。
//
// 需求原文：「一个广义完整的 datasource，一般是包含文件或者流 + parser + dataset」。
//
// 架构判断：这三段聚合成**一个用户面对的概念**，但聚合的是"运行时绑定关系"，
// 不是"资产归属"：
//
//   ┌──────────── DataSource (id) ────────────┐
//   │ ① 来源：文件或流（字节从哪里来）——前端唯一 │
//   │ ② 解析绑定：哪几个解析器并行吃这份字节    │
//   │ ③ 录制配置：格式×后端 的多路组合（可空）   │
//   │ ④ 数据集：各解析器产出的结果（Rust 拥有）  │
//   └──────────────────────────────────────────┘
//
// 关键语义（需求明确）：
//   - **前端唯一**：一个数据源只有一个字节来源（文件或流），不混。
//   - **解析器多个**：同一份字节同时喂给多个解析器（udp 里可能含多种格式数据），
//     每个解析器独立实例、独立产出自己的数据集。
//   - **录制多路**：格式（raw/mcap）× 后端（文件/udp）正交组合，可同时开多路。
//
// 三段的**就绪是渐进的**，这正是 sniff 存在的意义：
//   刚打开        → 只有 ①（还不知道是什么格式）
//   嗅探/选定后   → ① + ②（知道喂给谁了）
//   解析后        → ① + ② + ④（拿到了数据）
struct DataSourceInfo
{
    int id = -1;
    QString name;                       // 显示名
    QString origin;                     // 文件路径或端点
    SourceKind kind = SourceKind::File; // 文件 / 流 / 实时 / 回放

    // ① 来源
    SourceState state = SourceState::Closed;
    QString error;                      // 失败原因（state=Error 时有效）
    qint64 bytesTotal = 0;              // 0 = 不确定
    qint64 bytesRead = 0;

    bool replayable = false;            // 原始流可被重放

    // ② 解析绑定（扇出：同一份字节喂给多个解析器）
    QVector<ParserBinding> parsers;     // 该源并行使用的解析器
    QString formatHint;                 // 用户指定的格式（可空，仅作默认选择提示）

    // ③ 录制配置（格式 × 后端的正交组合，可多路同时）
    QVector<RecordingTarget> recordings;

    // ④ 数据集（每个解析器产出一个）
    QVector<int> datasetIds;            // 该源解析产出的数据集 id
};

} // namespace ng::core

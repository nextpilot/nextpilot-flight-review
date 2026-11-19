#pragma once

// 工作区数据源配置的**可序列化描述**——"上次这个工作区开了哪些源、每个源怎么配的"。
//
// 归属：SettingsStore 的 Workspace 作用域（会话可恢复，不随 Document 分享）。
// 单一职责：只描述"怎么做"，绝不持有数据本体。数据始终在 Rust PsDataset 里。
//
// 为什么是**一个原子 JSON 块**而不是 8 个散键：
//   一次 data.load 是"用这套参数打开这个源"的原子动作。若把文件/解析器/参数/
//   录制/平移拆成独立键，改一个源时会错配到上一个源的参数——这是真实 bug。
//   故整份申请单作为一个 QJsonArray 存到 ws/data/sources。
//
// 恢复语义 = **重放**，不是"载入快照"：
//   · 来源/解析器/参数 → 重新发 data.load，重新走摄取管道；
//   · 衍生配方        → 对每个重新发 data.derive，由 Rust 实时重算；
//   · 时间轴平移      → 作为 spec 字段传给画布（不改源数据）。
// 于是不管配置还是数据，settings 里都只有"配方"，没有百万行数组。
//
// 稳定性：衍生配方用 (sourceOrigin, table, column) 定位通道，**不用会话期 datasetId**
// ——后者每次打开都可能变，存下来重放就指错了。sourceOrigin 由数据集所属源反查得到。

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace nextpilot::plotscope::context
{


class SettingsStore;

// 一条派生配方：对某源的某列施加一个 Rust 算子。
// 恢复时重放为 data.derive 意图（参数一一对应），结果由 Rust 算、不写回数据集。
struct DeriveRecipeSpec
{
    QString sourceOrigin;   // 所属源的 origin（稳定），用于重放时反查 datasetId
    QString table;
    QString column;
    QString filter;         // ps-ops::filters 的算子 id
    QString paramsJson;     // 算子参数对象 JSON（空 = {}）
    QString name;           // 派生序列展示名（可选）

    QJsonObject toJson() const;
    static DeriveRecipeSpec fromJson(const QJsonObject &o);
    bool isValid() const;
};

// 一份数据源申请单：把"打开一个源所需的一切"打成一个包。
struct SourceSpec
{
    int schemaVersion = 1;

    // ① 来源：文件或流。origin 是文件路径或端点；kind 对齐 SourceKind 的字符串。
    QString origin;
    QString kind = QStringLiteral("file"); // file / stream / live / replay

    // ② 解析：解析器 id 列表（扇出）+ 按 parserId 分组的参数 JSON + 可选 topic 过滤。
    //    参数形如 { "ulog": {"...":...}, "csv": {"delimiter":","} }。
    QStringList parserIds;
    QJsonObject parserParams;
    QString topicFilter;

    // ③ 录制：格式 × 后端的正交组合（可多路），字段对齐 RecordingTarget。
    QJsonArray recordings; // 每项 {formatId, backendId, endpoint, enabled}

    // ④ 时间轴平移（秒）：显示时间 = 原始时间 - offset。视图层用，不改数据。
    double timeOffsetSec = 0.0;

    // ⑤ 衍生配方（只存"怎么做"，恢复时重算）。
    QVector<DeriveRecipeSpec> deriveRecipes;

    QJsonObject toJson() const;
    static SourceSpec fromJson(const QJsonObject &o);
    bool isValid() const;
};

// 整份工作区数据源配置：SourceSpec 列表。
class WorkspaceDataConfig
{
public:
    static constexpr int kSchemaVersion = 1;
    // settings 里的相对键（Workspace 作用域下完整键为 "ws/data/sources"）。
    static constexpr const char *kSettingsKey = "data/sources";

    QVector<SourceSpec> sources;

    QJsonObject toJson() const;
    static WorkspaceDataConfig fromJson(const QJsonObject &o);

    // 从 SettingsStore 读写（自动用 Workspace 作用域 + setJson）。
    static WorkspaceDataConfig load(const SettingsStore &store);
    bool save(SettingsStore &store, QString *err = nullptr) const;

    // 按 origin 找申请单（恢复时按源定位）。找不到返回 nullptr。
    const SourceSpec *findByOrigin(const QString &origin) const;
    SourceSpec *findByOrigin(const QString &origin);
    void upsert(const SourceSpec &spec);
    bool removeByOrigin(const QString &origin);
};

} // namespace nextpilot::plotscope::context
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using context::DeriveRecipeSpec;
using context::SourceSpec;
using context::WorkspaceDataConfig;
} // namespace nextpilot::plotscope::core


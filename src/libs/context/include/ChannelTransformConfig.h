#pragma once

// 通道级变换配方的**可序列化描述**——"这个通道的所有读数都要施加这个变换"。
//
// 归属：SettingsStore 的 Workspace 作用域（会话可恢复）。
// 语义（变换 vs 衍生的裁决）：
//   · 变换是**非破坏视图**——原始 dataset 永不改动，字段名不变；
//     plot 画布、data.series、导出等所有读数后端统一生效。
//   · plot 级（曲线 yTransformId）是**视图局部**配方，只影响当前曲线，
//     存在 AxesView 的系列配方里，与本配置无关。
// 定位键用 sourceOrigin（稳定）+ table + column——datasetId 会话间会变，
// 持久化用会话期 id 重放必错（与 WorkspaceDataConfig 同一条裁决）。

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace nextpilot::plotscope::context
{

class SettingsStore;

// 一条通道变换配方。
struct ChannelTransformSpec
{
    QString origin;         // 数据源 origin（文件路径/端点，稳定）
    QString table;
    QString column;
    QJsonObject transform;  // TransformRequest JSON（descriptorId + kind + params）

    bool isValid() const;
    QJsonObject toJson() const;
    static ChannelTransformSpec fromJson(const QJsonObject &o);
};

// 整份通道变换配置。
class ChannelTransformConfig
{
public:
    // settings 里的相对键（Workspace 作用域下完整键为 "ws/data/transforms"）。
    static constexpr const char *kSettingsKey = "data/transforms";

    QVector<ChannelTransformSpec> specs;

    static ChannelTransformConfig load(const SettingsStore &store);
    bool save(SettingsStore &store, QString *err = nullptr) const;

    QJsonObject toJson() const;
    static ChannelTransformConfig fromJson(const QJsonObject &o);

    // 精确查找（origin+table+column）。找不到返回 nullptr。
    const ChannelTransformSpec *find(const QString &origin, const QString &table,
                                     const QString &column) const;
    // 幂等 upsert（同通道覆盖旧配方）。
    void upsert(const ChannelTransformSpec &spec);
    // 移除通道配方。返回是否确实移除了条目。
    bool removeByChannel(const QString &origin, const QString &table, const QString &column);
};

} // namespace nextpilot::plotscope::context

#pragma once

// 派生序列注册表：data.derive 产出的"独立派生字段"的宿主侧账本。
//
// 裁决语义：派生序列**不写回原始 dataset**——原始数据零污染。派生结果是
// 宿主层的独立序列（name → 来源配方 + t/v 结果），所有读数后端按 name
// 访问：data.series / plot.channel 以派生名穿透，数据树经 data.derive.list
// 展示。配方随 WorkspaceDataConfig::DeriveRecipeSpec 持久化（重放即重建），
// 本注册表只管**会话期**的已算结果与可发现性。
//
// MIMO（如 quat_to_rpy）一次派生产出多条序列，每条独立登记。

#include <QByteArray>
#include "Registry.h"
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <limits>

namespace nextpilot::plotscope::context
{

class DerivedSeriesRegistry final : public QObject
{
    Q_OBJECT
public:
    struct Entry
    {
        QString name;        // 展示名/访问名（如 "demo_flight.att.roll[rpy]"）
        int datasetId = -1;  // 来源数据集（重放用）
        QString table;
        QString column;      // 来源主输入列（SISO）；MIMO 存第一个输入
        QString filterId;    // 派生算子（SISO）或 "quat_to_rpy"（MIMO）
        QJsonObject transform;     // 统一变换请求（SISO transform 路径时非空）
        QJsonObject paramsJson;    // 算子参数（SISO Rust 路径时非空）
        QVector<double> t;   // 结果缓存（秒）
        QVector<double> v;   // 结果缓存（值）

        // === 增量刷新（MIMO 流式派生，对齐 PJ4 增量引擎的水位思想）===
        // 配方指纹：inputs/interp/degrees/unwrap 的序列化。同名重发 data.derive
        // 时比对——一致走增量（只算 [waterUs, now] 新段），不一致自动回全量。
        QByteArray recipe;
        // 基准轴水位（微秒，UTC）：已处理到的最大基准轴时间戳。
        // INT64_MIN = 尚未增量过（全量口径）。输出轴以基准轴为准，水位只属基准。
        qint64 waterUs = std::numeric_limits<qint64>::min();
    };

    explicit DerivedSeriesRegistry(QObject *parent = nullptr);

    // 登记派生序列。同名覆盖（重复派生同一配方 = 刷新缓存）。返回最终 name。
    QString registerSeries(const Entry &entry);

    // 增量追加（MIMO 流式派生）：向同名 entry 的 t/v 缓存追加新段并更新水位。
    // 找不到同名 entry 返回 false（调用方应回全量登记）。
    bool appendSeries(const QString &name, const QVector<double> &tNew,
                      const QVector<double> &vNew, qint64 waterUs);

    // 按名取（穿透读数的入口）。找不到返回 nullptr。
    const Entry *get(const QString &name) const;

    // 删除（数据集移除时同步清理）。返回是否确实删除。
    bool remove(const QString &name);
    // 删除某数据集的全部派生序列。
    int removeAllForDataset(int datasetId);

    QStringList names() const;
    QVector<Entry> all() const;
    int count() const { return m_entries.count(); }

Q_SIGNALS:
    // 任何登记/删除后发出（数据树/图例刷新用）。
    void changed();

private:
    // 容器本体委托泛型注册表（保序：登记顺序即 derive.list 展示顺序）；
    // 本壳只保留领域语义：同名覆盖刷新缓存、追加缓存推进水位、按 dataset 级联清理。
    nextpilot::plotscope::registry::Registry<Entry, QString> m_entries;
    int indexOf(const QString &name) const;
};

} // namespace nextpilot::plotscope::context

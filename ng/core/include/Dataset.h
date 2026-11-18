#pragma once

// Dataset 是 Rust kernel PsDataset 的薄 C++ RAII 门面。
// 数据本体（typed 列、null 位图、共享时间轴、ring 预算）全部由 Rust 持有与管理，
// C++ 侧只保留轻量 schema 投影（表名/列名/类型/时间轴视图），不复制列值。
// 目的：内存安全（Rust 管理 + panic 不跨 FFI）、高性能（列式 + LOD 抽稀），
// 并为缓存（ps-cache）、抽稀/滤波/FFT（ps-ops）统一留出入口。

#include "ps_kernel.h"

#include <QJsonObject>
#include <QSharedDataPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <memory>
#include <optional>

namespace ng::core
{

using DatasetId = int;
constexpr DatasetId kInvalidDataset = -1;

// 时间轴统一 UTC 微秒，与 kernel ps_ndf::typed 的 i64 微秒一致。
// 用 int64_t（而非 qint64=long long）以匹配 C ABI 的 int64_t 指针签名。
using TimeUs = int64_t;

// 列值类型，数值与 kernel 的 PsValueType / ValueType 完全一致（可直接作为 ABI 码）。
enum class ValueType : int
{
    F64 = PS_VALUE_F64,
    F32 = PS_VALUE_F32,
    I64 = PS_VALUE_I64,
    U64 = PS_VALUE_U64,
    I32 = PS_VALUE_I32,
    U32 = PS_VALUE_U32,
    I16 = PS_VALUE_I16,
    U16 = PS_VALUE_U16,
    I8 = PS_VALUE_I8,
    U8 = PS_VALUE_U8,
    Bool = PS_VALUE_BOOL,
    Text = PS_VALUE_TEXT
};

const char *valueTypeName(ValueType type);
bool valueTypeIsNumeric(ValueType type);
int valueTypeBytes(ValueType type); // Text 变长返回 0

// 列 schema：只描述，不拥有数据。真正的值在 Rust PsDataset 的 typed 列里。
struct Column
{
    QString name;
    QString unit;
    ValueType type = ValueType::F64;
    int rowCount = 0;
};

// 表 schema：表名 + 共享时间轴视图（微秒）+ 列描述。
// timeUs 是从 Rust 投影出的时间视图（供光标/范围显示），数据本体仍归 Rust。
struct Table
{
    QString name;
    QVector<TimeUs> timeUs; // 单调非降
    QVector<Column> columns;

    int rowCount() const { return timeUs.size(); }
    int columnIndex(const QString &name) const;
    const Column *column(const QString &name) const;
};

// 通道视图：把 (table, column) 投影成绘图用的 (t_sec, value) f64 序列。
// 由 Dataset::channel() 现场调用 ps_dataset_read_window 从 Rust 读取。
struct ChannelView
{
    QString table;
    QString column;
    QString name; // 展示名："table.column"
    QString unit;
    ValueType type = ValueType::F64;
    QVector<double> time;  // 秒
    QVector<double> value;

    bool empty() const { return time.isEmpty(); }
    int size() const { return qMin(time.size(), value.size()); }
};

// 视口抽稀结果：一个 (x, min, max) 三元组，来自 kernel 的 LOD 分桶。
struct LodTriple
{
    double x = 0.0;
    double min = 0.0;
    double max = 0.0;
};

// 统计结果，镜像 kernel PsStats。
struct ColumnStats
{
    quint32 count = 0;
    double mean = 0.0;
    double stdDev = 0.0;
    double min = 0.0;
    double max = 0.0;
    double rms = 0.0;
    double median = 0.0;
};

// Dataset = Rust PsDataset 的共享所有权句柄 + C++ schema 投影。
// 不可变快照语义：填充在构造期完成，填充后视为只读（Rust 侧仍可安全并发读）。
class Dataset
{
public:
    Dataset();
    explicit Dataset(const QString &name);
    Dataset(const Dataset &other);
    Dataset &operator=(const Dataset &other);
    ~Dataset();

    // 新建一个空的 Rust 数据集（内部 ps_dataset_new）。
    static Dataset create(const QString &name, const QString &format);
    // 接管一个已由 Rust 构造/缓存还原的数据集句柄（取得所有权）。
    static Dataset adopt(PsDataset *native, const QString &name, const QString &format);

    // 底层 Rust 句柄，仅供门面与扩展通过 C ABI 访问；nullptr 表示空壳。
    PsDataset *nativeHandle() const;
    bool isNull() const;

    QString name() const;
    QString format() const;
    void setFormat(const QString &format);

    // schema 查询（来自构造期投影）
    int tableCount() const;
    const Table *table(int index) const;
    const Table *table(const QString &name) const;
    int tableIndex(const QString &name) const;
    QStringList tableNames() const;

    // 构造期填充：直接写 Rust typed 表。返回 -1 表示失败。
    // timeUs 为 UTC 微秒共享时间轴（kernel 表级时间轴，只复制一次）。
    int addTable(const QString &name, const QVector<TimeUs> &timeUs);
    int addTable(const QString &name, const QVector<double> &timeSec);
    // F64 列便捷入口（CSV/多数遥测列）。nulls 为可选逐行标记。
    bool addF64Column(const QString &table, const QString &name, const QString &unit,
                      const QVector<double> &values, const QVector<bool> *nulls = nullptr);
    // typed 列通用入口：values 按 type 解释；nulls 为 (n+7)/8 字节位图（1=null），可空。
    bool addColumnTyped(const QString &table, const QString &name, const QString &unit,
                        ValueType type, const void *values, const quint8 *nulls, int count);

    // 全库时间范围（秒，来自 schema 时间轴视图）
    double timeMin() const;
    double timeMax() const;

    // 数据读取：调用 ps_dataset_read_window 从 Rust 现场取 (秒,值)，null 输出 NaN 被跳过。
    std::optional<ChannelView> channel(const QString &table, const QString &column) const;
    std::optional<ChannelView> channel(const QString &qualifiedName) const;
    QVector<ChannelView> allChannels() const;

    // Rust 侧视口 LOD 抽稀：在 [t0Us, t1Us] 内按 px 像素分桶输出 (x,min,max)。
    // 这是"降采样在 Rust 侧完成"的落地——大数据不进 C++ 全量数组。
    QVector<LodTriple> lodMinMax(const QString &table, const QString &column,
                                 TimeUs t0Us, TimeUs t1Us, int px) const;

    // 分析算子（全部委托 Rust ps-ops）：滤波 / 抗混叠抽取 / 统计。
    // 结果写调用方缓冲；缓冲不足时返回需要的长度（>0），用该长度重试。
    // 返回 0 表示成功且长度见 out；负数表示错误码。
    int applyFilter(const QString &filterId, const QJsonObject &params,
                    const QVector<double> &t, const QVector<double> &v,
                    QVector<double> &outT, QVector<double> &outV, QString *err = nullptr) const;
    int decimate(quint32 factor, const QVector<double> &t, const QVector<double> &v,
                 QVector<double> &outT, QVector<double> &outV, QString *err = nullptr) const;
    std::optional<ColumnStats> stats(const QVector<double> &v, bool withMedian = false,
                                     QString *err = nullptr) const;

    // 缓存（委托 Rust ps-cache，按源文件 size/mtime 指纹校验）：
    // 保存整个 typed 数据集，命中时不重新解析源文件。
    bool saveCache(const QString &path, qint64 sourceSize, qint64 sourceMtime,
                   QString *err = nullptr) const;
    // 命中返回已还原的数据集句柄；未命中/过期返回空 optional。
    static std::optional<Dataset> loadCache(const QString &path, qint64 sourceSize,
                                            qint64 sourceMtime, QString *err = nullptr);

private:
    class Data;
    QSharedDataPointer<Data> d;
};

} // namespace ng::core

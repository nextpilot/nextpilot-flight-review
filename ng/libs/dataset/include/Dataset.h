#pragma once

// Dataset 是 Rust kernel PsDataset 的薄 C++ 门面。
//
// 设计铁律（面向百万级数据，内存/速度要求高）：
//   1. 所有高性能数据处理都在 Rust 侧（列式存储、流式追加、窗口读取、LOD 抽稀、滤波/统计、缓存）。
//   2. C++ 只是门面：句柄 RAII + C ABI 类型映射 + 意图翻译，不持有任何列值/时间轴副本。
//   3. C++ 侧常驻内存 = O(列数 + 窗口上限)，与行数无关；绘图只读屏幕像素级数据。
//
// 因此：
//   - Table 只存 schema 元数据（行数、首尾时间、列名/类型/单位），不存 QVector<TimeUs> 时间轴。
//   - 填充走 appendFrame() 流式灌入 Rust，不在 C++ 攒完整数据集。
//   - 读走 readWindow(table, column, t0, t1, cap)：cap bounded，kernel 内等步长抽稀。
//   - 视口抽稀走 lodMinMax(px)：无论多少行，只返回 px 个 (x,min,max) 三元组。

#include "ps_kernel.h"

#include <QJsonObject>
#include <QSharedDataPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace nextpilot::plotscope::dataset
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

// 表 schema：只有元数据，不持有时间轴数组。
// 时间轴本体在 Rust（typed 表共享 i64 微秒轴）；这里仅存首尾时间供范围显示，O(1)。
struct Table
{
    QString name;
    int rows = 0;             // 行数（= kernel point_count）
    TimeUs firstTimeUs = 0;   // 首行时间（秒视图 * 1e6 还原），空表为 0
    TimeUs lastTimeUs = 0;    // 末行时间
    QVector<Column> columns;

    int rowCount() const { return rows; }
    int columnIndex(const QString &name) const;
    const Column *column(const QString &name) const;
};

// 绘图/分析用的有界序列：由 readWindow 现场从 Rust 读取，最多 cap 个点。
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

// 流式追加的一列：指向调用方缓冲，kernel 立即复制进 Rust，门面不留存。
struct FrameColumn
{
    QString name;
    QString unit;
    ValueType type = ValueType::F64;
    const void *values = nullptr;    // 按 type 解释的连续数组
    const quint8 *nulls = nullptr;   // (n+7)/8 字节位图（1=null），可空
};

// Dataset = Rust PsDataset 的共享所有权句柄 + C++ schema 元数据投影。
// 填充在构造期以流式 appendFrame 完成；填充后视为只读快照。
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

    // 改数据集显示名。
    //
    // 为什么需要：数据集的名字由**创建它的一方**决定（管道给的是
    // "src<源id>_<解析器id>"），而缓存还原出来的数据集带着缓存文件名。
    // 两者必须一致，否则"同一份数据第二次打开显示不同的名字"。
    void setName(const QString &name);

    // ---- schema 查询（来自构造期投影，纯元数据）----
    int tableCount() const;
    const Table *table(int index) const;
    const Table *table(const QString &name) const;
    int tableIndex(const QString &name) const;
    QStringList tableNames() const;

    // ---- 流式填充：全部委托 Rust kernel ----
    // 追加一批行；表不存在则按首批列建表，存在则校验列名/单位/类型后追加（ps_dataset_append_frame）。
    // timeUs 与每列 values 长度必须 == count。这是百万数据加载的唯一入口：调用方分块喂，C++ 不攒全量。
    bool appendFrame(const QString &table, const TimeUs *timeUs, int count,
                     const QVector<FrameColumn> &columns, QString *err = nullptr);

    // 便捷入口（小数据/测试用）：一次性建表并加 F64 列；内部仍流式委托 Rust，不留 C++ 副本。
    int addTable(const QString &name, const QVector<TimeUs> &timeUs);
    int addTable(const QString &name, const QVector<double> &timeSec);
    bool addF64Column(const QString &table, const QString &name, const QString &unit,
                      const QVector<double> &values, const QVector<bool> *nulls = nullptr);
    bool addColumnTyped(const QString &table, const QString &name, const QString &unit,
                        ValueType type, const void *values, const quint8 *nulls, int count);

    // 刷新 schema 投影（append 后调用；只从 channel 读 O(1) 元数据）。
    void refreshSchema();

    // 原始范围（微秒，精确整数）：对齐策略必须先在整数域算出 offset，
    // 再交给 AppContext::setSourceTimeOffset()（宿主态唯一真值，不在 Dataset 里存偏移）。
    std::optional<std::pair<TimeUs, TimeUs>> rawTimeRangeUs() const;

    // 全库时间范围（秒，来自 schema 元数据）
    double timeMin() const;
    double timeMax() const;

    // ---- 有界读取：cap 是硬上限，kernel 内等步长抽稀，绝不返回全量 ----
    // 默认上限 4096：即使百万行，一次也只读最多 4096 点。
    static constexpr int kDefaultWindowPoints = 4096;
    std::optional<ChannelView> readWindow(const QString &table, const QString &column,
                                          TimeUs t0Us, TimeUs t1Us, int cap) const;
    std::optional<ChannelView> channel(const QString &table, const QString &column,
                                       int cap = kDefaultWindowPoints) const;
    std::optional<ChannelView> channel(const QString &qualifiedName,
                                       int cap = kDefaultWindowPoints) const;
    QVector<ChannelView> allChannels(int cap = kDefaultWindowPoints) const;

    // Rust 侧视口 LOD：无论多少行，只返回 px 个 (x,min,max)——绘图路径的内存与行数无关。
    QVector<LodTriple> lodMinMax(const QString &table, const QString &column,
                                 TimeUs t0Us, TimeUs t1Us, int px) const;

    // 分析算子（全部委托 Rust ps-ops）：滤波 / 抗混叠抽取 / 统计。
    // 结果写调用方缓冲；缓冲不足时返回需要的长度（>0），用该长度重试。
    // 返回 0 表示成功且长度见 out；负数表示错误码。
    int applyFilter(const QString &filterId, const QJsonObject &params,
                    const QVector<double> &t, const QVector<double> &v,
                    QVector<double> &outT, QVector<double> &outV, QString *err = nullptr) const;

    // 只读派生预览（委托 Rust ps_preview_filter）：从本数据集的一条通道实时算出
    // 变换后的 (time, value) 序列，**不写回数据集、不新增通道**——原数据零改动。
    // 这是 data.derive 意图的底座：结果交给 UI（画布以 generated series 展示），
    // 而数据集始终是不可变快照。
    // cap 为硬上限（防止全量派生灌进 UI 内存）；total 输出 kernel 算出的实际点数，
    // total > 返回值即表示被 cap 截断。返回实际写入 outT/outV 的点数，<=0 看 err。
    int previewFilter(const QString &qualifiedName, const QString &filterId,
                      const QByteArray &paramsJson, int cap,
                      QVector<double> &outT, QVector<double> &outV,
                      int *total = nullptr, QString *err = nullptr) const;

    int decimate(quint32 factor, const QVector<double> &t, const QVector<double> &v,
                 QVector<double> &outT, QVector<double> &outV, QString *err = nullptr) const;
    std::optional<ColumnStats> stats(const QVector<double> &v, bool withMedian = false,
                                     QString *err = nullptr) const;

    // 缓存（委托 Rust ps-cache，按源文件 size/mtime 指纹校验）：
    bool saveCache(const QString &path, qint64 sourceSize, qint64 sourceMtime,
                   QString *err = nullptr) const;
    // 命中返回已还原的数据集句柄；未命中/过期返回空 optional。
    static std::optional<Dataset> loadCache(const QString &path, qint64 sourceSize,
                                            qint64 sourceMtime, QString *err = nullptr);

private:
    class Data;
    QSharedDataPointer<Data> d;
};

} // namespace nextpilot::plotscope::dataset

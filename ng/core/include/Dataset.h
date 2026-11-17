#pragma once

#include <QMap>
#include <QSharedDataPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace ng::core
{

using DatasetId = int;
constexpr DatasetId kInvalidDataset = -1;

// 时间轴统一 UTC 微秒，与 kernel ps_ndf::typed 的 i64 微秒一致。
using TimeUs = qint64;

// 列值类型，顺序与 kernel 的 PsValueType / ValueType 完全一致（数值可作为 ABI 码）。
enum class ValueType : int
{
    F64 = 0,
    F32 = 1,
    I64 = 2,
    U64 = 3,
    I32 = 4,
    U32 = 5,
    I16 = 6,
    U16 = 7,
    I8 = 8,
    U8 = 9,
    Bool = 10,
    Text = 11
};

const char *valueTypeName(ValueType type);
bool valueTypeIsNumeric(ValueType type);
int valueTypeBytes(ValueType type); // Text 变长返回 0

// 一列的物理存储。列式布局，values 的解释由 type 决定。
// 用 QVector<QVariant> 承载 typed 值，语义对齐 kernel 的 ColumnData 枚举，
// 但保持 ng 骨架独立于 Rust kernel。
struct Column
{
    QString name;
    QString unit;
    ValueType type = ValueType::F64;
    std::vector<QVariant> values;
    std::vector<bool> nulls; // 每行一个 null 标记；空表示全有效

    int size() const { return static_cast<int>(values.size()); }
    bool isNull(int row) const
    {
        if (nulls.empty())
            return false;
        return row >= 0 && row < static_cast<int>(nulls.size()) && nulls.at(row);
    }
    void setNull(int row, bool isNull);

    // 绘图/光标统一取 f64：null 或越界返回空。语义对齐 Column::value_as_f64。
    std::optional<double> valueAsF64(int row) const;
    QString valueText(int row) const;
};

// 一张表：一份共享时间轴（UTC 微秒）+ 表内序号 + 多列。
// 对齐 kernel 的 Table：一个 topic 一张表，表内所有列共享同一时间轴。
struct Table
{
    QString name;
    QVector<TimeUs> timeUs;      // 单调非降
    QVector<quint64> seq;        // 表内序号，同一微秒内多条记录靠它排序
    QVector<Column> columns;

    int rowCount() const { return timeUs.size(); }
    bool timeIsMonotonic() const;

    int columnIndex(const QString &name) const;
    const Column *column(const QString &name) const;
    Column *column(const QString &name);
};

// 通道视图：把 (table, column) 投影成绘图用的 (t_sec, value) f64 序列。
// 这是视图层唯一需要的形态；时间从表的微秒轴换算成秒，null 行被跳过。
// 对齐 kernel 的 PsChannelInfo。
struct ChannelView
{
    QString table;
    QString column;
    QString name;   // 展示名："table.column"
    QString unit;
    ValueType type = ValueType::F64;
    QVector<double> time;  // 秒
    QVector<double> value;

    bool empty() const { return time.isEmpty(); }
    int size() const { return qMin(time.size(), value.size()); }
};

// Dataset = 库，装多张表，表名在库内唯一。对齐 kernel 的 Dataset。
// 不可变快照：addTable/addColumn 在构造期完成，填充后视为只读。
class Dataset
{
public:
    Dataset();
    explicit Dataset(const QString &name);
    Dataset(const Dataset &other);
    Dataset &operator=(const Dataset &other);
    ~Dataset();

    QString name() const;
    QString format() const;
    void setFormat(const QString &format);

    // 表操作
    int tableCount() const;
    const Table *table(int index) const;
    Table *table(int index);
    const Table *table(const QString &name) const;
    Table *table(const QString &name);
    int tableIndex(const QString &name) const;
    QStringList tableNames() const;

    // 构造期填充。返回 -1 表示失败（重名、空时间轴、非单调等）。
    int addTable(const QString &name, const QVector<TimeUs> &timeUs);
    int addTable(const QString &name, const QVector<double> &timeSec);
    bool addColumn(const QString &tableName, Column column);

    // 全库统计（跨表取时间边界）
    double timeMin() const; // 秒
    double timeMax() const; // 秒

    // 通道投影：按 "table.column" 或 "column"（全库按列名找）取绘图序列。
    std::optional<ChannelView> channel(const QString &table, const QString &column) const;
    std::optional<ChannelView> channel(const QString &qualifiedName) const;
    QVector<ChannelView> allChannels() const;

private:
    class Data;
    QSharedDataPointer<Data> d;
};

} // namespace ng::core

#include "Dataset.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace nextpilot::plotscope::dataset
{

namespace
{
// Rust PsDataset 的共享所有权：最后一个引用释放时调用 ps_dataset_free。
std::shared_ptr<PsDataset> ownNative(PsDataset *p)
{
    return std::shared_ptr<PsDataset>(p, [](PsDataset *q) { ps_dataset_free(q); });
}

// 把逐行 bool 标记打包成 (n+7)/8 字节位图（1=null），对齐 kernel NullBits::from_raw。
QVector<quint8> packNulls(const QVector<bool> *flags, int n)
{
    QVector<quint8> bits;
    if (!flags || flags->isEmpty())
        return bits; // 空表示全有效
    bits.fill(0, (n + 7) / 8);
    for (int i = 0; i < n && i < flags->size(); ++i)
        if (flags->at(i))
            bits[i / 8] |= static_cast<quint8>(1u << (i % 8));
    return bits;
}

// 表格查看器的行区间读取：ps_dataset_value_at 逐值循环（Rust 列存 O(1) 随机访问）。
// kernel 返回码：0 成功 / 1 越界（停止）/ 2 null（NaN 占位）。
// values/timeSec 可空指针——调用方按需取其一或两者。
int readColumnRowsImpl(PsDataset *native, const Table &t, const QString &columnName,
                       int rowStart, int maxRows, QVector<double> *values,
                       QVector<double> *timeSec)
{
    const int rows = qMin(maxRows, t.rows - rowStart);
    if (rows <= 0)
        return 0;
    if (values)
        values->resize(rows);
    if (timeSec)
        timeSec->resize(rows);

    const QByteArray tableUtf8 = t.name.toUtf8();
    const QByteArray colUtf8 = columnName.toUtf8();
    double tv = 0.0;
    double vv = 0.0;
    for (int i = 0; i < rows; ++i)
    {
        const int rc = ps_dataset_value_at(
            native, tableUtf8.constData(), colUtf8.constData(),
            static_cast<quint64>(rowStart + i), &tv, &vv);
        if (rc == 1)
            return i; // 越界即停（ring 淘汰等场景行数比 schema 投影少）
        const bool isNull = (rc == 2);
        if (values)
            (*values)[i] = isNull ? std::numeric_limits<double>::quiet_NaN() : vv;
        if (timeSec)
            (*timeSec)[i] = tv;
    }
    return rows;
}

// 从 Rust native 重建 C++ schema 投影。
// 铁律：只读 O(1) 元数据（行数/首尾时间/列名/类型/单位），绝不复制时间轴或列值数组。
// PsChannelInfo::time 指向 Rust 内部 f64 视图（秒），首/末元素 O(1) 可读；
// 每列一个 typed channel，共享同一时间轴。
// kernel 的 typed 空表不产生 channel，keepEmpty 用于 addTable 后立即重建时保留空表。
void rebuildSchema(const std::shared_ptr<PsDataset> &native, QVector<Table> &out,
                   const QVector<Table> *keepEmpty = nullptr)
{
    out.clear();
    if (!native)
        return;
    const quint32 n = ps_dataset_channel_count(native.get());
    for (quint32 i = 0; i < n; ++i)
    {
        const PsChannelInfo *info = ps_dataset_channel(native.get(), i);
        if (!info || !info->table || !info->column)
            continue; // 跳过旧式 f64 通道，只投影 typed 表列
        const QString tableName = QString::fromUtf8(info->table);
        int ti = -1;
        for (int k = 0; k < out.size(); ++k)
            if (out.at(k).name == tableName)
            {
                ti = k;
                break;
            }
        if (ti < 0)
        {
            Table t;
            t.name = tableName;
            t.rows = static_cast<int>(info->point_count);
            // O(1) 读首尾时间：f64 秒 * 1e9 还原为纳秒。
            if (info->time && t.rows > 0)
            {
                t.firstTimeNs = static_cast<TimeNs>(std::llround(info->time[0] * 1000000000.0));
                t.lastTimeNs = static_cast<TimeNs>(std::llround(info->time[t.rows - 1] * 1000000000.0));
            }
            out.push_back(std::move(t));
            ti = out.size() - 1;
        }
        Column col;
        col.name = QString::fromUtf8(info->column);
        col.unit = info->unit ? QString::fromUtf8(info->unit) : QString();
        col.type = static_cast<ValueType>(ps_dataset_column_type(
            native.get(), info->table, info->column));
        col.rowCount = static_cast<int>(info->point_count);
        out[ti].columns.push_back(std::move(col));
    }
    // 补回尚无任何列的空表（kernel 此时不产出 channel）。
    if (keepEmpty)
    {
        for (const Table &t : *keepEmpty)
            if (t.columns.isEmpty())
            {
                bool exists = false;
                for (const Table &o : out)
                    if (o.name == t.name)
                    {
                        exists = true;
                        break;
                    }
                if (!exists)
                    out.push_back(t);
            }
    }
}
} // namespace

const char *valueTypeName(ValueType type)
{
    return ps_value_type_name(static_cast<int>(type));
}

bool valueTypeIsNumeric(ValueType type)
{
    return type != ValueType::Text;
}

int valueTypeBytes(ValueType type)
{
    switch (type)
    {
    case ValueType::F64:
    case ValueType::I64:
    case ValueType::U64:
        return 8;
    case ValueType::F32:
    case ValueType::I32:
    case ValueType::U32:
        return 4;
    case ValueType::I16:
    case ValueType::U16:
        return 2;
    case ValueType::I8:
    case ValueType::U8:
    case ValueType::Bool:
        return 1;
    case ValueType::Text:
        return 0;
    }
    return 0;
}

int Table::columnIndex(const QString &name) const
{
    for (int i = 0; i < columns.size(); ++i)
        if (columns.at(i).name == name)
            return i;
    return -1;
}

const Column *Table::column(const QString &name) const
{
    const int i = columnIndex(name);
    return i < 0 ? nullptr : &columns.at(i);
}

class Dataset::Data : public QSharedData
{
public:
    QString name;
    QString format;
    std::shared_ptr<PsDataset> native;
    QVector<Table> schema; // 从 native 投影的纯元数据，非真相源
};

Dataset::Dataset() : d(new Data) {}

Dataset::Dataset(const QString &name) : d(new Data)
{
    d->name = name;
}

Dataset::Dataset(const Dataset &other) = default;

Dataset &Dataset::operator=(const Dataset &other) = default;

Dataset::~Dataset() = default;

Dataset Dataset::create(const QString &name, const QString &format)
{
    Dataset ds(name);
    ds.d->native = ownNative(ps_dataset_new());
    ds.d->format = format;
    return ds;
}

Dataset Dataset::adopt(PsDataset *native, const QString &name, const QString &format)
{
    Dataset ds(name);
    ds.d->native = ownNative(native);
    ds.d->format = format;
    rebuildSchema(ds.d->native, ds.d->schema);
    return ds;
}

PsDataset *Dataset::nativeHandle() const
{
    return d->native.get();
}

bool Dataset::isNull() const
{
    return !d->native;
}

QString Dataset::name() const
{
    return d->name;
}

QString Dataset::format() const
{
    return d->format;
}

void Dataset::setFormat(const QString &format)
{
    d->format = format;
}

void Dataset::setName(const QString &name)
{
    d->name = name;
}

int Dataset::tableCount() const
{
    return d->schema.size();
}

const Table *Dataset::table(int index) const
{
    if (index < 0 || index >= d->schema.size())
        return nullptr;
    return &d->schema.at(index);
}

const Table *Dataset::table(const QString &name) const
{
    const int i = tableIndex(name);
    return i < 0 ? nullptr : &d->schema.at(i);
}

int Dataset::tableIndex(const QString &name) const
{
    for (int i = 0; i < d->schema.size(); ++i)
        if (d->schema.at(i).name == name)
            return i;
    return -1;
}

QStringList Dataset::tableNames() const
{
    QStringList names;
    names.reserve(d->schema.size());
    for (const Table &t : d->schema)
        names << t.name;
    return names;
}

bool Dataset::appendFrame(const QString &table, const TimeNs *timeNs, int count,
                          const QVector<FrameColumn> &columns, QString *err)
{
    if (!d->native || table.isEmpty() || !timeNs || count <= 0)
        return false;

    const QByteArray tableUtf8 = table.toUtf8();
    QVector<QByteArray> nameBufs;
    QVector<QByteArray> unitBufs;
    QVector<PsFrameColumn> nativeCols;
    nameBufs.reserve(columns.size());
    unitBufs.reserve(columns.size());
    nativeCols.reserve(columns.size());
    for (const FrameColumn &c : columns)
    {
        nameBufs.push_back(c.name.toUtf8());
        unitBufs.push_back(c.unit.toUtf8());
        PsFrameColumn nc;
        std::memset(&nc, 0, sizeof(nc));
        nc.name = nameBufs.constLast().constData();
        nc.unit = unitBufs.constLast().constData();
        nc.type = static_cast<PsValueType>(static_cast<int>(c.type));
        nc.values = c.values;
        nc.nulls = c.nulls;
        nativeCols.push_back(nc);
    }

    char errBuf[256] = {0};
    const int rc = ps_dataset_append_frame(
        d->native.get(), tableUtf8.constData(), timeNs, static_cast<quint32>(count),
        nativeCols.constData(), static_cast<quint32>(nativeCols.size()),
        errBuf, sizeof(errBuf));
    if (rc != 0)
    {
        if (err)
            *err = QString::fromUtf8(errBuf);
        return false;
    }
    // 刻意不在此刷新 schema：kernel 的 ps_dataset_channel() 会触发 rebuild_infos()
    // 重建整条 f64 时间轴（O(行数)）。流式加载应在所有批次灌完后调用一次 refreshSchema()，
    // 否则每批刷新会退化成 O(批数 × 行数)。
    return true;
}

void Dataset::refreshSchema()
{
    rebuildSchema(d->native, d->schema);
}

int Dataset::addTable(const QString &name, const QVector<TimeNs> &timeNs)
{
    if (!d->native || name.isEmpty() || timeNs.isEmpty() || tableIndex(name) >= 0)
        return -1;
    const QByteArray nameUtf8 = name.toUtf8();
    char err[256] = {0};
    const int rc = ps_dataset_add_table_typed(d->native.get(), nameUtf8.constData(),
                                              timeNs.constData(),
                                              static_cast<quint32>(timeNs.size()), err, sizeof(err));
    if (rc != 0)
        return -1;
    // 空表在 kernel 侧尚无 channel，无法投影；直接登记调用方已知的 O(1) 元数据。
    Table t;
    t.name = name;
    t.rows = timeNs.size();
    t.firstTimeNs = timeNs.first();
    t.lastTimeNs = timeNs.last();
    d->schema.push_back(std::move(t));
    return d->schema.size() - 1;
}

int Dataset::addTable(const QString &name, const QVector<double> &timeSec)
{
    QVector<TimeNs> us;
    us.reserve(timeSec.size());
    for (double t : timeSec)
        us.push_back(static_cast<TimeNs>(std::llround(t * 1000000000.0)));
    return addTable(name, us);
}

bool Dataset::addF64Column(const QString &tableName, const QString &name, const QString &unit,
                           const QVector<double> &values, const QVector<bool> *nulls)
{
    return addColumnTyped(tableName, name, unit, ValueType::F64, values.constData(),
                          packNulls(nulls, values.size()).constData(), values.size());
}

bool Dataset::addColumnTyped(const QString &tableName, const QString &name, const QString &unit,
                             ValueType type, const void *values, const quint8 *nulls, int count)
{
    if (!d->native || name.isEmpty() || count < 0 || tableIndex(tableName) < 0)
        return false;
    const QByteArray tableUtf8 = tableName.toUtf8();
    const QByteArray nameUtf8 = name.toUtf8();
    const QByteArray unitUtf8 = unit.toUtf8();
    char err[256] = {0};
    const int rc = ps_dataset_add_table_column_typed(
        d->native.get(), tableUtf8.constData(), nameUtf8.constData(), unitUtf8.constData(),
        static_cast<quint32>(type), values, nulls, static_cast<quint32>(count), err, sizeof(err));
    if (rc != 0)
        return false;
    const QVector<Table> before = d->schema; // 保留尚未有列的空表
    rebuildSchema(d->native, d->schema, &before);
    return true;
}

std::optional<std::pair<TimeNs, TimeNs>> Dataset::rawTimeRangeNs() const
{
    std::optional<TimeNs> min;
    std::optional<TimeNs> max;
    for (const Table &t : d->schema)
    {
        if (t.rows <= 0)
            continue;
        if (!min || t.firstTimeNs < *min)
            min = t.firstTimeNs;
        if (!max || t.lastTimeNs > *max)
            max = t.lastTimeNs;
    }
    if (!min || !max)
        return std::nullopt;
    return std::make_pair(*min, *max);
}

double Dataset::timeMin() const
{
    std::optional<TimeNs> min;
    for (const Table &t : d->schema)
        if (t.rows > 0)
        {
            if (!min || t.firstTimeNs < *min)
                min = t.firstTimeNs;
        }
    return min ? *min / 1000000000.0 : 0.0;
}

double Dataset::timeMax() const
{
    std::optional<TimeNs> max;
    for (const Table &t : d->schema)
        if (t.rows > 0)
        {
            if (!max || t.lastTimeNs > *max)
                max = t.lastTimeNs;
        }
    return max ? *max / 1000000000.0 : 0.0;
}

std::optional<ChannelView> Dataset::readWindow(const QString &tableName, const QString &columnName,
                                               TimeNs t0Ns, TimeNs t1Ns, int cap) const
{
    if (!d->native || cap <= 0)
        return std::nullopt;
    const Table *t = table(tableName);
    if (!t || !t->column(columnName) || t->rows <= 0)
        return std::nullopt;

    const QByteArray tableUtf8 = tableName.toUtf8();
    const QByteArray colUtf8 = columnName.toUtf8();
    const int n = qMin(cap, t->rows);
    QVector<double> time(n);
    QVector<double> value(n);
    // 有界读：cap 由调用方给定，kernel 在行数超过 cap 时等步长抽稀。
    const quint32 got = ps_dataset_read_window(
        d->native.get(), tableUtf8.constData(), colUtf8.constData(),
        t0Ns, t1Ns, time.data(), value.data(), static_cast<quint32>(n));
    if (got == 0)
        return std::nullopt;

    ChannelView view;
    view.table = t->name;
    view.column = columnName;
    view.name = QStringLiteral("%1.%2").arg(t->name, columnName);
    const Column *c = t->column(columnName);
    view.unit = c->unit;
    view.type = c->type;
    view.time.reserve(got);
    view.value.reserve(got);
    for (quint32 i = 0; i < got; ++i)
    {
        const double v = value.at(static_cast<int>(i));
        if (std::isnan(v))
            continue; // null 行跳过，与 kernel 视图一致
        view.time.push_back(time.at(static_cast<int>(i)));
        view.value.push_back(v);
    }
    return view;
}

std::optional<ChannelView> Dataset::channel(const QString &tableName, const QString &columnName,
                                            int cap) const
{
    const Table *t = table(tableName);
    if (!t)
        return std::nullopt;
    // 有界读：[首时间, 末时间] 窗口内最多 cap 点。
    return readWindow(tableName, columnName, t->firstTimeNs, t->lastTimeNs, cap);
}

std::optional<ChannelView> Dataset::channel(const QString &qualifiedName, int cap) const
{
    const int dot = qualifiedName.indexOf(QLatin1Char('.'));
    if (dot > 0)
        return channel(qualifiedName.left(dot), qualifiedName.mid(dot + 1), cap);
    for (const Table &t : d->schema)
        if (t.column(qualifiedName))
            return channel(t.name, qualifiedName, cap);
    return std::nullopt;
}

QVector<ChannelView> Dataset::allChannels(int cap) const
{
    QVector<ChannelView> out;
    for (const Table &t : d->schema)
        for (const Column &c : t.columns)
            if (auto view = channel(t.name, c.name, cap))
                out.push_back(std::move(*view));
    return out;
}

QVector<LodTriple> Dataset::lodMinMax(const QString &tableName, const QString &columnName,
                                      TimeNs t0Ns, TimeNs t1Ns, int px) const
{
    QVector<LodTriple> out;
    if (!d->native || px <= 0)
        return out;
    const QByteArray tableUtf8 = tableName.toUtf8();
    const QByteArray colUtf8 = columnName.toUtf8();
    QVector<double> triples(static_cast<qsizetype>(px) * 3);
    const quint32 n = ps_dataset_lod_minmax(d->native.get(), tableUtf8.constData(),
                                            colUtf8.constData(), t0Ns, t1Ns,
                                            static_cast<quint32>(px), triples.data(),
                                            static_cast<quint32>(px));
    out.reserve(n);
    for (quint32 i = 0; i < n; ++i)
        out.push_back({triples.at(static_cast<int>(i) * 3),
                       triples.at(static_cast<int>(i) * 3 + 1),
                       triples.at(static_cast<int>(i) * 3 + 2)});
    return out;
}

int Dataset::readTimeRows(const QString &tableName, int rowStart, int maxRows,
                          QVector<double> &timeSec) const
{
    // 时间轴取自该表首列：typed 表所有列共享同一时间轴（见 rebuildSchema 注释）。
    const Table *t = table(tableName);
    if (!t || !d->native || maxRows <= 0 || rowStart < 0 || t->columns.isEmpty())
        return (t && d->native && rowStart >= 0 && maxRows > 0) ? 0 : -1;
    return readColumnRowsImpl(d->native.get(), *t, t->columns.first().name, rowStart,
                              maxRows, nullptr, &timeSec);
}

int Dataset::readColumnRows(const QString &tableName, const QString &columnName,
                            int rowStart, int maxRows, QVector<double> &values) const
{
    const Table *t = table(tableName);
    if (!t || !d->native || maxRows <= 0 || rowStart < 0)
        return (t && d->native && rowStart >= 0 && maxRows > 0) ? 0 : -1;
    if (!t->column(columnName))
        return -1;
    return readColumnRowsImpl(d->native.get(), *t, columnName, rowStart, maxRows,
                              &values, nullptr);
}

bool Dataset::textAt(const QString &tableName, const QString &columnName, int row,
                     QString *out) const
{
    const Table *t = table(tableName);
    if (!t || !d->native || row < 0 || row >= t->rows)
        return false;
    const Column *c = t->column(columnName);
    if (!c || c->type != ValueType::Text)
        return false;
    const QByteArray tableUtf8 = t->name.toUtf8();
    const QByteArray colUtf8 = columnName.toUtf8();
    quint32 len = 0;
    // 先查长度（out=NULL/cap=0），再按实际长度取——参数值可能很长（位掩码/字符串枚举）。
    if (ps_dataset_text_at(d->native.get(), tableUtf8.constData(), colUtf8.constData(),
                           static_cast<quint64>(row), nullptr, 0, &len) != 0)
        return false;
    QByteArray buf(static_cast<int>(len) + 1, '\0');
    if (ps_dataset_text_at(d->native.get(), tableUtf8.constData(), colUtf8.constData(),
                           static_cast<quint64>(row), buf.data(),
                           static_cast<quint32>(buf.size()), &len) != 0)
        return false;
    if (out)
        *out = QString::fromUtf8(buf.constData(), static_cast<int>(len));
    return true;
}

int Dataset::readTextRows(const QString &tableName, const QString &columnName, int rowStart,
                          int maxRows, QStringList &out) const
{
    const Table *t = table(tableName);
    if (!t || maxRows <= 0 || rowStart < 0)
        return -1;
    const Column *c = t->column(columnName);
    if (!c || c->type != ValueType::Text)
        return -1;
    const int rows = qMin(maxRows, t->rows - rowStart);
    out.clear();
    out.reserve(qMax(0, rows));
    for (int i = 0; i < rows; ++i)
    {
        QString v;
        // null 或读失败：用空串占位，保持行号对齐（参数表里"值缺失"是合法状态）。
        textAt(tableName, columnName, rowStart + i, &v);
        out.append(v);
    }
    return out.size();
}

// ---- M2.7.1 整列原始读取 ----

std::optional<Dataset::ColumnRawView> Dataset::columnRaw(const QString &tableName,
                                                         const QString &columnName) const
{
    if (!d->native)
        return std::nullopt;
    const QByteArray tableUtf8 = tableName.toUtf8();
    const QByteArray colUtf8 = columnName.toUtf8();
    const void *values = nullptr;
    uint64_t count = 0;
    uint32_t type = 0;
    const quint8 *nulls = nullptr;
    const int rc = ps_dataset_column_raw(d->native.get(), tableUtf8.constData(),
                                         colUtf8.constData(), &values, &count, &type, &nulls);
    if (rc != 0)
        return std::nullopt;
    ColumnRawView view;
    view.values = values;
    view.count = count;
    view.type = static_cast<ValueType>(type);
    view.nulls = nulls;
    return view;
}

namespace
{
// column_raw 特化读值：按列类型直读原始缓冲 + null → NaN（一趟循环，无 FFI）。
// 返回 false 表示类型不支持（Text）。
bool convertRawF64(const Dataset::ColumnRawView &view, QVector<double> &out)
{
    const qsizetype n = static_cast<qsizetype>(view.count);
    out.resize(n);
    const bool hasNulls = view.nulls != nullptr;
    auto isNull = [view](qsizetype i) {
        return view.nulls[i / 8] & (1u << (i % 8));
    };
    switch (view.type)
    {
    case ValueType::F64:
        if (!hasNulls)
        {
            // 全有效：整块拷贝（8MB/百万行 ≈ 1ms 级）。
            std::memcpy(out.data(), view.values, n * sizeof(double));
        }
        else
        {
            const double *src = static_cast<const double *>(view.values);
            for (qsizetype i = 0; i < n; ++i)
                out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN() : src[i];
        }
        return true;
    case ValueType::F32:
    {
        const float *src = static_cast<const float *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::I64:
    {
        const int64_t *src = static_cast<const int64_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::U64:
    {
        const uint64_t *src = static_cast<const uint64_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::I32:
    {
        const int32_t *src = static_cast<const int32_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::U32:
    {
        const uint32_t *src = static_cast<const uint32_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::I16:
    {
        const int16_t *src = static_cast<const int16_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::U16:
    {
        const uint16_t *src = static_cast<const uint16_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::I8:
    {
        const int8_t *src = static_cast<const int8_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::U8:
    {
        const uint8_t *src = static_cast<const uint8_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : static_cast<double>(src[i]);
        return true;
    }
    case ValueType::Bool:
    {
        // kernel 侧 Vec<bool> 每元素 1 字节（0/1），按 U8 布局借出。
        const uint8_t *src = static_cast<const uint8_t *>(view.values);
        for (qsizetype i = 0; i < n; ++i)
            out[i] = isNull(i) ? std::numeric_limits<double>::quiet_NaN()
                               : (src[i] != 0 ? 1.0 : 0.0);
        return true;
    }
    case ValueType::Text:
        return false;
    }
    return false;
}
} // namespace

int Dataset::readColumnF64(const QString &tableName, const QString &columnName,
                           QVector<double> &values) const
{
    const Table *t = table(tableName);
    if (!t || !d->native)
        return -1;
    const Column *c = t->column(columnName);
    if (!c)
        return -1;
    if (c->type == ValueType::Text || t->rows == 0)
        return 0;
    auto view = columnRaw(tableName, columnName);
    if (!view || !convertRawF64(*view, values))
        return -1; // 类型/借出失败，退回逐值循环由调用方决定
    return static_cast<int>(view->count);
}

// ---- M2.7.5 数据集切片 ----

std::optional<Dataset> Dataset::slice(const QString &tableName,
                                      const QVector<QString> &columns,
                                      TimeNs t0Ns, TimeNs t1Ns,
                                      QString *err) const
{
    if (!d->native)
    {
        if (err)
            *err = QStringLiteral("数据集为空");
        return std::nullopt;
    }
    const QByteArray tableUtf8 = tableName.toUtf8();
    QVector<QByteArray> colUtf8;
    QVector<const char *> colPtrs;
    if (!columns.isEmpty())
    {
        colUtf8.reserve(columns.size());
        colPtrs.reserve(columns.size());
        for (const QString &c : columns)
        {
            colUtf8.push_back(c.toUtf8());
            colPtrs.push_back(colUtf8.constLast().constData());
        }
    }
    char errBuf[256] = {0};
    PsDataset *sliced = nullptr;
    const int rc = ps_dataset_slice(
        d->native.get(), tableUtf8.constData(),
        colPtrs.isEmpty() ? nullptr : colPtrs.constData(),
        static_cast<quint32>(colPtrs.size()),
        t0Ns, t1Ns, &sliced, errBuf, sizeof(errBuf));
    if (rc != 0)
    {
        if (err)
            *err = QString::fromUtf8(errBuf);
        return std::nullopt;
    }
    return Dataset::adopt(sliced, d->name, d->format);
}

// ---- M2.7.6 多列对齐读取 ----

std::optional<QVector<ChannelView>> Dataset::readWindowMulti(const QString &tableName,
                                                             const QVector<QString> &columns,
                                                             TimeNs t0Ns, TimeNs t1Ns,
                                                             int cap) const
{
    if (!d->native || cap <= 0 || columns.isEmpty())
        return std::nullopt;
    const Table *t = table(tableName);
    if (!t)
        return std::nullopt;
    for (const QString &c : columns)
        if (!t->column(c))
            return std::nullopt;

    const QByteArray tableUtf8 = tableName.toUtf8();
    QVector<QByteArray> colUtf8;
    QVector<const char *> colPtrs;
    colUtf8.reserve(columns.size());
    colPtrs.reserve(columns.size());
    for (const QString &c : columns)
    {
        colUtf8.push_back(c.toUtf8());
        colPtrs.push_back(colUtf8.constLast().constData());
    }
    const int n = qMin(cap, t->rows);
    QVector<double> time(n);
    QVector<QVector<double>> values(columns.size());
    QVector<double *> valuePtrs;
    valuePtrs.reserve(values.size());
    for (QVector<double> &v : values)
    {
        v.resize(n);
        valuePtrs.push_back(v.data());
    }
    const quint32 got = ps_dataset_read_window_multi(
        d->native.get(), tableUtf8.constData(), colPtrs.constData(),
        static_cast<quint32>(colPtrs.size()), t0Ns, t1Ns,
        time.data(), const_cast<const double **>(valuePtrs.constData()),
        static_cast<quint32>(n));
    if (got == 0)
        return std::nullopt;

    QVector<ChannelView> out;
    out.reserve(columns.size());
    for (int ci = 0; ci < columns.size(); ++ci)
    {
        const Column *c = t->column(columns.at(ci));
        ChannelView view;
        view.table = t->name;
        view.column = columns.at(ci);
        view.name = QStringLiteral("%1.%2").arg(t->name, columns.at(ci));
        view.unit = c->unit;
        view.type = c->type;
        view.time.reserve(got);
        view.value.reserve(got);
        const QVector<double> &src = values.at(ci);
        for (quint32 i = 0; i < got; ++i)
        {
            const double v = src.at(static_cast<int>(i));
            if (std::isnan(v))
                continue; // null 行跳过，与单列 readWindow 一致
            view.time.push_back(time.at(static_cast<int>(i)));
            view.value.push_back(v);
        }
        out.push_back(std::move(view));
    }
    return out;
}

int Dataset::applyFilter(const QString &filterId, const QJsonObject &params,
                         const QVector<double> &t, const QVector<double> &v,
                         QVector<double> &outT, QVector<double> &outV, QString *err) const
{
    const QByteArray idUtf8 = filterId.toUtf8();
    const QJsonDocument json = QJsonDocument(params);
    const QByteArray jsonBytes = json.toJson(QJsonDocument::Compact);
    const quint32 n = static_cast<quint32>(qMin(t.size(), v.size()));
    quint32 outN = 0;
    char errBuf[256] = {0};
    outT.resize(n);
    outV.resize(n);
    int rc = ps_filter_apply_series(idUtf8.constData(), jsonBytes.constData(),
                                    t.constData(), v.constData(), n,
                                    outT.data(), outV.data(), n, &outN, errBuf, sizeof(errBuf));
    if (rc == 6 && outN > 0)
    {
        outT.resize(outN);
        outV.resize(outN);
        rc = ps_filter_apply_series(idUtf8.constData(), jsonBytes.constData(),
                                    t.constData(), v.constData(), n,
                                    outT.data(), outV.data(), outN, &outN, errBuf, sizeof(errBuf));
    }
    if (rc != 0 && err)
        *err = QString::fromUtf8(errBuf);
    if (rc == 0)
    {
        outT.resize(outN);
        outV.resize(outN);
    }
    return rc;
}

int Dataset::previewFilter(const QString &qualifiedName, const QString &filterId,
                           const QByteArray &paramsJson, int cap,
                           QVector<double> &outT, QVector<double> &outV,
                           int *total, QString *err) const
{
    outT.clear();
    outV.clear();
    if (total)
        *total = 0;
    if (!d->native || qualifiedName.isEmpty() || filterId.isEmpty() || cap <= 0)
    {
        if (err)
            *err = QStringLiteral("数据集为空或参数不足");
        return 0;
    }

    const QByteArray nameUtf8 = qualifiedName.toUtf8();
    const QByteArray filterUtf8 = filterId.toUtf8();
    const QByteArray params = paramsJson.isEmpty() ? QByteArray("{}") : paramsJson;
    char errBuf[512] = {0};

    // 第一段：探测真实点数（out_t/out_v 传空缓冲、cap=0，kernel 仍会写 out_n）。
    // ps_preview_filter 在前置检查里要求 out_t/out_v 非空指针，故给一个哑元素。
    double dummy = 0.0;
    quint32 actual = 0;
    const int probeRc = ps_preview_filter(
        d->native.get(), nameUtf8.constData(), filterUtf8.constData(), params.constData(),
        &dummy, &dummy, 0, &actual, errBuf, sizeof(errBuf));
    // cap=0 时必然因 n<len 返回 6；其余非 0 是真实错误。
    if (probeRc != 0 && probeRc != 6)
    {
        if (err)
            *err = QString::fromUtf8(errBuf);
        return 0;
    }
    if (total)
        *total = static_cast<int>(actual);
    const int n = qMin<int>(static_cast<int>(actual), cap);
    if (n <= 0)
        return 0;

    // 第二段：按 cap 有界取数。cap 已 clamp 到实际点数，kernel 返回 0。
    outT.resize(n);
    outV.resize(n);
    const int rc = ps_preview_filter(
        d->native.get(), nameUtf8.constData(), filterUtf8.constData(), params.constData(),
        outT.data(), outV.data(), static_cast<quint32>(n), &actual, errBuf, sizeof(errBuf));
    if (rc != 0 && rc != 6)
    {
        outT.clear();
        outV.clear();
        if (err)
            *err = QString::fromUtf8(errBuf);
        return 0;
    }
    return n;
}

int Dataset::decimate(quint32 factor, const QVector<double> &t, const QVector<double> &v,
                      QVector<double> &outT, QVector<double> &outV, QString *err) const
{
    const quint32 n = static_cast<quint32>(qMin(t.size(), v.size()));
    quint32 outN = 0;
    char errBuf[256] = {0};
    const quint32 cap = qMax(quint32(1), n);
    outT.resize(cap);
    outV.resize(cap);
    int rc = ps_decimate_series(t.constData(), v.constData(), n, factor,
                                outT.data(), outV.data(), cap, &outN, errBuf, sizeof(errBuf));
    if (rc == 6 && outN > 0)
    {
        outT.resize(outN);
        outV.resize(outN);
        rc = ps_decimate_series(t.constData(), v.constData(), n, factor,
                                outT.data(), outV.data(), outN, &outN, errBuf, sizeof(errBuf));
    }
    if (rc != 0 && err)
        *err = QString::fromUtf8(errBuf);
    if (rc == 0)
    {
        outT.resize(outN);
        outV.resize(outN);
    }
    return rc;
}

std::optional<ColumnStats> Dataset::stats(const QVector<double> &v, bool withMedian,
                                          QString *err) const
{
    PsStats raw = {};
    char errBuf[256] = {0};
    const int rc = ps_stats_series(v.constData(), static_cast<quint32>(v.size()),
                                   withMedian ? 1 : 0, &raw, errBuf, sizeof(errBuf));
    if (rc != 0)
    {
        if (err)
            *err = QString::fromUtf8(errBuf);
        return std::nullopt;
    }
    ColumnStats s;
    s.count = raw.count;
    s.mean = raw.mean;
    s.stdDev = raw.std_dev;
    s.min = raw.min;
    s.max = raw.max;
    s.rms = raw.rms;
    s.median = raw.median;
    return s;
}

std::optional<QVector<double>> Dataset::quantiles(const QVector<double> &v,
                                                  const QVector<double> &probs,
                                                  QString *err) const
{
    QVector<double> out(qMax<qsizetype>(probs.size(), 1));
    char errBuf[256] = {0};
    const int rc = ps_stats_quantiles(
        v.constData(), static_cast<quint32>(v.size()),
        probs.constData(), static_cast<quint32>(probs.size()),
        out.data(), static_cast<quint32>(out.size()), errBuf, sizeof(errBuf));
    if (rc != 0)
    {
        if (err)
            *err = QString::fromUtf8(errBuf);
        return std::nullopt;
    }
    out.resize(probs.size());
    return out;
}

bool Dataset::saveCache(const QString &path, qint64 sourceSize, qint64 sourceMtime,
                        QString *err) const
{
    if (!d->native)
        return false;
    const QByteArray pathUtf8 = path.toUtf8();
    char errBuf[256] = {0};
    const int rc = ps_cache_save_dataset(d->native.get(), pathUtf8.constData(),
                                         sourceSize, sourceMtime, errBuf, sizeof(errBuf));
    if (rc != 0 && err)
        *err = QString::fromUtf8(errBuf);
    return rc == 0;
}

std::optional<Dataset> Dataset::loadCache(const QString &path, qint64 sourceSize,
                                          qint64 sourceMtime, QString *err)
{
    const QByteArray pathUtf8 = path.toUtf8();
    PsDataset *native = nullptr;
    char errBuf[256] = {0};
    const int rc = ps_cache_load_dataset(pathUtf8.constData(), sourceSize, sourceMtime,
                                         &native, errBuf, sizeof(errBuf));
    if (rc != 0)
    {
        if (err)
            *err = QString::fromUtf8(errBuf);
        return std::nullopt;
    }
    return adopt(native, QFileInfo(path).completeBaseName(), QStringLiteral("cache"));
}

} // namespace nextpilot::plotscope::dataset

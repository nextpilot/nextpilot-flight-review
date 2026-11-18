#include "Dataset.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ng::core
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

// 从 Rust native 重建 C++ schema 投影。schema 完全派生自 Rust，不在 C++ 侧另存一份真相。
// 注意：kernel 的 typed 表在没有任何列时不产生 channel，因此"空表"无法从 channel 枚举出来；
// keepEmpty 用于 addTable 后立即重建时保留调用方刚建的表（时间轴由调用方持有并传入）。
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
        if (!info || !info->table || !info->column || !info->time)
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
            const int rows = static_cast<int>(info->point_count);
            t.timeUs.reserve(rows);
            for (int r = 0; r < rows; ++r)
                t.timeUs.push_back(static_cast<TimeUs>(std::llround(info->time[r] * 1000000.0)));
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
    QVector<Table> schema; // 从 native 投影，非真相源
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

int Dataset::addTable(const QString &name, const QVector<TimeUs> &timeUs)
{
    if (!d->native || name.isEmpty() || timeUs.isEmpty() || tableIndex(name) >= 0)
        return -1;
    const QByteArray nameUtf8 = name.toUtf8();
    char err[256] = {0};
    const int rc = ps_dataset_add_table_typed(d->native.get(), nameUtf8.constData(),
                                              timeUs.constData(),
                                              static_cast<quint32>(timeUs.size()), err, sizeof(err));
    if (rc != 0)
        return -1;
    // 空表在 kernel 侧尚无 channel，无法从 native 投影出时间轴；直接登记调用方已知的时间轴。
    Table t;
    t.name = name;
    t.timeUs = timeUs;
    d->schema.push_back(std::move(t));
    return d->schema.size() - 1;
}

int Dataset::addTable(const QString &name, const QVector<double> &timeSec)
{
    QVector<TimeUs> us;
    us.reserve(timeSec.size());
    for (double t : timeSec)
        us.push_back(static_cast<TimeUs>(std::llround(t * 1000000.0)));
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

double Dataset::timeMin() const
{
    std::optional<TimeUs> min;
    for (const Table &t : d->schema)
        for (TimeUs us : t.timeUs)
            if (!min || us < *min)
                min = us;
    return min ? *min / 1000000.0 : 0.0;
}

double Dataset::timeMax() const
{
    std::optional<TimeUs> max;
    for (const Table &t : d->schema)
        for (TimeUs us : t.timeUs)
            if (!max || us > *max)
                max = us;
    return max ? *max / 1000000.0 : 0.0;
}

std::optional<ChannelView> Dataset::channel(const QString &tableName, const QString &columnName) const
{
    if (!d->native)
        return std::nullopt;
    const Table *t = table(tableName);
    if (!t || !t->column(columnName))
        return std::nullopt;

    const QByteArray tableUtf8 = tableName.toUtf8();
    const QByteArray colUtf8 = columnName.toUtf8();
    const int rows = t->rowCount();
    if (rows <= 0)
        return std::nullopt;

    QVector<double> time(rows);
    QVector<double> value(rows);
    // 全量读：[INT64_MIN, INT64_MAX]，cap=rows 不触发等步长抽稀。
    const quint32 got = ps_dataset_read_window(
        d->native.get(), tableUtf8.constData(), colUtf8.constData(),
        std::numeric_limits<TimeUs>::min(), std::numeric_limits<TimeUs>::max(),
        time.data(), value.data(), static_cast<quint32>(rows));
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
        if (std::isnan(value.at(static_cast<int>(i))))
            continue; // null 行跳过，与 kernel 视图一致
        view.time.push_back(time.at(static_cast<int>(i)));
        view.value.push_back(value.at(static_cast<int>(i)));
    }
    return view;
}

std::optional<ChannelView> Dataset::channel(const QString &qualifiedName) const
{
    const int dot = qualifiedName.indexOf(QLatin1Char('.'));
    if (dot > 0)
        return channel(qualifiedName.left(dot), qualifiedName.mid(dot + 1));
    for (const Table &t : d->schema)
        if (t.column(qualifiedName))
            return channel(t.name, qualifiedName);
    return std::nullopt;
}

QVector<ChannelView> Dataset::allChannels() const
{
    QVector<ChannelView> out;
    for (const Table &t : d->schema)
        for (const Column &c : t.columns)
            if (auto view = channel(t.name, c.name))
                out.push_back(std::move(*view));
    return out;
}

QVector<LodTriple> Dataset::lodMinMax(const QString &tableName, const QString &columnName,
                                      TimeUs t0Us, TimeUs t1Us, int px) const
{
    QVector<LodTriple> out;
    if (!d->native || px <= 0)
        return out;
    const QByteArray tableUtf8 = tableName.toUtf8();
    const QByteArray colUtf8 = columnName.toUtf8();
    QVector<double> triples(static_cast<qsizetype>(px) * 3);
    const quint32 n = ps_dataset_lod_minmax(d->native.get(), tableUtf8.constData(),
                                            colUtf8.constData(), t0Us, t1Us,
                                            static_cast<quint32>(px), triples.data(),
                                            static_cast<quint32>(px));
    out.reserve(n);
    for (quint32 i = 0; i < n; ++i)
        out.push_back({triples.at(static_cast<int>(i) * 3),
                       triples.at(static_cast<int>(i) * 3 + 1),
                       triples.at(static_cast<int>(i) * 3 + 2)});
    return out;
}

int Dataset::applyFilter(const QString &filterId, const QJsonObject &params,
                         const QVector<double> &t, const QVector<double> &v,
                         QVector<double> &outT, QVector<double> &outV, QString *err) const
{
    const QByteArray idUtf8 = filterId.toUtf8();
    const QByteArray json = QJsonDocument(params).toJson(QJsonDocument::Compact);
    const quint32 n = static_cast<quint32>(qMin(t.size(), v.size()));
    quint32 outN = 0;
    char errBuf[256] = {0};
    outT.resize(n);
    outV.resize(n);
    // 缓冲不足时 Rust 返回 6 并回填 outN 实际长度 -> 扩容重试一次。
    int rc = ps_filter_apply_series(idUtf8.constData(), json.constData(),
                                    t.constData(), v.constData(), n,
                                    outT.data(), outV.data(), n, &outN, errBuf, sizeof(errBuf));
    if (rc == 6 && outN > 0)
    {
        outT.resize(outN);
        outV.resize(outN);
        rc = ps_filter_apply_series(idUtf8.constData(), json.constData(),
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

} // namespace ng::core

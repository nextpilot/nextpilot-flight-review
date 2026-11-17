#include "Dataset.h"

#include <QSharedData>

#include <algorithm>

namespace ng::core
{

const char *valueTypeName(ValueType type)
{
    static const char *names[] = {"F64", "F32", "I64", "U64", "I32", "U32",
                                  "I16", "U16", "I8",  "U8",  "Bool", "Text"};
    const int i = static_cast<int>(type);
    return i >= 0 && i < 12 ? names[i] : "";
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

void Column::setNull(int row, bool isNull)
{
    if (row < 0 || row >= size())
        return;
    if (nulls.empty())
        nulls.assign(size(), false);
    nulls[row] = isNull;
}

std::optional<double> Column::valueAsF64(int row) const
{
    if (row < 0 || row >= size() || isNull(row))
        return std::nullopt;
    const QVariant &v = values.at(row);
    switch (type)
    {
    case ValueType::Text:
        return v.toString().toDouble();
    case ValueType::Bool:
        return v.toBool() ? 1.0 : 0.0;
    default:
        return v.toDouble();
    }
}

QString Column::valueText(int row) const
{
    if (row < 0 || row >= size() || isNull(row))
        return QString();
    if (type == ValueType::Text)
        return values.at(row).toString();
    return values.at(row).toString();
}

bool Table::timeIsMonotonic() const
{
    for (int i = 1; i < timeUs.size(); ++i)
        if (timeUs.at(i) < timeUs.at(i - 1))
            return false;
    return true;
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

Column *Table::column(const QString &name)
{
    const int i = columnIndex(name);
    return i < 0 ? nullptr : &columns[i];
}

class Dataset::Data : public QSharedData
{
public:
    QString name;
    QString format;
    QVector<Table> tables;
};

Dataset::Dataset() : d(new Data) {}

Dataset::Dataset(const QString &name) : d(new Data)
{
    d->name = name;
}

Dataset::Dataset(const Dataset &other) = default;

Dataset &Dataset::operator=(const Dataset &other) = default;

Dataset::~Dataset() = default;

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
    return d->tables.size();
}

const Table *Dataset::table(int index) const
{
    if (index < 0 || index >= d->tables.size())
        return nullptr;
    return &d->tables.at(index);
}

Table *Dataset::table(int index)
{
    if (index < 0 || index >= d->tables.size())
        return nullptr;
    return &d->tables[index];
}

const Table *Dataset::table(const QString &name) const
{
    const int i = tableIndex(name);
    return i < 0 ? nullptr : &d->tables.at(i);
}

Table *Dataset::table(const QString &name)
{
    const int i = tableIndex(name);
    return i < 0 ? nullptr : &d->tables[i];
}

int Dataset::tableIndex(const QString &name) const
{
    for (int i = 0; i < d->tables.size(); ++i)
        if (d->tables.at(i).name == name)
            return i;
    return -1;
}

QStringList Dataset::tableNames() const
{
    QStringList names;
    names.reserve(d->tables.size());
    for (const Table &t : d->tables)
        names << t.name;
    return names;
}

int Dataset::addTable(const QString &name, const QVector<TimeUs> &timeUs)
{
    if (name.isEmpty() || timeUs.isEmpty() || tableIndex(name) >= 0)
        return -1;
    Table t;
    t.name = name;
    t.timeUs = timeUs;
    for (int i = 0; i < timeUs.size(); ++i)
        t.seq.push_back(static_cast<quint64>(i));
    if (!t.timeIsMonotonic())
        return -1;
    d->tables.push_back(std::move(t));
    return d->tables.size() - 1;
}

int Dataset::addTable(const QString &name, const QVector<double> &timeSec)
{
    QVector<TimeUs> us;
    us.reserve(timeSec.size());
    for (double t : timeSec)
        us.push_back(static_cast<TimeUs>(t * 1000000.0));
    return addTable(name, us);
}

bool Dataset::addColumn(const QString &tableName, Column column)
{
    Table *t = table(tableName);
    if (!t || column.name.isEmpty() || t->columnIndex(column.name) >= 0)
        return false;
    if (!column.nulls.empty() && column.nulls.size() != column.values.size())
        return false;
    t->columns.push_back(std::move(column));
    return true;
}

double Dataset::timeMin() const
{
    std::optional<TimeUs> min;
    for (const Table &t : d->tables)
        for (TimeUs us : t.timeUs)
            if (!min || us < *min)
                min = us;
    return min ? *min / 1000000.0 : 0.0;
}

double Dataset::timeMax() const
{
    std::optional<TimeUs> max;
    for (const Table &t : d->tables)
        for (TimeUs us : t.timeUs)
            if (!max || us > *max)
                max = us;
    return max ? *max / 1000000.0 : 0.0;
}

std::optional<ChannelView> Dataset::channel(const QString &tableName, const QString &columnName) const
{
    const Table *t = table(tableName);
    if (!t)
        return std::nullopt;
    const Column *c = t->column(columnName);
    if (!c)
        return std::nullopt;

    ChannelView view;
    view.table = t->name;
    view.column = c->name;
    view.name = QStringLiteral("%1.%2").arg(t->name, c->name);
    view.unit = c->unit;
    view.type = c->type;
    const int n = qMin(t->rowCount(), c->size());
    view.time.reserve(n);
    view.value.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const auto v = c->valueAsF64(i);
        if (!v.has_value())
            continue; // null 行跳过，与 kernel 视图一致
        view.time.push_back(t->timeUs.at(i) / 1000000.0);
        view.value.push_back(*v);
    }
    return view;
}

std::optional<ChannelView> Dataset::channel(const QString &qualifiedName) const
{
    const int dot = qualifiedName.indexOf(QLatin1Char('.'));
    if (dot > 0)
        return channel(qualifiedName.left(dot), qualifiedName.mid(dot + 1));
    // 无表名时全库按列名查找，取第一个命中。
    for (const Table &t : d->tables)
        if (t.column(qualifiedName))
            return channel(t.name, qualifiedName);
    return std::nullopt;
}

QVector<ChannelView> Dataset::allChannels() const
{
    QVector<ChannelView> out;
    for (const Table &t : d->tables)
        for (const Column &c : t.columns)
            if (auto view = channel(t.name, c.name))
                out.push_back(std::move(*view));
    return out;
}

} // namespace ng::core

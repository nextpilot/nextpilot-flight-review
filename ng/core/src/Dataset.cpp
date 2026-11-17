#include "Dataset.h"

#include <QSharedData>

namespace ng::core
{

class Dataset::Data : public QSharedData
{
public:
    QString name;
    QString format;
    QVector<SeriesData> series;
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

int Dataset::seriesCount() const
{
    return d->series.size();
}

const SeriesData *Dataset::series(int index) const
{
    if (index < 0 || index >= d->series.size())
        return nullptr;
    return &d->series.at(index);
}

const SeriesData *Dataset::series(const QString &name) const
{
    for (const SeriesData &s : d->series)
        if (s.name == name)
            return &s;
    return nullptr;
}

QStringList Dataset::seriesNames() const
{
    QStringList names;
    names.reserve(d->series.size());
    for (const SeriesData &s : d->series)
        names << s.name;
    return names;
}

double Dataset::timeMin() const
{
    double min = 0.0;
    bool first = true;
    for (const SeriesData &s : d->series)
    {
        for (double t : s.time)
        {
            if (first || t < min)
            {
                min = t;
                first = false;
            }
        }
    }
    return min;
}

double Dataset::timeMax() const
{
    double max = 0.0;
    bool first = true;
    for (const SeriesData &s : d->series)
    {
        for (double t : s.time)
        {
            if (first || t > max)
            {
                max = t;
                first = false;
            }
        }
    }
    return max;
}

void Dataset::addSeries(SeriesData series)
{
    d->series.push_back(std::move(series));
}

} // namespace ng::core

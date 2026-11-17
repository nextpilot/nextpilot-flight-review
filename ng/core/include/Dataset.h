#pragma once

#include <QSharedDataPointer>
#include <QString>
#include <QVector>

#include <cstdint>

namespace ng::core
{

using DatasetId = int;
constexpr DatasetId kInvalidDataset = -1;

// 一列数据：时间轴（UTC 秒）+ 值。Series 是绘图的唯一输入。
struct SeriesData
{
    QString name;
    QString unit;
    QVector<double> time;
    QVector<double> value;

    bool empty() const { return time.isEmpty(); }
    int size() const { return qMin(time.size(), value.size()); }
};

// Dataset 是不可变快照：派生/变换产生新 Dataset，不修改原对象。
// 用隐式共享（QSharedData）让拷贝廉价，符合"数据快照"语义。
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

    int seriesCount() const;
    const SeriesData *series(int index) const;
    const SeriesData *series(const QString &name) const;
    QStringList seriesNames() const;

    double timeMin() const;
    double timeMax() const;

    // 构造期填充；填充完成后视为不可变。
    void addSeries(SeriesData series);

private:
    class Data;
    QSharedDataPointer<Data> d;
};

} // namespace ng::core

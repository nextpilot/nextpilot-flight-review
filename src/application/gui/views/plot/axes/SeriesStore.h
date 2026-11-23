#pragma once

#include "PlotSeries.h"

#include <QColor>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace nextpilot::plotscope::gui
{

// 曲线集合：画布上有哪些曲线、各自的样式与变换配方。
// 只管数据，不知道视图怎么画；变化通过 changed 通知宿主重推。
class SeriesStore : public QObject
{
    Q_OBJECT // NOLINT
public:
    using DatasetId = nextpilot::plotscope::dataset::DatasetId;

    explicit SeriesStore(QObject *parent = nullptr);

    // 自动配色：宿主按"当前条数"给一个主题色（palette 归画布，store 不碰主题）。
    void setColorProvider(std::function<QColor(int)> provider);

    const QVector<PlotSeries> &all() const { return m_series; }
    int count() const { return m_series.size(); }
    bool isEmpty() const { return m_series.isEmpty(); }
    const PlotSeries *at(int index) const;
    const PlotSeries *byKey(const QString &key) const;
    int indexOf(const QString &key) const;

    // 同 key 原地更新、否则追加；返回索引。会自动补主题色。
    int upsert(const PlotSeries &series);
    // 单条替换全部（旧 setChannel 兼容入口，不触发重推）。
    void replaceWith(const PlotSeries &series);
    // 只更新首条曲线的 LOD 区间（旧 setLodData 兼容入口）。
    void setFirstLod(const QVector<nextpilot::plotscope::dataset::LodTriple> &lod,
                     double t0, double t1);
    bool remove(const QString &key);
    void clear();

    bool setAxis(const QString &key, bool rightAxis);
    bool setVisible(const QString &key, bool visible);
    bool setInk(const QString &key, const QColor &color);
    bool setWidth(const QString &key, double width);
    bool setXOffset(const QString &key, double offsetSec);
    bool setYTransform(const QString &key, const QString &descriptorId,
                       const QString &paramsJson);
    // 图例点击后由宿主回写：只改状态、不改可见性通知（避免重建冲掉 Axes 状态）。
    void setVisibleSilently(int index, bool visible);

    void setSourceOffsetProvider(std::function<double(DatasetId)> provider);
    double sourceOffsetFor(const PlotSeries &s) const;

    // 批量重建（renderFrom）用：拿到可写指针后由调用方负责重推。
    PlotSeries *mutableAt(int index) { return at(index) ? &m_series[index] : nullptr; }

    double t0() const { return m_t0; }
    double t1() const { return m_t1; }

signals:
    // 数据/样式变了，宿主该重推曲线。
    void changed();
    // 标题变了；firstSeries 为真是"画布第一次有曲线"。
    void titleChanged(const QString &title, bool firstSeries);

private:
    QVector<PlotSeries> m_series;
    std::function<QColor(int)> m_colorProvider;
    std::function<double(DatasetId)> m_sourceOffsetProvider;
    double m_t0 = 0.0;
    double m_t1 = 0.0;
};

} // namespace nextpilot::plotscope::gui

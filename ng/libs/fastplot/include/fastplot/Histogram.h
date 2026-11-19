#pragma once

#include "Artist.h"
#include "Instrument.h"
#include "PlotData.h"

#include <QColor>
#include <QVector>

namespace nextpilot::plotscope::fastplot
{

// 直方图：把一条序列的值域分箱计数（对应老画布 Histogram 模式）。
//
// 分箱在库内完成；数据仍由调用方持有，这里只持指针。计数结果按
// （数据 revision, 点数）缓存，内容不变不重算。
class Histogram : public Instrument, public Artist
{
    Q_OBJECT
public:
    explicit Histogram(QWidget *parent = nullptr);

    void setData(const IPlotData *data);
    void setBins(int bins);
    void setColor(const QColor &color);
    QColor color() const { return m_color; }

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;
    // Artist：作为坐标系里的图元绘制（复用同一绘制算法）。
    void paintInRect(QPainter &painter, const QRectF &rect) const override;

private:
    void rebuild() const;

    const IPlotData *m_data = nullptr;
    int m_bins = 40;
    QColor m_color;

    mutable QVector<int> m_counts;
    mutable double m_lo = 0.0;
    mutable double m_hi = 1.0;
    mutable int m_maxCount = 0;
    mutable quint64 m_revision = 0;
    mutable qsizetype m_n = 0;
};

} // namespace nextpilot::plotscope::fastplot

#pragma once

#include "fastplot/Instrument.h"

#include <QColor>
#include <QVector>

namespace fastplot
{

class IPlotData;

// 瀑布图：多条序列沿 z 方向错开堆叠（老画布的 "3D 曲面"）。
//
// 老实说它不是曲面——没有栅格、没有插值，就是一叠错开的折线。名字用 Waterfall，
// 将来真做曲面另立 SurfacePlot，不要让这个名字骗人。
class WaterfallPlot : public Instrument
{
    Q_OBJECT
public:
    explicit WaterfallPlot(QWidget *parent = nullptr);

    // 非拥有：直读调用方的数组。
    void setSeries(const QVector<const IPlotData *> &series, const QVector<QColor> &colors = {});
    void clearSeries();

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;

private:
    QVector<const IPlotData *> m_series;
    QVector<QColor> m_colors;
};

} // namespace fastplot

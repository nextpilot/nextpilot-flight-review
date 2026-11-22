#pragma once

#include "Artist.h"
#include "Instrument.h"

#include <QColor>
#include <QStringList>
#include <QVector>

namespace nextpilot::plotscope::fastplot
{

// 柱状图：每根柱对应一个类别，高度是该类别的值（对应老画布 Bar 模式）。
//
// 与 PlotWidget 的区别：它不画时间轴、不做 LOD，输入就是一组已聚合好的
// 类别-值对；聚合（如各通道均值）由调用方算好喂进来。
class Bar : public Instrument, public Artist
{
    Q_OBJECT
public:
    explicit Bar(QWidget *parent = nullptr);

    // colors 为空时统一用 accent；长度不足时循环取色。
    void setBars(const QStringList &labels, const QVector<double> &values,
                 const QVector<QColor> &colors = {});

    // 显式量程；不设则按 0 与数据极值自动定界。
    void setValueRange(double lo, double hi);
    void clearValueRange();

    // 槽位内留白比例（0..1），默认 0.35。
    void setBarGap(double ratio);

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;
    // Artist：作为坐标系里的图元绘制（复用同一绘制算法）。
    void paintInRect(QPainter &painter, const QRectF &rect) const override;

private:
    QStringList m_labels;
    QVector<double> m_values;
    QVector<QColor> m_colors;
    bool m_rangeSet = false;
    double m_lo = 0.0;
    double m_hi = 1.0;
    double m_gap = 0.35;
};

// 兼容别名：BarChart 即 Bar。
using BarChart = Bar;

} // namespace nextpilot::plotscope::fastplot

#pragma once

#include "fastplot/Artist.h"
#include "fastplot/Axis.h"
#include "fastplot/LodCache.h"
#include "fastplot/PlotData.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace nextpilot::plotscope::fastplot
{

// 一条曲线。两种数据来源，同一套绘制：
//
//   1) fromData：手里有全分辨率数组 → 库内像素列聚合（LodCache）；
//   2) fromLod ：内核已按像素分桶 → 直接画，绝不在 C++ 里重扫。
//
// 曲线只持指针/三元组，不复制数据数组；LOD 三元组本身 <= 像素宽，
// 拷贝它是有限开销（ng 侧每帧一次，不随行数增长）。
// 散点符号形状。None = 不画符号（纯折线/包络）。
enum class Symbol
{
    None,
    Circle,
    Square,
    Diamond,
    Cross
};

class Line : public Artist
{
public:
    static Line fromData(const IPlotData *data, const QColor &ink, qreal lineWidth = 1.4);
    static Line fromLod(const QVector<LodPoint> &lod, const QColor &ink, qreal lineWidth = 1.4);

    // 曲线带多少点（LOD 是桶数，数据是行数）。缓存/自检用。
    qsizetype pointCount() const { return isLod() ? m_lod.size() : (m_data ? m_data->size() : 0); }

    QColor ink() const { return m_ink; }
    void setInk(const QColor &color) { m_ink = color; }

    qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal w) { m_lineWidth = w; }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    // 该曲线挂在哪条 y 轴（双轴不需要任何特判）。
    AxisSide yAxis() const { return m_yAxis; }
    void setYAxis(AxisSide side) { m_yAxis = side; }

    // 散点模式：true = 不连线，只画符号（对应老画布 Scatter 模式）。
    bool scatter() const { return m_scatter; }
    void setScatter(bool on) { m_scatter = on; }

    Symbol symbol() const { return m_symbol; }
    void setSymbol(Symbol symbol) { m_symbol = symbol; }
    qreal symbolSize() const { return m_symbolSize; }
    void setSymbolSize(qreal size) { m_symbolSize = size; }

    bool isLod() const { return m_data == nullptr; }
    const IPlotData *data() const { return m_data; }
    const QVector<LodPoint> &lod() const { return m_lod; }

    // 数据内容变了（revision 也变了）就调这个，强制重建聚合。
    void invalidate() { m_cache.clear(); }

private:
    Line() = default;

    const IPlotData *m_data = nullptr;
    QVector<LodPoint> m_lod;
    QColor m_ink{0x5b, 0xc0, 0xf0};
    qreal m_lineWidth = 1.4;
    bool m_visible = true;
    bool m_scatter = false;
    Symbol m_symbol = Symbol::Circle;
    qreal m_symbolSize = 5.0;
    AxisSide m_yAxis = AxisSide::Left;
    LodCache m_cache;

    friend class Axes;
};

// 旧名兼容：老代码里的 Curve 就是 Line。
using Curve = Line;

} // namespace nextpilot::plotscope::fastplot

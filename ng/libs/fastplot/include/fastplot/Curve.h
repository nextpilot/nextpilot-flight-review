#pragma once

#include "fastplot/Axis.h"
#include "fastplot/LodCache.h"
#include "fastplot/PlotData.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace fastplot
{

// 一条曲线。两种数据来源，同一套绘制：
//
//   1) fromData：手里有全分辨率数组 → 库内像素列聚合（LodCache）；
//   2) fromLod ：内核已按像素分桶 → 直接画，绝不在 C++ 里重扫。
//
// 曲线只持指针/三元组，不复制数据数组；LOD 三元组本身 <= 像素宽，
// 拷贝它是有限开销（ng 侧每帧一次，不随行数增长）。
class Curve
{
public:
    static Curve fromData(const IPlotData *data, const QColor &ink, qreal lineWidth = 1.4);
    static Curve fromLod(const QVector<LodPoint> &lod, const QColor &ink, qreal lineWidth = 1.4);

    // 曲线带多少点（LOD 是桶数，数据是行数）。缓存/自检用。
    qsizetype pointCount() const { return isLod() ? m_lod.size() : (m_data ? m_data->size() : 0); }

    // 图例里显示的名字（空 = 不进图例）。
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    QColor ink() const { return m_ink; }
    void setInk(const QColor &color) { m_ink = color; }

    qreal lineWidth() const { return m_lineWidth; }
    void setLineWidth(qreal w) { m_lineWidth = w; }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    // 该曲线挂在哪条 y 轴（双轴不需要任何特判）。
    AxisSide yAxis() const { return m_yAxis; }
    void setYAxis(AxisSide side) { m_yAxis = side; }

    bool isLod() const { return m_data == nullptr; }
    const IPlotData *data() const { return m_data; }
    const QVector<LodPoint> &lod() const { return m_lod; }

    // 数据内容变了（revision 也变了）就调这个，强制重建聚合。
    void invalidate() { m_cache.clear(); }

private:
    Curve() = default;

    const IPlotData *m_data = nullptr;
    QVector<LodPoint> m_lod;
    QString m_name;
    QColor m_ink{0x5b, 0xc0, 0xf0};
    qreal m_lineWidth = 1.4;
    bool m_visible = true;
    AxisSide m_yAxis = AxisSide::Left;
    LodCache m_cache;

    friend class PlotWidget;
};

} // namespace fastplot

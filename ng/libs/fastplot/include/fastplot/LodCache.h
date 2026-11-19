#pragma once

#include "fastplot/PlotData.h"
#include "fastplot/Viewport.h"

#include <QRectF>
#include <QVector>

namespace nextpilot::plotscope::fastplot
{

// 像素列聚合缓存：把全分辨率点集压成"每屏幕列一个 min/max"。
//
// 为什么必须有：3000 点还好，100 万点直接 drawPolyline 就是灾难。
// 聚合后绘制成本 = O(像素宽)，与行数无关；且聚合结果按
// （数据版本, 视口, 列数）缓存，拖动/悬停时同一视口不重算。
//
// 与 ng 的关系：ng 的 typed 表已经有 Rust 侧 LOD（ps_dataset_lod_minmax），
// 那条路径不该在 C++ 里再扫一遍——用 Curve::fromLod 直接吃内核结果。
// 本缓存服务的是"C++ 侧手里就有一整段数组"的场景（脚本结果、外部插件数据）。
class LodCache
{
public:
    // 返回按屏幕列聚合后的三元组；命中缓存时不重算。
    const QVector<LodPoint> &points(const IPlotData &data, const Viewport &vp);

    void clear();

    // 诊断用：上次重建扫描了多少个点；重建次数用来证明缓存真的命中了。
    qsizetype lastScanned() const { return m_lastScanned; }
    int rebuilds() const { return m_rebuilds; }

private:
    QVector<LodPoint> m_points;
    bool m_valid = false;
    quint64 m_revision = ~0ull;
    QRectF m_rect;
    double m_xLo = 0.0;
    double m_xHi = 0.0;
    int m_columns = 0;
    qsizetype m_lastScanned = 0;
    int m_rebuilds = 0;
};

} // namespace nextpilot::plotscope::fastplot

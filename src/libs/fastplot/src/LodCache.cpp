#include "fastplot/LodCache.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nextpilot::plotscope::fastplot
{

const QVector<LodPoint> &LodCache::points(const IPlotData &data, const Viewport &vp)
{
    const int columns = std::max(1, static_cast<int>(std::lround(vp.plotRect().width())));
    const quint64 revision = data.revision();

    const bool hit = m_valid
        && m_revision == revision
        && m_rect == vp.plotRect()
        && m_xLo == vp.xLower()
        && m_xHi == vp.xUpper()
        && m_columns == columns;
    if (hit)
        return m_points;

    ++m_rebuilds;
    m_points.clear();
    const qsizetype n = data.size();
    m_lastScanned = n;
    if (n <= 0)
    {
        m_valid = true;
        m_revision = revision;
        m_rect = vp.plotRect();
        m_xLo = vp.xLower();
        m_xHi = vp.xUpper();
        m_columns = columns;
        return m_points;
    }

    // 一列一个桶：只在"有样本落入"的列产出点，避免空白列浪费绘制。
    QVector<double> colMin(columns, std::numeric_limits<double>::infinity());
    QVector<double> colMax(columns, -std::numeric_limits<double>::infinity());
    QVector<double> colX(columns, 0.0);
    QVector<bool> colHit(columns, false);

    const double left = vp.plotRect().left();
    const double width = std::max(1.0, vp.plotRect().width());

    const double *px = nullptr;
    const double *py = nullptr;
    const bool direct = data.directAccess(&px, &py);

    for (qsizetype i = 0; i < n; ++i)
    {
        const double x = direct ? px[i] : data.xAt(i);
        const double y = direct ? py[i] : data.yAt(i);
        // 缺测/NaN 不参与聚合，也不置列命中：纯 NaN 列不产点，
        // 其两侧列在绘制端被时间间隙阈值判为 gap（缺测断线，不连直线）。
        // 若不拦，首个 NaN 会毒化该列 min/max 且永不恢复（std::min 对 NaN 返回旧值，
        // 但被 NaN 种子过的列 isfinite 检查全灭）。
        if (!std::isfinite(x) || !std::isfinite(y))
            continue;
        const double sx = vp.xToPixel(x);
        int col = static_cast<int>((sx - left) / width * columns);
        if (col < 0)
            col = 0;
        else if (col >= columns)
            col = columns - 1;

        if (!colHit[col])
        {
            colHit[col] = true;
            colX[col] = x;
            colMin[col] = y;
            colMax[col] = y;
        }
        else
        {
            colMin[col] = std::min(colMin[col], y);
            colMax[col] = std::max(colMax[col], y);
        }
    }

    int count = 0;
    for (int c = 0; c < columns; ++c)
        count += colHit[c] ? 1 : 0;
    m_points.reserve(count);
    for (int c = 0; c < columns; ++c)
    {
        if (colHit[c])
            m_points.push_back(LodPoint{colX[c], colMin[c], colMax[c]});
    }
    // 乱序数据（非时间单调）会让折线乱飞：按 x 排一次，代价 O(列数)。
    std::sort(m_points.begin(), m_points.end(),
              [](const LodPoint &a, const LodPoint &b) { return a.x < b.x; });

    m_valid = true;
    m_revision = revision;
    m_rect = vp.plotRect();
    m_xLo = vp.xLower();
    m_xHi = vp.xUpper();
    m_columns = columns;
    return m_points;
}

void LodCache::clear()
{
    m_points.clear();
    m_valid = false;
    m_revision = ~0ull;
    m_columns = 0;
}

} // namespace nextpilot::plotscope::fastplot

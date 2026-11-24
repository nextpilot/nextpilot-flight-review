#include "fastplot/Line.h"

#include <algorithm>
#include <vector>

namespace nextpilot::plotscope::fastplot
{

double Line::gapThreshold() const
{
    if (m_lodGapThreshold >= 0.0)
        return m_lodGapThreshold;
    double threshold = 0.0;
    if (m_lod.size() > 2)
    {
        std::vector<double> gaps;
        gaps.reserve(size_t(m_lod.size() - 1));
        for (qsizetype i = 1; i < m_lod.size(); ++i)
        {
            const double g = m_lod[i].x - m_lod[i - 1].x;
            if (g > 0.0)
                gaps.push_back(g);
        }
        if (!gaps.empty())
        {
            std::sort(gaps.begin(), gaps.end());
            const double median = gaps[gaps.size() / 2];
            if (median > 0.0)
                threshold = median * 3.0;
        }
    }
    m_lodGapThreshold = threshold;
    return threshold;
}

Line Line::fromData(const IPlotData *data, const QColor &ink, qreal lineWidth)
{
    Line c;
    c.m_data = data;
    c.m_ink = ink;
    c.m_lineWidth = lineWidth;
    return c;
}

Line Line::fromLod(const QVector<LodPoint> &lod, const QColor &ink, qreal lineWidth)
{
    Line c;
    c.m_data = nullptr;
    c.m_lod = lod;
    c.m_ink = ink;
    c.m_lineWidth = lineWidth;
    return c;
}

} // namespace nextpilot::plotscope::fastplot

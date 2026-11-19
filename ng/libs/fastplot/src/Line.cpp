#include "fastplot/Line.h"

namespace nextpilot::plotscope::fastplot
{

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

#include "fastplot/Curve.h"

namespace fastplot
{

Curve Curve::fromData(const IPlotData *data, const QColor &ink, qreal lineWidth)
{
    Curve c;
    c.m_data = data;
    c.m_ink = ink;
    c.m_lineWidth = lineWidth;
    return c;
}

Curve Curve::fromLod(const QVector<LodPoint> &lod, const QColor &ink, qreal lineWidth)
{
    Curve c;
    c.m_data = nullptr;
    c.m_lod = lod;
    c.m_ink = ink;
    c.m_lineWidth = lineWidth;
    return c;
}

} // namespace fastplot

#include "fastplot/PlotWidget.h"

#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace fastplot
{

namespace
{
int sideIndex(AxisSide side)
{
    switch (side)
    {
    case AxisSide::Left: return 0;
    case AxisSide::Right: return 1;
    case AxisSide::Bottom: return 2;
    case AxisSide::Top: return 3;
    }
    return 0;
}
// 画"每列一条竖线 + 相邻中点连线"的包络：
// 密集数据看起来是一条实心带（正确的视觉语义），稀疏数据退回普通折线（见 paintCurves）。
void drawEnvelope(QPainter &painter, const Viewport &vp, const QVector<LodPoint> &pts)
{
    if (pts.isEmpty())
        return;
    QPolygonF mid;
    mid.reserve(pts.size());
    for (const LodPoint &pt : pts)
    {
        const double px = vp.xToPixel(pt.x);
        const double pyMin = vp.yToPixel(pt.min);
        const double pyMax = vp.yToPixel(pt.max);
        painter.drawLine(QPointF(px, pyMin), QPointF(px, pyMax));
        mid.push_back(QPointF(px, (pyMin + pyMax) / 2.0));
    }
    if (mid.size() > 1)
        painter.drawPolyline(mid);
}

} // namespace

PlotWidget::PlotWidget(QWidget *parent)
    : QWidget(parent)
    , m_axes{Axis(AxisSide::Left), Axis(AxisSide::Right), Axis(AxisSide::Bottom), Axis(AxisSide::Top)}
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // P1 默认只要左轴 + 下轴；右轴/上轴按需 setVisible(true)。
    m_axes[sideIndex(AxisSide::Left)].setVisible(true);
    m_axes[sideIndex(AxisSide::Bottom)].setVisible(true);
    m_axes[sideIndex(AxisSide::Right)].setVisible(false);
    m_axes[sideIndex(AxisSide::Top)].setVisible(false);

    m_vp.setXRange(0.0, 1.0);
    m_vp.setYRange(0.0, 1.0);
    relayout();
}

PlotWidget::~PlotWidget() = default;

Axis *PlotWidget::axisPtr(AxisSide side)
{
    return &m_axes[sideIndex(side)];
}

Axis &PlotWidget::axis(AxisSide side)
{
    return *axisPtr(side);
}

const Axis &PlotWidget::axis(AxisSide side) const
{
    return m_axes[sideIndex(side)];
}

void PlotWidget::setXRange(double lo, double hi)
{
    m_vp.setXRange(lo, hi);
    relayout();
}

void PlotWidget::setYRange(double lo, double hi)
{
    m_vp.setYRange(lo, hi);
    relayout();
}

void PlotWidget::setBackground(const QColor &color)
{
    m_background = color;
    invalidateStatic();
}

void PlotWidget::setGridVisible(bool visible)
{
    for (Axis &a : m_axes)
        a.setGridVisible(visible);
    invalidateStatic();
}

void PlotWidget::replot()
{
    relayout();
}

Curve &PlotWidget::addCurve(Curve curve)
{
    m_curves.push_back(curve);
    update();
    return m_curves.last();
}

void PlotWidget::clearCurves()
{
    m_curves.clear();
    update();
}

bool PlotWidget::autoScaleY(double marginRatio)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const Curve &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        if (c.isLod())
        {
            for (const LodPoint &pt : c.m_lod)
            {
                lo = std::min(lo, pt.min);
                hi = std::max(hi, pt.max);
            }
            continue;
        }
        const IPlotData *data = c.m_data;
        if (!data || data->size() <= 0)
            continue;
        const double *px = nullptr;
        const double *py = nullptr;
        const bool direct = data->directAccess(&px, &py);
        for (qsizetype i = 0; i < data->size(); ++i)
        {
            const double y = direct ? py[i] : data->yAt(i);
            lo = std::min(lo, y);
            hi = std::max(hi, y);
        }
    }
    if (!std::isfinite(lo) || !std::isfinite(hi))
        return false;
    if (lo == hi)
    {
        const double pad = std::fabs(lo) > 1.0 ? std::fabs(lo) * 0.05 : 1.0;
        lo -= pad;
        hi += pad;
    }
    else
    {
        const double pad = (hi - lo) * marginRatio;
        lo -= pad;
        hi += pad;
    }
    setYRange(lo, hi);
    return true;
}

bool PlotWidget::autoScaleX(double marginRatio)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const Curve &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        if (c.isLod())
        {
            for (const LodPoint &pt : c.m_lod)
            {
                lo = std::min(lo, pt.x);
                hi = std::max(hi, pt.x);
            }
            continue;
        }
        const IPlotData *data = c.m_data;
        if (!data || data->size() <= 0)
            continue;
        const double *px = nullptr;
        const double *py = nullptr;
        const bool direct = data->directAccess(&px, &py);
        for (qsizetype i = 0; i < data->size(); ++i)
        {
            const double x = direct ? px[i] : data->xAt(i);
            lo = std::min(lo, x);
            hi = std::max(hi, x);
        }
    }
    if (!std::isfinite(lo) || !std::isfinite(hi))
        return false;
    if (lo == hi)
    {
        const double pad = std::fabs(lo) > 1.0 ? std::fabs(lo) * 0.02 : 1.0;
        lo -= pad;
        hi += pad;
    }
    else
    {
        const double pad = (hi - lo) * marginRatio;
        lo -= pad;
        hi += pad;
    }
    setXRange(lo, hi);
    return true;
}

bool PlotWidget::autoScale()
{
    // 先定 x 再定 y：刻度密度依赖绘图区尺寸，setXRange 会触发 relayout。
    const bool x = autoScaleX();
    const bool y = autoScaleY();
    return x && y;
}

QRectF PlotWidget::dataBounds() const
{
    double xLo = std::numeric_limits<double>::infinity();
    double xHi = -std::numeric_limits<double>::infinity();
    double yLo = xLo, yHi = xHi;
    for (const Curve &c : m_curves)
    {
        if (!c.isVisible())
            continue;
        if (c.isLod())
        {
            for (const LodPoint &pt : c.m_lod)
            {
                xLo = std::min(xLo, pt.x);
                xHi = std::max(xHi, pt.x);
                yLo = std::min(yLo, pt.min);
                yHi = std::max(yHi, pt.max);
            }
        }
    }
    if (!std::isfinite(xLo))
        return QRectF();
    return QRectF(QPointF(xLo, yLo), QPointF(xHi, yHi));
}

void PlotWidget::paintCurves(QPainter &painter)
{
    const QRectF plot = m_vp.plotRect();
    const int columns = std::max(1, static_cast<int>(std::lround(plot.width())));

    painter.save();
    painter.setClipRect(plot);

    for (Curve &c : m_curves)
    {
        if (!c.isVisible())
            continue;

        QPen pen(c.m_ink, c.m_lineWidth);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);

        if (c.isLod())
        {
            // 内核已分桶：直接画，一行数据都不用扫。
            drawEnvelope(painter, m_vp, c.m_lod);
            continue;
        }

        const IPlotData *data = c.m_data;
        if (!data || data->size() <= 0)
            continue;

        if (data->size() <= columns)
        {
            // 稀疏数据不聚合：逐点折线更真实（聚合会把峰值"抹平"到列内）。
            const double *px = nullptr;
            const double *py = nullptr;
            const bool direct = data->directAccess(&px, &py);
            QPolygonF poly;
            poly.reserve(data->size());
            for (qsizetype i = 0; i < data->size(); ++i)
            {
                const double x = direct ? px[i] : data->xAt(i);
                const double y = direct ? py[i] : data->yAt(i);
                poly.push_back(m_vp.toPixel(x, y));
            }
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.drawPolyline(poly);
            painter.setRenderHint(QPainter::Antialiasing, false);
            continue;
        }

        // 密集数据：像素列 min/max 包络，绘制成本 O(像素宽)。
        const QVector<LodPoint> &pts = c.m_cache.points(*data, m_vp);
        drawEnvelope(painter, m_vp, pts);
    }

    painter.restore();
}

void PlotWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void PlotWidget::relayout()
{
    int left = m_axes[sideIndex(AxisSide::Left)].preferredMargin();
    int right = m_axes[sideIndex(AxisSide::Right)].preferredMargin();
    int top = m_axes[sideIndex(AxisSide::Top)].preferredMargin();
    int bottom = m_axes[sideIndex(AxisSide::Bottom)].preferredMargin();

    // 控件太小时优先保住绘图区，把边距压到 0。
    if (width() - left - right < 40)
    {
        left = std::min(left, 24);
        right = std::min(right, 24);
    }
    if (height() - top - bottom < 30)
    {
        top = std::min(top, 8);
        bottom = std::min(bottom, 16);
    }

    m_vp.setPlotRect(QRectF(left, top,
                            std::max(1, width() - left - right),
                            std::max(1, height() - top - bottom)));
    refreshScaleDivs();
    invalidateStatic();
}

void PlotWidget::refreshScaleDivs()
{
    const QRectF plot = m_vp.plotRect();
    // 刻度密度按像素算：横向 72px 一个主刻度、纵向 48px 一个，避免标签重叠。
    const int xTicks = std::clamp(static_cast<int>(plot.width() / 72.0), 2, 12);
    const int yTicks = std::clamp(static_cast<int>(plot.height() / 48.0), 2, 12);

    const ScaleDiv xDiv = ScaleDiv::linear(m_vp.xLower(), m_vp.xUpper(), xTicks);
    const ScaleDiv yDiv = ScaleDiv::linear(m_vp.yLower(), m_vp.yUpper(), yTicks);

    m_axes[sideIndex(AxisSide::Bottom)].setScaleDiv(xDiv);
    m_axes[sideIndex(AxisSide::Top)].setScaleDiv(xDiv);
    m_axes[sideIndex(AxisSide::Left)].setScaleDiv(yDiv);
    m_axes[sideIndex(AxisSide::Right)].setScaleDiv(yDiv);
}

void PlotWidget::invalidateStatic()
{
    m_staticDirty = true;
    update();
}

void PlotWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const QSize cacheSize = size() * devicePixelRatioF();
    if (m_staticDirty || m_staticCache.size() != cacheSize)
    {
        m_staticCache = QPixmap(cacheSize);
        m_staticCache.setDevicePixelRatio(devicePixelRatioF());
        m_staticCache.fill(m_background);

        QPainter p(&m_staticCache);
        p.setRenderHint(QPainter::Antialiasing, false);

        // 网格先画（在曲线之下），轴后画（在网格之上）。
        for (const Axis &a : m_axes)
            a.paintGrid(&p, m_vp);
        for (const Axis &a : m_axes)
            a.paint(&p, m_vp, rect());
        p.end();

        m_staticDirty = false;
    }

    QPainter painter(this);
    painter.drawPixmap(0, 0, m_staticCache);
    // 曲线画在静态图层之上；聚合结果按（数据版本, 视口, 列数）缓存，
    // 悬停/重绘不重扫数据。
    paintCurves(painter);
}

QImage PlotWidget::grabImage()
{
    QImage image(size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    render(&image);
    return image;
}

} // namespace fastplot

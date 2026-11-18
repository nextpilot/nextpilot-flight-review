#include "fastplot/PlotWidget.h"

#include <QPainter>
#include <QResizeEvent>

#include <algorithm>

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
    // P2 起：曲线层画在静态缓存之上，只重绘脏区。
}

QImage PlotWidget::grabImage()
{
    QImage image(size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    render(&image);
    return image;
}

} // namespace fastplot

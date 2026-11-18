#pragma once

#include "fastplot/ScaleDiv.h"

#include <QColor>
#include <QFont>

class QPainter;

namespace fastplot
{

class Viewport;

enum class AxisSide
{
    Left,
    Right,
    Bottom,
    Top
};

// 一条轴：位置、刻度线、标签、网格开关、标题。
//
// 轴不知道自己对应哪条曲线——曲线通过"用哪条轴"来声明（P2）。这样双轴不需要任何
// 特判：左轴一条、右轴一条，都是同一个类的实例。
class Axis
{
public:
    explicit Axis(AxisSide side = AxisSide::Left);

    AxisSide side() const { return m_side; }
    void setSide(AxisSide side) { m_side = side; }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    QColor ink() const { return m_ink; }
    void setInk(const QColor &color) { m_ink = color; }

    QColor labelColor() const { return m_labelColor; }
    void setLabelColor(const QColor &color) { m_labelColor = color; }

    QFont font() const { return m_font; }
    void setFont(const QFont &font) { m_font = font; }

    int tickLength() const { return m_tickLength; }
    void setTickLength(int px) { m_tickLength = px; }

    bool gridVisible() const { return m_grid; }
    void setGridVisible(bool visible) { m_grid = visible; }

    QColor gridInk() const { return m_gridInk; }
    void setGridInk(const QColor &color) { m_gridInk = color; }

    int minorTicksPerMajor() const { return m_minorPerMajor; }
    void setMinorTicksPerMajor(int n) { m_minorPerMajor = n < 0 ? 0 : n; }

    const ScaleDiv &scaleDiv() const { return m_div; }
    void setScaleDiv(const ScaleDiv &div) { m_div = div; }

    // 该轴需要预留的边距（像素）：标签尺寸 + 刻度线 + 间隙。
    int preferredMargin() const;

    // 刻度线 + 标签（不画网格）。
    void paint(QPainter *painter, const Viewport &vp, const QRect &widgetRect) const;

    // 绘图区内的网格线（只画主刻度）。
    void paintGrid(QPainter *painter, const Viewport &vp) const;

private:
    AxisSide m_side;
    bool m_visible = true;
    bool m_grid = true;
    int m_tickLength = 4;
    int m_minorPerMajor = 3;
    QColor m_ink{0x84, 0x94, 0xa0};
    QColor m_labelColor{0xc9, 0xd1, 0xd9};
    QColor m_gridInk{0x1c, 0x24, 0x30};
    QFont m_font;
    ScaleDiv m_div;
};

} // namespace fastplot

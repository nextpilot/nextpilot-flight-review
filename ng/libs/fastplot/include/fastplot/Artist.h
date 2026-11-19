#pragma once

#include <QtGlobal>
#include <QString>

class QPainter;
class QRectF;

namespace nextpilot::plotscope::fastplot
{

class Axes;

// artist —— 画在 Axes 上的一个对象。术语对齐 matplotlib.artist.Artist / MATLAB 图形对象。
//
// 与 QWidget 无关：artist 只是"知道如何把自己画到坐标系里"的逻辑对象。
// 这让它既能被 Axes 组合绘制（bar/hist/spectrum），也能复用给离屏导出；
// 而交互部件（Zoomer/Panner/Picker）和仪表盘（Instrument 族）不属于 artist。
//
// artist 不持有 visible 状态——显隐由宿主 Axes 统一管理，避免与 QWidget::isVisible 二义。
// 绘制两种粒度，子类按需实现（默认空）：
//   - paint：给一个 Axes，artist 自己从 axes.viewport() 取坐标映射（Line 走这条）；
//   - paintInRect：只给一块像素矩形，与数据坐标无关（柱状/直方图这类"已聚合"artist 走这条）。
class Artist
{
public:
    virtual ~Artist() = default;

    // 图例里显示的名字（空 = 不进图例）。
    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    // 带坐标系的绘制（曲线类）。默认什么都不画。
    virtual void paint(QPainter &painter, const Axes &axes) const
    {
        Q_UNUSED(painter);
        Q_UNUSED(axes);
    }

    // 给定像素区域的绘制（已聚合类，如柱/直方图/频谱）。默认什么都不画。
    virtual void paintInRect(QPainter &painter, const QRectF &rect) const
    {
        Q_UNUSED(painter);
        Q_UNUSED(rect);
    }

private:
    QString m_name;
};

} // namespace nextpilot::plotscope::fastplot

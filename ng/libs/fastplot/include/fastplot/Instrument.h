#pragma once

#include <QColor>
#include <QWidget>

class QPainter;
class QPaintEvent;

namespace nextpilot::plotscope::fastplot
{

// 仪表的公共底座：一块底色 + 一块"内容区"，子类只画内容。
//
// 为什么单独成族：仪表不是曲线画布——它没有坐标轴、没有缩放、没有 LOD，
// 拿到的只是一个（或几条）标量/序列。硬塞进 PlotWidget 会把两种模型混在一起，
// 所以仪表自己一个基类，PlotWidget 不认识它们。
class Instrument : public QWidget
{
    Q_OBJECT
public:
    explicit Instrument(QWidget *parent = nullptr);

    QColor background() const { return m_background; }
    void setBackground(const QColor &color);

    QColor ink() const { return m_ink; }
    void setInk(const QColor &color);

    QColor labelInk() const { return m_labelInk; }
    void setLabelInk(const QColor &color);

    QColor accent() const { return m_accent; }
    void setAccent(const QColor &color);

    int margin() const { return m_margin; }
    void setMargin(int px);

protected:
    void paintEvent(QPaintEvent *event) override;
    virtual void paintInstrument(QPainter &painter, const QRectF &rect) = 0;

    QColor m_background{0x10, 0x16, 0x1c};
    QColor m_ink{0x84, 0x94, 0xa0};
    QColor m_labelInk{0xc9, 0xd1, 0xd9};
    QColor m_accent{0x5b, 0xc0, 0xf0};
    int m_margin = 12;
};

} // namespace nextpilot::plotscope::fastplot

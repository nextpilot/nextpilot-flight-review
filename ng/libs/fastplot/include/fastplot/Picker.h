#pragma once

#include <QObject>
#include <QPointF>

namespace nextpilot::plotscope::fastplot
{

class Axes;

// 读数部件：鼠标在绘图区悬停时取最近样本，画十字光标 + 右上角读数。
// 只发信号，不写文字格式（格式属于视图层，库不管"秒/度/伏"这些单位）。
class Picker : public QObject
{
    Q_OBJECT
public:
    explicit Picker(Axes *plot, QObject *parent = nullptr);

Q_SIGNALS:
    void hovered(double x, double y, bool valid);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clear();

    Axes *m_plot = nullptr;
};

} // namespace nextpilot::plotscope::fastplot

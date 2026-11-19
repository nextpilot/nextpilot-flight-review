#pragma once

#include <QObject>
#include <QPointF>

namespace nextpilot::plotscope::fastplot
{

class Axes;

// 平移部件：左键拖动画布。像素位移换算成数据位移，与缩放正交——
// 两个部件各自只改"范围"，不互相知道对方存在。
class Panner : public QObject
{
    Q_OBJECT
public:
    explicit Panner(Axes *plot, QObject *parent = nullptr);

Q_SIGNALS:
    void panned();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Axes *m_plot = nullptr;
    bool m_dragging = false;
    QPointF m_lastPos;
};

} // namespace nextpilot::plotscope::fastplot

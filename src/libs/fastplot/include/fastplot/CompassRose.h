#pragma once

#include "fastplot/Instrument.h"

namespace nextpilot::plotscope::fastplot
{

// 罗盘：航向角决定指针方向。刻度每 30° 一格，北向高亮。
class CompassRose : public Instrument
{
    Q_OBJECT
public:
    explicit CompassRose(QWidget *parent = nullptr);

    void setHeading(double headingDeg);
    double headingDeg() const { return m_heading; }

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;

private:
    double m_heading = 0.0;
};

} // namespace nextpilot::plotscope::fastplot

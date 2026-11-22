#pragma once

#include "fastplot/Instrument.h"

namespace nextpilot::plotscope::fastplot
{

// 姿态球（地平仪）：滚转决定倾斜、俯仰决定地平线上下平移。
// 输入是**两个角度标量**，与数据长度无关，所以按值传参即可。
class AttitudeIndicator : public Instrument
{
    Q_OBJECT
public:
    explicit AttitudeIndicator(QWidget *parent = nullptr);

    void setRollPitch(double rollDeg, double pitchDeg);
    double rollDeg() const { return m_roll; }
    double pitchDeg() const { return m_pitch; }

    void setSkyColor(const QColor &color);
    void setGroundColor(const QColor &color);

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;

private:
    double m_roll = 0.0;
    double m_pitch = 0.0;
    QColor m_sky{0x2f, 0x6f, 0xb0};
    QColor m_ground{0x8a, 0x5a, 0x2b};
};

} // namespace nextpilot::plotscope::fastplot

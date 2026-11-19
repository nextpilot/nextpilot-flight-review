#pragma once

#include "fastplot/Instrument.h"

namespace nextpilot::plotscope::fastplot
{

// 表盘：270° 弧 + 指针 + 中央数值。量程由调用方给（仪表不猜数据含义）。
class DialGauge : public Instrument
{
    Q_OBJECT
public:
    explicit DialGauge(QWidget *parent = nullptr);

    void setValue(double value);
    void setValueMissing();            // 无数据：显示 "--"，指针停在起点
    void setRange(double lo, double hi);
    void setTitle(const QString &title);

    double value() const { return m_value; }
    bool hasValue() const { return m_hasValue; }
    double lower() const { return m_lo; }
    double upper() const { return m_hi; }

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;

private:
    double m_value = 0.0;
    bool m_hasValue = false;
    double m_lo = 0.0;
    double m_hi = 1.0;
    QString m_title;
};

} // namespace nextpilot::plotscope::fastplot

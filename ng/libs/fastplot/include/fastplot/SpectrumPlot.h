#pragma once

#include "Instrument.h"
#include "PlotData.h"

#include <QColor>
#include <QVector>

namespace fastplot
{

// 频谱图：对一条序列去均值后做单边 DFT，画幅值谱（对应老画布 Spectrum 模式）。
//
// 注意：当前是朴素 DFT（O(n²)），与老画布口径一致，快速原型阶段够用；
// 点数上来后应换成 FFT。频率轴按采样间隔 dt（秒）换算，dt 缺省时归一化到 0..1。
class SpectrumPlot : public Instrument
{
    Q_OBJECT
public:
    explicit SpectrumPlot(QWidget *parent = nullptr);

    void setData(const IPlotData *data);
    // 采样间隔（秒）；<=0 表示未知，频率轴按序号归一化。
    void setSampleDt(double dt);
    void setColor(const QColor &color);

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;

private:
    void rebuild() const;

    const IPlotData *m_data = nullptr;
    double m_dt = 0.0;
    QColor m_color;

    mutable QVector<double> m_amp;   // 单边幅值
    mutable double m_fMax = 1.0;
    mutable double m_ampMax = 0.0;
    mutable quint64 m_revision = 0;
    mutable qsizetype m_n = 0;
};

} // namespace fastplot

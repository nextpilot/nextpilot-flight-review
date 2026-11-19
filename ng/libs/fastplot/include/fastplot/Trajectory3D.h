#pragma once

#include "fastplot/Instrument.h"

#include <QVector>

namespace nextpilot::plotscope::fastplot
{

class IPlotData;

// 三维轨迹：三条通道（X/Y/Z，Z 可空）经正交投影成折线 + 参考立方体。
//
// 只用 painter 的正交投影，不引 OpenGL：老画布也是这么画的，这是"看形状"的仪表，
// 不是渲染引擎。真要旋转交互另立部件，别把相机塞进来。
class Trajectory3D : public Instrument
{
    Q_OBJECT
public:
    explicit Trajectory3D(QWidget *parent = nullptr);

    // 非拥有：直读调用方的数组。
    void setTracks(const IPlotData *x, const IPlotData *y, const IPlotData *z = nullptr,
                   const QString &xLabel = QString(), const QString &yLabel = QString(),
                   const QString &zLabel = QString());
    void clearTracks();

    void setCurveColor(const QColor &color);

protected:
    void paintInstrument(QPainter &painter, const QRectF &rect) override;

private:
    const IPlotData *m_x = nullptr;
    const IPlotData *m_y = nullptr;
    const IPlotData *m_z = nullptr;
    QString m_xLabel;
    QString m_yLabel;
    QString m_zLabel;
    QColor m_curve{0x5b, 0xc0, 0xf0};
};

} // namespace nextpilot::plotscope::fastplot

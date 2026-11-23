#pragma once

#include "fastplot/Overlay.h"

namespace nextpilot::plotscope::fastplot
{

// Marker 规则产出：事件竖线/点、时间区间带、值区间带。
struct MarkerMark
{
    enum class Kind { Event, Region, ValueBand } kind = Kind::Event;
    enum class Severity { Info, Warning, Error, Critical } severity = Severity::Info;
    double tStart = 0.0;
    double tEnd = 0.0;
    double valueLow = 0.0;
    double valueHigh = 0.0;
    bool hasValue = false;
    QString label;
    QColor color; // 无效则用 severity 色
};

// Marker 叠加层（PJ4 PlotMarkersItem 的对应物）：状态由 Axes::setMarkers 喂入。
class MarkerOverlay : public Overlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override;

    const QVector<MarkerMark> &marks() const { return m_marks; }
    void setMarks(const QVector<MarkerMark> &marks) { m_marks = marks; }

private:
    QVector<MarkerMark> m_marks;
};

} // namespace nextpilot::plotscope::fastplot

#pragma once

#include "fastplot/Overlay.h"

namespace nextpilot::plotscope::fastplot
{

// 十字游标 + 逐曲线读数框（对齐老 PlottingCanvas 的读数块；MATLAB DataTip /
// PJ4 CurveTracker 的对应物）。状态由 Axes::setCursorReadout 喂入；
// 绘制时按 context 避让图例块与 A/B 测量框。
class CursorReadoutOverlay : public Overlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override;

    // ---- 状态 ----
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }
    double x() const { return m_x; }
    void set(double x, bool active, bool pinned, const QString &header,
             const QVector<ReadoutRow> &rows);
    const QVector<ReadoutRow> &rows() const { return m_rows; }

private:
    double m_x = 0.0;
    bool m_active = false;
    bool m_pinned = false;
    QString m_header;
    QVector<ReadoutRow> m_rows;
};

} // namespace nextpilot::plotscope::fastplot

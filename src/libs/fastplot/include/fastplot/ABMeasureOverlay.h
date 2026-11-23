#pragma once

#include "fastplot/Overlay.h"

namespace nextpilot::plotscope::fastplot
{

// A/B 双游标测量（对齐老 PlottingCanvas：区间带 + ΔX 徽标 + A/B 直角三角 +
// 逐曲线 Δy/k 读数框）。MATLAB 无原生对应，PJ4 也未拆——但它在 Axes 里是
// 独立的状态域（ enabled/a/b/rows/triangle），按 Overlay 模式拆出。
class ABMeasureOverlay : public Overlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override;

    // ---- 状态 ----
    bool isEnabled() const { return m_enabled; }
    // ay/by 为首条可见曲线 A/B 两点的像素 y（直角三角形用，由上层换算传入）。
    void set(bool enabled, double a, double b, bool hasB, const QString &header,
             const QVector<ReadoutRow> &rows, bool drawTriangle, double ay, double by);
    const QVector<ReadoutRow> &rows() const { return m_rows; }
    void setRows(const QVector<ReadoutRow> &rows) { m_rows = rows; } // 导出时临时清空

private:
    bool m_enabled = false;
    double m_a = 0.0;
    double m_b = 0.0;
    bool m_hasB = false;
    QString m_header;
    QVector<ReadoutRow> m_rows;
    bool m_triangle = false;
    double m_ay = 0.0;
    double m_by = 0.0;
};

} // namespace nextpilot::plotscope::fastplot

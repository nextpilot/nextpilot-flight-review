#pragma once

#include "fastplot/Overlay.h"

#include <QElapsedTimer>

namespace nextpilot::plotscope::fastplot
{

// 十字游标 + 逐曲线读数框（对齐老 PlottingCanvas 的读数块；MATLAB DataTip /
// PJ4 CurveTracker 的对应物）。状态由 Axes::setCursorReadout 喂入；
// 绘制时按 context 避让图例块与 A/B 测量框。
//
// 第3仗 3c（E-4，对标 PJ4 CurveTracker.cpp:282-367）：
//   - 值框纵向跟随游标（setCursorPixelY 喂入锚点），限速 200px/s 滑动；
//   - 大跳（>20px，seek/翻边）直接 snap 不滑动；
//   - boxAnchorY clamp：整框不出画布（上/下双向钳制 + 越界翻边）。
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
    // 游标像素 Y（框的纵向锚点）；<0 = 无锚点，框回顶部锚位。
    void setCursorPixelY(double pixelY) { m_anchorY = pixelY; }
    // 测试/调试：最近一次 paint 的读数框矩形。
    QRectF boxRectForTest() const { return m_boxRect; }

private:
    double stepBoxTop(double targetTop, const QRectF &plot);

    double m_x = 0.0;
    bool m_active = false;
    bool m_pinned = false;
    QString m_header;
    QVector<ReadoutRow> m_rows;
    double m_anchorY = -1.0;
    // 框滑动状态（限速跟随）：
    bool m_gliding = false;
    double m_boxTopCurrent = 0.0;
    QElapsedTimer m_glideClock;
    QRectF m_boxRect; // 最近一次绘制的框（boxRectForTest）
};

} // namespace nextpilot::plotscope::fastplot

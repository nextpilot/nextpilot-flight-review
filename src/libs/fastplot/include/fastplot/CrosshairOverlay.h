#pragma once

#include "fastplot/Overlay.h"

namespace nextpilot::plotscope::fastplot
{

// 交叉线族叠加层：右上角浮字、悬浮十字线、播放头竖线、联动游标竖线、
// 框选橡皮筋（MATLAB annotation / PJ4 PlotFocusOverlay 的对应物）。
//
// 绘制分两阶段（Axes 按序调用，保持与抽取前完全相同的叠层顺序）：
//   paint()           —— 浮字 + 悬浮十字 + 播放头 + 联动游标（读数框等之前）
//   paintRubberBand() —— 橡皮筋（读数框等之后，框选必须盖在最上）
class CrosshairOverlay : public Overlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override;
    void paintRubberBand(QPainter &painter, const OverlayContext &ctx);

    // ---- 状态（"画什么"由上层喂入）----
    // 橡皮筋（像素坐标）。空 QRect = 清除。
    void setRubberBand(const QRect &pixels) { m_rubberBand = pixels; }
    QRect rubberBand() const { return m_rubberBand; }
    // 悬浮十字：给一个数据坐标；active=false 时隐藏。
    void setHover(const QPointF &dataPoint, bool active)
    { m_hoverPoint = dataPoint; m_hoverActive = active; }
    // 播放头竖线：给一个 x 数据坐标；active=false 时隐藏。
    void setPlayhead(double x, bool active)
    { m_playheadX = x; m_playheadActive = active; }
    bool playheadActive() const { return m_playheadActive; }
    double playheadX() const { return m_playheadX; }
    // 联动游标竖线：同页其它画布的时间同步，只画竖线不画读数框。
    void setLinkedCursor(double x, bool active)
    { m_linkedX = x; m_linkedActive = active; }
    // 右上角读数文本（空串 = 不画）。
    void setOverlayText(const QString &text) { m_overlayText = text; }
    // 清瞬时态（悬浮十字、浮字、橡皮筋）。
    void clearTransient()
    { m_hoverActive = false; m_overlayText.clear(); m_rubberBand = QRect(); }

private:
    QRect m_rubberBand;
    QPointF m_hoverPoint;
    bool m_hoverActive = false;
    double m_playheadX = 0.0;
    bool m_playheadActive = false;
    double m_linkedX = 0.0;
    bool m_linkedActive = false;
    QString m_overlayText;
};

} // namespace nextpilot::plotscope::fastplot

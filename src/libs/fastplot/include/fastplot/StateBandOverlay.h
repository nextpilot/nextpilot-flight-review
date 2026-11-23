#pragma once

#include "fastplot/Overlay.h"

namespace nextpilot::plotscope::fastplot
{

// 状态视图：每条通道一行色带，每段是一个连续不变的状态
// （label 为着色键，见 StateColors.h）。
struct StateBandMark
{
    double tStart = 0.0;
    double tEnd = 0.0;
    QString label; // 状态标签（bool→"true"/"false"、枚举→十进制串、string 原文）
    QColor color;  // 无效则用 stateColor(label)
    bool operator==(const StateBandMark &o) const
    { return tStart == o.tStart && tEnd == o.tEnd && label == o.label && color == o.color; }
};
struct StateRowMark
{
    QString channel; // 行名（"table.column"）
    QVector<StateBandMark> segments;
    bool operator==(const StateRowMark &o) const
    { return channel == o.channel && segments == o.segments; }
};

// 状态色带叠加层：每行画在绘图区上方的预留带里（band 22px + gap 6px），
// 顶部预留空间由 Axes::relayout 按 rowCount() 计算——两层按同一常量约定。
class StateBandOverlay : public Overlay
{
public:
    void paint(QPainter &painter, const OverlayContext &ctx) override;

    const QVector<StateRowMark> &rows() const { return m_rows; }
    void setRows(const QVector<StateRowMark> &rows) { m_rows = rows; }
    int rowCount() const { return m_rows.size(); }

private:
    QVector<StateRowMark> m_rows;
};

} // namespace nextpilot::plotscope::fastplot

#pragma once

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QVector>

#include "fastplot/Viewport.h"

namespace nextpilot::plotscope::fastplot
{

// 逐曲线读数行（十字游标读数框 / A-B 测量共用）：label + value + 曲线色。
// 取值/格式化在 AxesView 做（库不依赖 Dataset）。
struct ReadoutRow
{
    QString label;
    QString value;
    QColor color;
};

// 叠加层公共绘制上下文：Axes 每帧组装一次，各叠加层只读。
struct OverlayContext
{
    const Viewport *vp = nullptr;  // 坐标映射（toPixel / yToPixel）
    QRectF plotRect;
    QFont font;                    // Axes 当前字体
    QColor labelColor;             // 左轴文字色（读数文本）
    QColor axisInk;                // 下轴 ink（行名/橡皮筋边框）
    // 兄弟叠加层避让信息（十字游标读数框用）：
    QRectF legendBlockRect;        // 图例整块矩形（空 = 图例不可见）
    bool abActive = false;         // A/B 激活时读数框顶部下移避让
};

// 画布叠加层基类（MATLAB annotation / PJ4 CurveTracker·PlotMarkersItem 的对应物）：
// 状态自持 + paint 只吃 OverlayContext 快照，不回指 Axes（与 Legend 同一模式）。
// Axes 按固定顺序调用各叠加层（色带 → Marker → 书签 → A/B → 读数框，后者画在最上）。
class Overlay
{
public:
    virtual ~Overlay() = default;
    virtual void paint(QPainter &painter, const OverlayContext &ctx) = 0;
};

} // namespace nextpilot::plotscope::fastplot

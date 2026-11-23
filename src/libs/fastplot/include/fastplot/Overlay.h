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

// 叠加层脏标记位：Axes 按"哪些层状态变了"决定是否重建稳态层缓存。
// 稳态层（StateBand/Marker/Bookmark/AB）内容不随游标移动变化，缓存复用；
// 游标层（Crosshair/CursorReadout）随鼠标每帧变化，始终实时绘制。
// 注意：这是 QPainter 场景的自研分层（我们无 GPU canvas），不是 PJ4 retained layer 的等价物。
enum OverlayLayer : quint32
{
    OverlayLayerNone        = 0,
    OverlayLayerCrosshair   = 1u << 0,  // 悬浮十字/播放头/联动游标/橡皮筋/浮字
    OverlayLayerStateBand   = 1u << 1,  // 状态视图色带
    OverlayLayerMarker      = 1u << 2,  // Marker 规则产出
    OverlayLayerBookmark    = 1u << 3,  // 时间书签
    OverlayLayerABMeasure   = 1u << 4,  // A/B 双游标测量
    OverlayLayerCursorRead  = 1u << 5,  // 十字游标读数框
    // 稳态组：这些层内容不随游标移动变化，可缓存；任一层脏则整组缓存重建。
    OverlayLayerStableMask  = OverlayLayerStateBand | OverlayLayerMarker
                            | OverlayLayerBookmark | OverlayLayerABMeasure,
    OverlayLayerAll         = 0xFFFFFFFFu
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
    // 本帧各层脏标记（供各 Overlay 自行决定是否跳过内部昂贵计算；默认全脏）。
    // 游标层由 Axes 每帧实时绘制，稳态层走缓存——此字段主要给层内部优化用。
    quint32 dirtyLayers = OverlayLayerAll;
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

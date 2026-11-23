#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace nextpilot::plotscope::fastplot
{

// 图例：独立 overlay 组件（对齐 PJ4 PlotLegend 的组织方式——状态、布局、
// 绘制、命中检测封装在一个类里；Axes 只做组装与委托，不内嵌图例状态）。
//
// 零耦合：paint() 只吃一次 PaintInput 快照（曲线行、绘图区、配色、游标实时值），
// 不回指 Axes；绘制的同时缓存行/标题/整块矩形，供命中检测与读数框避让。
//
// 布局：右上角贴边窄块；不做顶部水平横铺（吃掉顶部整行且与读数框抢位置）。
// 命名曲线行数超过单列上限（6 行）自动拆多列（至多 4 列），块高近似减半；
// 多列整块宽上限绘图区 75%，放不下逐级退列；退回单列维持 45% 上限。
// 行序不变；游标实时值按全局命名行序对齐读数框。
class Legend : public QObject
{
    Q_OBJECT
public:
    explicit Legend(QObject *parent = nullptr);

    // 一条图例行（Axes 从曲线列表组装；名字为空的曲线不进图例）。
    struct Item
    {
        int curveIndex = -1; // 对应 Axes 曲线列表索引（命中后据此切显隐）
        QString name;
        QColor ink;
        bool visible = true;
    };
    // 游标实时值单元格（与读数框同源；与 items 同序）。
    struct ValueCell
    {
        QString value;
        QColor color;
    };
    // 一次绘制的输入快照。
    struct PaintInput
    {
        QRectF plotRect;
        QFont baseFont;            // Axes 当前字体（字号缩放因子由本类叠加）
        QColor labelColor;         // 文字色（左轴 labelColor）
        QColor windowColor;        // 块底色（palette window）
        QVector<Item> items;       // 命名曲线行，顺序即行序
        bool cursorActive = false;
        QVector<ValueCell> values; // 游标激活时的实时值（与 items 同序）
    };

    void paint(QPainter &painter, const PaintInput &in);

    // 命中检测（基于最近一次 paint 缓存的几何；resize/paint 后自动更新）：
    // >=0 命中曲线行（返回 Item::curveIndex）；-1 标题栏；-2 未命中/不可见。
    int hitTest(const QPointF &pixel) const;

    // 整块矩形（标题栏 + 所有行）：读数框据此避让。不可见时为空。
    QRectF blockRect() const { return m_blockRect; }

    // ---- 状态 ----
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);     // 关闭时清空几何缓存
    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed); // 变化时 Q_EMIT collapseChanged
    double fontScale() const { return m_fontScale; }
    void setFontScale(double scale);   // clamp [0.7, 2.0]
    void zoomFont(int steps);          // Ctrl+滚轮步进：±0.1
    qreal topInset() const { return m_topInset; }
    void setTopInset(qreal px);        // 顶部让位（悬浮工具栏占位），clamp >= 0

Q_SIGNALS:
    void collapseChanged(bool collapsed);

private:
    bool m_visible = false;
    bool m_collapsed = false;
    double m_fontScale = 1.0;          // 字号缩放因子（Ctrl+滚轮）
    qreal m_topInset = 0.0;
    struct RowRect
    {
        int curveIndex = -1;
        QRectF rect;
    };
    QVector<RowRect> m_rows;  // 最近一次 paint 的行矩形（命中检测）
    QRectF m_titleRect;       // 标题栏矩形（折叠按钮命中）
    QRectF m_blockRect;       // 整块矩形（读数框避让）
};

} // namespace nextpilot::plotscope::fastplot

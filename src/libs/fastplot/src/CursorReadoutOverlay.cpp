#include "fastplot/CursorReadoutOverlay.h"
#include "fastplot/SemanticColors.h"

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{
namespace
{
// 限速跟随参数（对标 PJ4 CurveTracker kLabelSpeedPxPerSec / kLabelSnapJumpPx）：
// 框向锚点滑动最高 200px/s；一帧内目标跳变超过 20px（seek 大跳/翻边）直接 snap。
constexpr double kBoxSpeedPxPerSec = 200.0;
constexpr double kSnapJumpPx = 20.0;
constexpr double kMaxFrameSec = 0.1; // 长时间不 paint 后的首帧，防一步跨半屏
} // namespace

double CursorReadoutOverlay::stepBoxTop(double targetTop, const QRectF &plot)
{
    if (!m_gliding || !m_glideClock.isValid())
    {
        // 首帧/重新激活：直接落位，计时起点重置。
        m_gliding = true;
        m_glideClock.start();
        m_boxTopCurrent = targetTop;
        return targetTop;
    }
    double dt = double(m_glideClock.restart()) / 1000.0;
    dt = qBound(0.0, dt, kMaxFrameSec);
    const double maxStep = kBoxSpeedPxPerSec * dt;
    const double delta = targetTop - m_boxTopCurrent;
    if (std::abs(delta) > kSnapJumpPx || std::abs(delta) <= maxStep)
    {
        m_boxTopCurrent = targetTop; // 大跳 snap / 已到位
        return targetTop;
    }
    m_boxTopCurrent += delta > 0 ? maxStep : -maxStep;
    return m_boxTopCurrent;
}

void CursorReadoutOverlay::set(double x, bool active, bool pinned,
                               const QString &header, const QVector<ReadoutRow> &rows)
{
    m_x = x;
    m_active = active;
    m_pinned = pinned;
    m_header = header;
    m_rows = rows;
    if (!active)
        m_gliding = false; // 关闭后重开：框直接落位，不做跨会话滑动
}

void CursorReadoutOverlay::paint(QPainter &painter, const OverlayContext &ctx)
{
    if (!m_active)
        return;
    const QRectF &plot = ctx.plotRect;
    const Viewport &vp = *ctx.vp;
    const double x = vp.toPixel(m_x, vp.yLower()).x();
    if (x < plot.left() || x > plot.right())
        return;

    painter.save();
    painter.setClipRect(plot);
    QFont f = ctx.font;
    f.setPointSizeF(std::max(7.0, f.pointSizeF()));
    painter.setFont(f);
    const QFontMetrics fm(f);

    // 竖线：固定读数用实线 accent，普通悬停用虚线灰。
    // 固定读数用语义色 plot.cursor（主题 token），兜底沿用历史橙。
    const QColor accent = semanticColor(QStringLiteral("plot.cursor"),
                                        QColor(QStringLiteral("#f2b134")));
    painter.setPen(m_pinned ? QPen(accent, 1.2, Qt::SolidLine)
                            : QPen(QColor(0x88, 0x88, 0x88, 0xC0), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    painter.setBrush(m_pinned ? accent : QColor(0x1F, 0x6F, 0xC0));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(x, plot.top() + 1), 3.0, 3.0);

    // 读数框：贴游标右侧，越界翻到左侧；最多 8 行。
    const int lineH = fm.height() + 2;
    const int boxW = qMin(int(plot.width() - 16), 280);
    const int boxH = 22 + qMin(m_rows.size(), 8) * lineH + 6;
    // 读数框避开图例：图例现在是**右上角**的自适应窄块，不再横铺一整行，
    // 所以不能用"游标靠左就整体下移"的老规则（那会把框白推下去一大截）。
    // 改为几何判定：按默认位置算出框，只有真正与图例矩形相交时才让开。
    double boxTop = plot.top() + (ctx.abActive ? 30 : 8);
    // 纵向锚点（第3仗 3c / E-4）：游标像素 Y 有效时框跟随游标（boxAnchorY 等效），
    // 经限速滑动收敛；无效回顶部锚位（钉住读数/无鼠标路径）。
    if (m_anchorY >= plot.top() && m_anchorY <= plot.bottom())
    {
        const double targetTop =
            qBound(plot.top() + 4.0, m_anchorY - boxH / 2.0, plot.bottom() - 4.0 - boxH);
        boxTop = stepBoxTop(targetTop, plot);
    }
    else
    {
        boxTop = stepBoxTop(qBound(plot.top() + 4.0, boxTop, plot.bottom() - 4.0 - boxH), plot);
    }
    QRectF box(x + 12, boxTop, boxW, boxH);
    if (box.right() > plot.right())
        box.moveLeft(x - 12 - boxW);
    // X/Y 双向避让兜底：翻边/钳制后仍越界（框比画布还宽等极端情形）再收敛一次。
    if (box.bottom() > plot.bottom())
        box.moveTop(plot.bottom() - boxH - 4);
    if (box.top() < plot.top())
        box.moveTop(plot.top() + 4);
    // 真正压到右上角图例时，把框整体挪到图例下方（图例窄，绝大多数游标位置都不触发）。
    if (!ctx.legendBlockRect.isEmpty() && box.intersects(ctx.legendBlockRect))
        box.moveTop(ctx.legendBlockRect.bottom() + 4);
    if (box.bottom() > plot.bottom())
        box.moveTop(plot.bottom() - boxH - 4);
    m_boxRect = box;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x14, 0x1a, 0x20, m_pinned ? 252 : 238));
    painter.drawRoundedRect(box, 3, 3);
    if (m_pinned)
    {
        painter.setPen(QPen(accent, 1));
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }

    painter.setPen(accent);
    painter.drawText(QRectF(box.left() + 8, box.top() + 3, box.width() - 16, lineH),
                     Qt::AlignVCenter | Qt::AlignLeft, m_header);
    painter.setPen(QColor(0x7a, 0x84, 0x8f));
    painter.drawLine(QPointF(box.left() + 7, box.top() + 21),
                     QPointF(box.right() - 7, box.top() + 21));

    int y = int(box.top()) + 23;
    const QColor textColor = ctx.labelColor;
    for (int i = 0; i < m_rows.size() && i < 8; ++i)
    {
        const ReadoutRow &r = m_rows.at(i);
        painter.setPen(r.color);
        painter.drawText(QRectF(box.left() + 8, y, box.width() * 0.55, lineH),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         fm.elidedText(r.label, Qt::ElideMiddle, int(box.width() * 0.55)));
        painter.setPen(textColor);
        painter.drawText(QRectF(box.left() + box.width() * 0.55, y, box.width() * 0.4, lineH),
                         Qt::AlignVCenter | Qt::AlignRight, r.value);
        y += lineH;
    }
    painter.restore();
}

} // namespace nextpilot::plotscope::fastplot

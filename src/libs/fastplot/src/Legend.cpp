#include "fastplot/Legend.h"

#include <QPainter>

#include <algorithm>

namespace nextpilot::plotscope::fastplot
{

Legend::Legend(QObject *parent) : QObject(parent) {}

void Legend::setVisible(bool visible)
{
    m_visible = visible;
    if (!visible)
    {
        // 图例关掉后几何缓存要清空，否则残留的块矩形会把读数框一直往下推。
        m_blockRect = QRectF();
        m_rows.clear();
        m_titleRect = QRectF();
    }
}

void Legend::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;
    Q_EMIT collapseChanged(collapsed);
}

void Legend::setFontScale(double scale)
{
    m_fontScale = qBound(0.7, scale, 2.0);
}

void Legend::zoomFont(int steps)
{
    setFontScale(m_fontScale + steps * 0.1);
}

void Legend::setTopInset(qreal px)
{
    m_topInset = std::max<qreal>(0.0, px);
}

void Legend::setCorner(Corner c)
{
    if (c == m_corner)
        return;
    m_corner = c;
    // 几何缓存（blockRect 等）在下一次 paint 时按新贴角重算；本类无 update 句柄，
    // 重绘由调用方（Axes::setLegendCorner）触发。
}

void Legend::paint(QPainter &painter, const PaintInput &in)
{
    const QRectF plot = in.plotRect;
    QFont f = in.baseFont;
    // Ctrl+滚轮字号缩放：因子作用于基准字号。
    f.setPointSizeF(std::max(6.0, f.pointSizeF() * m_fontScale));
    const QFontMetrics fm(f);
    const int rowH = fm.height() + 4;
    painter.save();
    painter.setFont(f);
    const QColor labelColor = in.labelColor;
    const QColor gray(0x88, 0x88, 0x88);

    // 标题栏：▾/▸ 折叠指示 + "图例"。点击整栏切换折叠。
    //
    // 位置：贴**右上角**。宽度按内容自适应（标题 / 最长曲线名 / 实时值列）。
    //
    // 多列拆分：曲线多时单列会从顶部拉到画面中部，遮住大片波形
    // （超过 6 行很明显）。行数超过单列上限自动拆多列，块高按列数
    // 近似减半，纵向遮挡变小；拆列后若整块宽超过绘图区 75% 再退回
    // 少一列。行序不变，逐行矩形存入 m_rows，命中检测与读数框
    // 避让（m_blockRect）都不依赖具体布局。
    constexpr int kMaxRowsPerCol = 6;
    const QString titleText = m_collapsed ? QStringLiteral("▸ 图例") : QStringLiteral("▾ 图例");
    const int titleW = fm.horizontalAdvance(titleText) + 20;

    // 每行行宽（色块 + 名字 + 边距；游标激活加实时值列）。
    QVector<int> rowW;
    rowW.reserve(in.items.size());
    for (const Item &item : in.items)
    {
        int w = 16 + fm.horizontalAdvance(item.name) + 12;
        if (in.cursorActive)
            w += 12 + 70;   // 实时值列
        rowW.append(w);
    }
    const int namedRows = in.items.size();

    // 列布局：每列行数均分（末列可少），列宽取列内最大行宽，列间距 16。
    struct ColLayout
    {
        int totalW = 0;       // 整块宽（含标题栏需求）
        int rowsPer = 0;      // 每列行数上限
        QVector<int> w;       // 各列宽
    };
    const auto colLayout = [&](int n) {
        ColLayout lay;
        lay.rowsPer = (namedRows + n - 1) / n;
        lay.w.resize(n);
        for (int r = 0; r < namedRows; ++r)
        {
            const int c = std::min(n - 1, r / lay.rowsPer);
            lay.w[c] = std::max(lay.w.at(c), rowW.at(r));
        }
        lay.totalW = titleW;
        for (int c = 0; c < n; ++c)
            lay.totalW += (c ? 16 : 0) + lay.w.at(c);
        return lay;
    };
    int nCols = qBound(1, (namedRows + kMaxRowsPerCol - 1) / kMaxRowsPerCol, 4);
    auto lay = colLayout(nCols);
    // 多列仍放不下就退回少一列；退到单列按原 45% 上限截断。
    while (nCols > 1 && lay.totalW > int(plot.width() * 0.75))
    {
        --nCols;
        lay = colLayout(nCols);
    }
    const qreal widthCap = (nCols > 1 ? 0.75 : 0.45) * plot.width();
    const int legendW = std::max(96, std::min(lay.totalW, int(widthCap)));
    const qreal blockH = (lay.rowsPer + 1) * rowH;

    // 贴角定位：四角任选（m_corner），8px 边距；m_topInset 只对上角生效。
    qreal legendX = 0;
    qreal legendY = 0;
    switch (m_corner)
    {
    case Corner::TopRight:
        legendX = plot.right() - 8 - legendW;
        legendY = plot.top() + 8 + m_topInset;
        break;
    case Corner::TopLeft:
        legendX = plot.left() + 8;
        legendY = plot.top() + 8 + m_topInset;
        break;
    case Corner::BottomLeft:
        legendX = plot.left() + 8;
        legendY = plot.bottom() - 8 - blockH;
        break;
    case Corner::BottomRight:
        legendX = plot.right() - 8 - legendW;
        legendY = plot.bottom() - 8 - blockH;
        break;
    }

    // 整块底色要先行铺上，否则曲线会从字里穿过去，右上角窄块压在波形上时尤其难看。
    const QRectF blockRect(legendX, legendY, legendW, blockH);
    QColor blockBg = in.windowColor;
    blockBg.setAlpha(232);
    painter.setPen(QPen(QColor(labelColor.red(), labelColor.green(), labelColor.blue(), 60), 1));
    painter.setBrush(blockBg);
    painter.drawRoundedRect(blockRect, 3, 3);

    m_titleRect = QRectF(legendX, legendY, legendW, rowH);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 36));
    painter.drawRoundedRect(m_titleRect, 2, 2);
    painter.setPen(labelColor);
    painter.drawText(QRectF(m_titleRect.left() + 4, m_titleRect.top(), 80, rowH),
                     Qt::AlignVCenter | Qt::AlignLeft, titleText);

    m_rows.clear();
    if (m_collapsed)
    {
        m_blockRect = m_titleRect;
        painter.restore();
        return;
    }

    // 每条命名曲线一行，按列从左到右排：可见用曲线色，隐藏灰显；
    // 隐藏行保留可点击切回。row 是全局命名行序，游标实时值与读数框同行序。
    int row = 0;
    int colX = int(legendX);
    for (int c = 0; c < nCols; ++c)
    {
        const int rEnd = std::min(namedRows, (c + 1) * lay.rowsPer);
        for (int r = c * lay.rowsPer; r < rEnd; ++r, ++row)
        {
            const Item &item = in.items.at(r);
            const QPointF origin(colX, legendY + (r - c * lay.rowsPer + 1) * rowH);
            // 行内裁剪：超宽的名字截在块底色内，不溢出（参照 PJ4 PlotLegend drawLegendData）。
            painter.save();
            painter.setClipRect(QRectF(colX, origin.y(), lay.w.at(c), rowH));
            painter.setPen(Qt::NoPen);
            // 隐藏行：色块降为约 45% 透明度但保留曲线色，颜色辨识不丢（同 PJ4）。
            QColor ink = item.ink;
            if (!item.visible)
                ink.setAlpha(115);
            painter.setBrush(ink);
            painter.drawRect(QRectF(origin.x(), origin.y() + 3, 10, 10));
            painter.setPen(item.visible ? labelColor : gray);
            painter.drawText(QPointF(origin.x() + 16, origin.y() + fm.ascent() + 1), item.name);
            // 图例实时值：游标激活时，图例每条曲线名后显示该时刻数值（与读数框同源、同行序）。
            if (in.cursorActive && row < in.values.size())
            {
                painter.setPen(in.values.at(row).color);
                painter.drawText(QRectF(origin.x() + 16 + qMin(150, fm.horizontalAdvance(item.name) + 12),
                                        origin.y(), 70, rowH),
                                 Qt::AlignVCenter | Qt::AlignLeft, in.values.at(row).value);
            }
            m_rows.append(RowRect{item.curveIndex, QRectF(colX, origin.y(), lay.w.at(c), rowH)});
            painter.restore();
        }
        colX += lay.w.at(c) + 16;
    }
    // 整块矩形：标题栏一行 + 所有曲线行（读数框按它避让）。
    m_blockRect = blockRect;
    painter.restore();
}

int Legend::hitTest(const QPointF &pixel) const
{
    if (!m_visible)
        return -2;
    if (m_titleRect.contains(pixel))
        return -1;
    for (const RowRect &r : m_rows)
        if (r.rect.contains(pixel))
            return r.curveIndex;
    return -2;
}

} // namespace nextpilot::plotscope::fastplot

#include "PlotCanvas.h"
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>


using namespace ng::core;

namespace ng::gui
{


PlotCanvas::PlotCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PlotCanvas::setChannel(DatasetId datasetId, const QString &table, const QString &column,
                            const QString &title)
{
    m_datasetId = datasetId;
    m_table = table;
    m_column = column;
    m_title = title;
}

void PlotCanvas::setLodData(const QVector<LodTriple> &lod, double t0, double t1)
{
    m_lod = lod;
    m_t0 = t0;
    m_t1 = t1;
    update();
}

void PlotCanvas::renderFrom(const Dataset &dataset)
{
    if (m_table.isEmpty() || m_column.isEmpty())
    {
        clear();
        return;
    }
    const Table *table = dataset.table(m_table);
    if (!table)
    {
        clear();
        return;
    }

    // 抽稀在 Rust 内完成：输出恒定为 <= 像素宽 个三元组。
    const int px = qMax(1, width() - 80);
    const auto lod = dataset.lodMinMax(m_table, m_column, table->firstTimeUs, table->lastTimeUs, px);
    if (lod.isEmpty())
    {
        clear();
        return;
    }
    setLodData(lod, table->firstTimeUs / 1000000.0, table->lastTimeUs / 1000000.0);
}

void PlotCanvas::clear()
{
    m_lod.clear();
    m_t0 = m_t1 = 0.0;
    update();
}

void PlotCanvas::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(event->rect(), QColor(0xFA, 0xFA, 0xFA));

    const int left = 56, right = 16, top = 28, bottom = 28;
    const QRect plotArea(left, top, qMax(1, width() - left - right),
                         qMax(1, height() - top - bottom));

    // 标题
    p.setPen(QColor(0x33, 0x33, 0x33));
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(left, 4, width() - left - right, 20), Qt::AlignLeft | Qt::AlignVCenter,
               m_title.isEmpty() ? QStringLiteral("(未选择通道)") : m_title);

    if (m_lod.isEmpty())
    {
        p.setFont(QFont());
        p.setPen(QColor(0x88, 0x88, 0x88));
        p.drawText(plotArea, Qt::AlignCenter, QStringLiteral("无数据"));
        return;
    }

    // 值域（用 LOD 的 min/max 求全局范围）
    double vmin = m_lod.first().min, vmax = m_lod.first().max;
    for (const LodTriple &t : m_lod)
    {
        vmin = qMin(vmin, t.min);
        vmax = qMax(vmax, t.max);
    }
    if (qFuzzyCompare(vmin, vmax))
    {
        vmin -= 0.5;
        vmax += 0.5;
    }

    // 网格 + 坐标轴
    p.setPen(QColor(0xE4, 0xE4, 0xE4));
    for (int i = 0; i <= 4; ++i)
    {
        const int y = plotArea.top() + plotArea.height() * i / 4;
        p.drawLine(plotArea.left(), y, plotArea.right(), y);
    }
    p.setPen(QColor(0xB0, 0xB0, 0xB0));
    p.drawRect(plotArea);

    // 曲线：每个三元组画一条竖线（min..max 包络），这是像素级 LOD 的标准画法。
    // 成本 = O(像素宽)，与数据行数无关。
    const double span = (m_t1 > m_t0) ? (m_t1 - m_t0) : 1.0;
    const double vspan = vmax - vmin;
    p.setPen(QPen(QColor(0x1F, 0x6F, 0xC0), 1.0));

    const int n = m_lod.size();
    for (int i = 0; i < n; ++i)
    {
        const LodTriple &t = m_lod[i];
        const int x = plotArea.left() + (n == 1 ? plotArea.width() / 2
                                                : plotArea.width() * i / (n - 1));
        const int yTop = plotArea.bottom() - int((t.max - vmin) / vspan * plotArea.height());
        const int yBot = plotArea.bottom() - int((t.min - vmin) / vspan * plotArea.height());
        p.drawLine(x, yTop, x, qMax(yTop, yBot));
    }

    // 轴标签
    p.setPen(QColor(0x66, 0x66, 0x66));
    p.setFont(QFont());
    p.drawText(QRect(0, plotArea.top() - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(vmax, 'g', 4));
    p.drawText(QRect(0, plotArea.bottom() - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(vmin, 'g', 4));
    p.drawText(QRect(plotArea.left(), plotArea.bottom() + 4, plotArea.width(), 16),
               Qt::AlignLeft, QString::number(m_t0, 'f', 3) + QStringLiteral(" s"));
    p.drawText(QRect(plotArea.left(), plotArea.bottom() + 4, plotArea.width(), 16),
               Qt::AlignRight, QString::number(m_t1, 'f', 3) + QStringLiteral(" s"));
    p.drawText(QRect(0, plotArea.bottom() + 4, left - 6, 16), Qt::AlignRight,
               QStringLiteral("秒"));
}

} // namespace ng::gui

#include "PlotCanvas.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>


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

int PlotCanvas::plotPixelWidth() const
{
    return qMax(1, width() - kMarginLeft - kMarginRight);
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
    const int px = plotPixelWidth();
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

void PlotCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 视口变了 → LOD 的像素分辨率该跟着变。
    // 否则窄屏取的 LOD 会被拉伸到大屏上（梳齿状假象），
    // 或者大屏取的 LOD 塞进小屏丢失细节。
    emit viewportChanged();
}

void PlotCanvas::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(event->rect(), QColor(0xFA, 0xFA, 0xFA));

    const int left = kMarginLeft, right = kMarginRight;
    const int top = kMarginTop, bottom = kMarginBottom;
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

    // 曲线：min/max 抽稀结果的**包络带**画法。
    //
    // 每个像素桶携带 (x, min, max)：桶内所有样本被压成一条竖段 [min,max]。
    // 只画孤立竖段会得到"梳齿"——相邻桶的竖段端点不重合，视觉上断裂。
    // 所以这里画三样东西合成一条连续包络：
    //   1. 上包络折线：连接各桶的 max（信号的峰线）
    //   2. 下包络折线：连接各桶的 min（信号的谷线）
    //   3. 竖段：连接同一桶的 min 与 max（表示该像素内的真实值域）
    // 二者合起来就是标准的 min/max 带状包络：不丢峰值（max），不藏细节（min）。
    //
    // 成本 = O(像素宽)，与数据行数无关。
    //
    // 关键前提：**桶数必须与画布实际像素宽一致**。桶太少则包络过粗，
    // 桶太多则退化成实心带。这个前提由 viewportChanged -> 重新取数 保证
    // （见 ChannelView）。
    const double span = (m_t1 > m_t0) ? (m_t1 - m_t0) : 1.0;
    const double vspan = vmax - vmin;

    const int n = m_lod.size();
    if (n <= 0)
        return;

    // 先算每个桶的像素坐标与上下端点。
    QVector<int> xs(n);
    QVector<int> yTop(n);
    QVector<int> yBot(n);
    for (int i = 0; i < n; ++i)
    {
        const LodTriple &t = m_lod[i];
        // 横坐标按**时间**定位（不是按序号）：数据可能不等间隔，
        // 按序号画会把时间轴拉歪。
        const double frac = qBound(0.0, (t.x - m_t0) / span, 1.0);
        xs[i] = plotArea.left() + int(frac * plotArea.width());
        const double fTop = (t.max - vmin) / vspan;
        const double fBot = (t.min - vmin) / vspan;
        yTop[i] = plotArea.bottom() - int(fTop * plotArea.height());
        yBot[i] = plotArea.bottom() - int(fBot * plotArea.height());
        if (yTop[i] > yBot[i])
            qSwap(yTop[i], yBot[i]);
    }

    p.setPen(QPen(QColor(0x1F, 0x6F, 0xC0), 1.0));

    // 1) 上下包络折线：让相邻桶的端点连起来，包络成为连续曲线。
    for (int i = 1; i < n; ++i)
    {
        p.drawLine(xs[i - 1], yTop[i - 1], xs[i], yTop[i]); // 上包络（峰线）
        p.drawLine(xs[i - 1], yBot[i - 1], xs[i], yBot[i]); // 下包络（谷线）
    }

    // 2) 桶内竖段：该像素的真实值域。
    for (int i = 0; i < n; ++i)
        p.drawLine(xs[i], yTop[i], xs[i], yBot[i]);

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

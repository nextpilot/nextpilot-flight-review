#include "PlotCanvas.h"

#include <fastplot/PlotWidget.h>

#include <QResizeEvent>
#include <QVBoxLayout>

using namespace ng::core;

namespace ng::gui
{

namespace
{
// 浅色配色：当前 ng 还没有主题层，先把颜色收在这一处，
// 将来 Theme 只改这几个常量，不必逐个视图改。
const QColor kBackground(0xFA, 0xFA, 0xFA);
const QColor kGridInk(0xE4, 0xE4, 0xE4);
const QColor kAxisInk(0xB0, 0xB0, 0xB0);
const QColor kLabelInk(0x66, 0x66, 0x66);
const QColor kCurveInk(0x1F, 0x6F, 0xC0);
const QColor kTitleInk(0x33, 0x33, 0x33);

const fastplot::AxisSide kSides[4] = {fastplot::AxisSide::Left, fastplot::AxisSide::Right,
                                      fastplot::AxisSide::Bottom, fastplot::AxisSide::Top};
} // namespace

PlotCanvas::PlotCanvas(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 绘制全部委托给 fastplot：本类不再自己算坐标、画网格、画包络。
    m_plot = new fastplot::PlotWidget(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_plot);

    m_plot->setBackground(kBackground);
    m_plot->setTitleColor(kTitleInk);
    for (fastplot::AxisSide side : kSides)
    {
        auto &axis = m_plot->axis(side);
        axis.setInk(kAxisInk);
        axis.setLabelColor(kLabelInk);
        axis.setGridInk(kGridInk);
    }
}

PlotCanvas::~PlotCanvas() = default;

void PlotCanvas::setChannel(DatasetId datasetId, const QString &table, const QString &column,
                            const QString &title)
{
    m_datasetId = datasetId;
    m_table = table;
    m_column = column;
    m_title = title;
    m_plot->setTitle(title);
}

void PlotCanvas::setLodData(const QVector<LodTriple> &lod, double t0, double t1)
{
    m_lod = lod;
    m_t0 = t0;
    m_t1 = t1;
    pushToPlot();
}

void PlotCanvas::pushToPlot()
{
    if (m_lod.isEmpty())
    {
        m_plot->clearCurves();
        return;
    }

    // 只转三元组（<= 像素宽），不复制原始数据——零拷贝的边界就在这里。
    QVector<fastplot::LodPoint> pts;
    pts.reserve(m_lod.size());
    for (const LodTriple &t : m_lod)
        pts.push_back(fastplot::LodPoint{t.x, t.min, t.max});

    m_plot->clearCurves();
    m_plot->addCurve(fastplot::Curve::fromLod(pts, kCurveInk, 1.0));

    // 横轴按真实时间定界（不是按序号）：数据可能不等间隔。
    if (m_t1 > m_t0)
        m_plot->setXRange(m_t0, m_t1);
    // 纵轴按 LOD 的 min/max 自动留 5% 余量。
    m_plot->autoScaleY();
}

int PlotCanvas::plotPixelWidth() const
{
    if (!m_plot)
        return 1;
    return qMax(1, int(m_plot->plotRect().width()));
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
    m_plot->clearCurves();
    m_plot->update();
}

void PlotCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 视口变了 → LOD 的像素分辨率该跟着变。
    // 否则窄屏取的 LOD 会被拉伸到大屏上（梳齿状假象），
    // 或者大屏取的 LOD 塞进小屏丢失细节。
    emit viewportChanged();
}

} // namespace ng::gui

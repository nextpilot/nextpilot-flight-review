#include "PlotCanvas.h"

#include <fastplot/Panner.h>
#include <fastplot/Picker.h>
#include <fastplot/PlotWidget.h>
#include <fastplot/Zoomer.h>

#include <QResizeEvent>
#include <QVBoxLayout>

using namespace ng::core;

namespace ng::gui
{

namespace
{
// 曲线色来自主题 token（数据语义色），不硬编码——深色主题下自动换成适配色。
// 轴/网格/标题/背景色走 palette（见 applyPalette），同样随主题。
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

    // 交互部件：滚轮/框选缩放、左键平移、悬停读数。
    // 三者的状态机都在库里，本类只做接线——画布自身仍然只负责画。
    m_zoomer = new fastplot::Zoomer(m_plot, this);
    m_panner = new fastplot::Panner(m_plot, this);
    m_picker = new fastplot::Picker(m_plot, this);

    connect(m_zoomer, &fastplot::Zoomer::zoomed, this, [this] {
        double t0 = 0.0;
        double t1 = 0.0;
        if (visibleTimeRange(&t0, &t1))
            Q_EMIT rangeChanged(t0, t1);
    });
    connect(m_panner, &fastplot::Panner::panned, this, [this] {
        double t0 = 0.0;
        double t1 = 0.0;
        if (visibleTimeRange(&t0, &t1))
            Q_EMIT rangeChanged(t0, t1);
    });
    connect(m_picker, &fastplot::Picker::hovered, this,
            [this](double x, double y, bool valid) {
                if (!valid)
                {
                    m_plot->setOverlayText(QString());
                    return;
                }
                // 单位属于视图层的知识，库只给数值。
                m_plot->setOverlayText(QStringLiteral("t = %1 s    v = %2")
                                          .arg(x, 0, 'f', 3)
                                          .arg(y, 0, 'f', 4));
            });

    applyPalette(); // 颜色全部来自当前主题 palette，不留硬编码浅色
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
    const QColor curveInk = palette().color(QPalette::Highlight); // 主题 accent 色
    auto &curve = m_plot->addCurve(fastplot::Curve::fromLod(pts, curveInk, 1.0));
    // 图例名字用通道名（表格.列），不用整条标题——标题已经在画布左上角了。
    curve.setName(m_table.isEmpty() ? m_column
                                    : QStringLiteral("%1.%2").arg(m_table, m_column));
    m_plot->setLegendVisible(true);

    // 横轴按真实时间定界（不是按序号）：数据可能不等间隔。
    if (m_t1 > m_t0)
        m_plot->setXRange(m_t0, m_t1);
    // 纵轴按 LOD 的 min/max 自动留 5% 余量。
    m_plot->autoScaleY();
}

bool PlotCanvas::exportPng(const QString &path)
{
    return m_plot && m_plot->exportPng(path);
}

bool PlotCanvas::exportCsv(const QString &path)
{
    return m_plot && m_plot->exportCsv(path);
}

QJsonObject PlotCanvas::saveViewState() const
{
    return m_plot ? m_plot->saveState() : QJsonObject();
}

bool PlotCanvas::restoreViewState(const QJsonObject &state)
{
    return m_plot && m_plot->restoreState(state);
}

void PlotCanvas::setLegendVisible(bool visible)
{
    if (m_plot)
        m_plot->setLegendVisible(visible);
}

bool PlotCanvas::legendVisible() const
{
    return m_plot && m_plot->legendVisible();
}

bool PlotCanvas::visibleTimeRange(double *t0, double *t1) const
{
    if (!m_plot || m_lod.isEmpty())
        return false;
    const auto &vp = m_plot->viewport();
    // 只认时间轴（x）；数据为空时 viewport 仍是默认 0..1，不能拿来当时间窗。
    if (t0)
        *t0 = vp.xLower();
    if (t1)
        *t1 = vp.xUpper();
    return true;
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

void PlotCanvas::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // 主题切换时 ThemeManager 重设全局 palette，PaletteChange 传播到每个 widget。
    // fastplot 库内色值在构造时定死，这里按新 palette 重设一遍，画布随之换肤。
    if (event->type() == QEvent::PaletteChange)
        applyPalette();
}

void PlotCanvas::applyPalette()
{
    if (!m_plot)
        return;
    const QPalette &pal = palette();
    m_plot->setBackground(pal.color(QPalette::Base));
    m_plot->setTitleColor(pal.color(QPalette::WindowText));
    for (fastplot::AxisSide side : kSides)
    {
        auto &axis = m_plot->axis(side);
        axis.setInk(pal.color(QPalette::Mid));
        axis.setLabelColor(pal.color(QPalette::WindowText));
        axis.setGridInk(pal.color(QPalette::Light));
    }
    // 曲线色在 pushToPlot 时按当时主题 palette 取（见 pushToPlot），
    // 主题切换后的下一次重取数会用新色；这里不重建曲线，避免丢失 LOD。
    m_plot->replot();
}

} // namespace ng::gui

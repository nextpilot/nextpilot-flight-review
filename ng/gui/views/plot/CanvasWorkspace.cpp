#include "CanvasWorkspace.h"

#include "AppContext.h"
#include "ChannelView.h"
#include "IIntentExecutor.h"
#include "InstrumentView.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "Result.h"

#include <QEvent>
#include <QGridLayout>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStringList>

#include <algorithm>

using namespace nextpilot::plotscope::core;

namespace nextpilot::plotscope::gui
{

namespace
{
// 曲线类图型：在 PlotCanvas 内切（都是"曲线"模型，走 LOD 折线）。
bool isCurveKind(const QString &kind)
{
    return kind == QStringLiteral("timeseries") || kind == QStringLiteral("scatter")
           || kind == QStringLiteral("xy");
}

// 专用图型：fastplot 的 Instrument widget（仪表/地图/3D/柱/直方/频谱）。
bool isInstrumentKind(const QString &kind)
{
    static const QStringList k = {
        QStringLiteral("gauge"),      QStringLiteral("compass"),
        QStringLiteral("horizon"),    QStringLiteral("traj3d"),
        QStringLiteral("map"),        QStringLiteral("waterfall"),
        QStringLiteral("bar"),        QStringLiteral("histogram"),
        QStringLiteral("fft"),
    };
    return k.contains(kind);
}

// gallery 的 map/horizon 映射到 InstrumentView 的真实 kind。
QString toInstrumentKind(const QString &kind)
{
    if (kind == QStringLiteral("map"))
        return QStringLiteral("track");
    if (kind == QStringLiteral("horizon"))
        return QStringLiteral("attitude");
    return kind;
}
} // namespace

CanvasWorkspace::CanvasWorkspace(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
    : QWidget(parent), m_appCtx(appCtx), m_executor(executor)
{
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(2, 2, 2, 2);
    m_grid->setSpacing(3);

    // 默认一个画布 cell。
    addCell();
    m_rows = 1;
    m_cols = 1;

    // 活动通道只追加到当前 cell（"当前画布"语义）。
    QObject::connect(&m_appCtx, &AppContext::activeChannelChanged, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         onActiveChannelChanged(datasetId, t, c);
                     });

    registerViewActions();
}

CanvasWorkspace::~CanvasWorkspace() = default;

int CanvasWorkspace::cellCount() const
{
    return m_cells.size();
}

int CanvasWorkspace::activeIndex() const
{
    return m_active;
}

PlotCanvas *CanvasWorkspace::activeCanvas() const
{
    return m_cells.isEmpty() ? nullptr : m_cells.at(qBound(0, m_active, m_cells.size() - 1)).canvas;
}

ChannelView *CanvasWorkspace::activeChannelView() const
{
    return m_cells.isEmpty() ? nullptr : m_cells.at(qBound(0, m_active, m_cells.size() - 1)).view;
}

int CanvasWorkspace::addCell()
{
    auto *stack = new QStackedWidget(this);
    auto *canvas = new PlotCanvas(stack);
    auto *view = new ChannelView(m_executor, canvas, canvas);

    auto *instrument = new InstrumentView(m_executor, stack);
    instrument->setAppContext(&m_appCtx);

    stack->addWidget(canvas);
    stack->addWidget(instrument);
    stack->setCurrentWidget(canvas);
    // 点击任意 cell 设为活动（子控件安装事件过滤器）。
    stack->installEventFilter(this);
    // 列节点拖到画布 → 画布只解码 MIME 发信号 → 这里切活动通道
    // （与数据集树双击同一路径，activity 路由回当前 cell）。
    QObject::connect(canvas, &PlotCanvas::channelDropped, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         m_appCtx.setActiveChannel(datasetId, t, c);
                     });

    Cell cell;
    cell.canvas = canvas;
    cell.view = view;
    cell.instrument = instrument; // 见头文件：需加字段
    cell.stack = stack;
    m_cells.append(cell);

    m_active = m_cells.size() - 1;
    relayout();
    return m_active;
}

bool CanvasWorkspace::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        for (int i = 0; i < m_cells.size(); ++i)
        {
            if (watched == m_cells.at(i).stack)
            {
                setActive(i);
                break;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CanvasWorkspace::setActive(int index)
{
    if (index < 0 || index >= m_cells.size() || m_active == index)
        return;
    m_active = index;
}

void CanvasWorkspace::relayout()
{
    // 先把所有 cell 从布局摘下，再按当前 rows×cols 重新铺。
    for (const Cell &c : m_cells)
        m_grid->removeWidget(c.stack);

    const int n = m_cells.size();
    for (int i = 0; i < n; ++i)
    {
        const int r = i / std::max(1, m_cols);
        const int c = i % std::max(1, m_cols);
        m_grid->addWidget(m_cells.at(i).stack, r, c);
    }
    // 行列 stretch 均分，保证拆分后各画布等宽/等高。
    for (int i = 0; i < m_grid->rowCount(); ++i)
        m_grid->setRowStretch(i, 1);
    for (int i = 0; i < m_grid->columnCount(); ++i)
        m_grid->setColumnStretch(i, 1);
}

bool CanvasWorkspace::removeAt(int index)
{
    if (index < 0 || index >= m_cells.size() || m_cells.size() <= 1)
        return false;
    Cell cell = m_cells.takeAt(index);
    m_grid->removeWidget(cell.stack);
    cell.stack->deleteLater();
    if (m_active >= m_cells.size())
        m_active = m_cells.size() - 1;
    // 删一个后重排为单行（列数 = 数量），保持整齐。
    m_rows = 1;
    m_cols = m_cells.size();
    relayout();
    return true;
}

bool CanvasWorkspace::split(bool horizontal)
{
    if (m_cells.isEmpty())
        return false;
    // 在当前活动 cell 之后插入一个新画布；水平拆分=左右（列+1），垂直=上下（行+1）。
    auto *stack = new QStackedWidget(this);
    auto *canvas = new PlotCanvas(stack);
    auto *view = new ChannelView(m_executor, canvas, canvas);
    auto *instrument = new InstrumentView(m_executor, stack);
    instrument->setAppContext(&m_appCtx);
    stack->addWidget(canvas);
    stack->addWidget(instrument);
    stack->setCurrentWidget(canvas);
    stack->installEventFilter(this);
    QObject::connect(canvas, &PlotCanvas::channelDropped, this,
                     [this](int datasetId, const QString &t, const QString &c) {
                         m_appCtx.setActiveChannel(datasetId, t, c);
                     });

    Cell cell;
    cell.canvas = canvas;
    cell.view = view;
    cell.instrument = instrument;
    cell.stack = stack;

    const int insertAt = qBound(0, m_active + 1, m_cells.size());
    m_cells.insert(insertAt, cell);
    // 拆分只加一列/一行；网格规格跟着更新。
    if (horizontal)
        m_cols = m_cells.size();
    else
    {
        m_cols = 1;
        m_rows = m_cells.size();
    }
    m_active = insertAt;
    relayout();
    return true;
}

bool CanvasWorkspace::setGrid(int rows, int cols)
{
    if (rows < 1 || cols < 1)
        return false;
    const int want = rows * cols;
    while (m_cells.size() < want)
        addCell();
    while (m_cells.size() > want && m_cells.size() > 1)
        removeAt(m_cells.size() - 1);
    m_rows = rows;
    m_cols = cols;
    m_active = qBound(0, m_active, m_cells.size() - 1);
    relayout();
    return true;
}

bool CanvasWorkspace::setCellType(int index, const QString &kind)
{
    if (index < 0 || index >= m_cells.size())
        return false;
    Cell &cell = m_cells[index];
    if (isCurveKind(kind))
    {
        cell.stack->setCurrentWidget(cell.canvas);
        cell.canvas->setPlotKind(kind);
        return true;
    }
    if (isInstrumentKind(kind))
    {
        cell.instrument->switchKind(toInstrumentKind(kind));
        cell.instrument->renderFromActive(toInstrumentKind(kind));
        cell.stack->setCurrentWidget(cell.instrument);
        return true;
    }
    return false;
}

void CanvasWorkspace::onActiveChannelChanged(int datasetId, const QString &t, const QString &c)
{
    if (m_cells.isEmpty())
        return;
    Cell &cell = m_cells[qBound(0, m_active, m_cells.size() - 1)];
    // 若活动 cell 当前显示的是仪表，把数据也喂给它；曲线画布则走 addChannel 叠加。
    if (cell.stack->currentWidget() == cell.instrument && !cell.instrument->currentKind().isEmpty())
        cell.instrument->renderFromActive(cell.instrument->currentKind());
    else
        cell.view->addChannel(datasetId, t, c, nullptr);
}

void CanvasWorkspace::registerViewActions()
{
    // 所有 plot.* 只在这里注册一次，路由到活动 cell —— 多画布不会被后建者劫持。
    m_appCtx.registerViewAction(QStringLiteral("plot.export.png"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(QStringLiteral("epath"), QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportPng(path))
                                        return fail(QStringLiteral("eio"), QStringLiteral("PNG 导出失败"));
                                    return QVariant(path);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.export.csv"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(QStringLiteral("epath"), QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportCsv(path))
                                        return fail(QStringLiteral("eio"), QStringLiteral("CSV 导出失败"));
                                    return QVariant(path);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.state.save"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return activeCanvas() ? QVariant(activeCanvas()->saveViewState())
                                                          : QVariant(QVariantMap());
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.state.restore"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const auto state = params.value(QStringLiteral("state")).toJsonObject();
                                    if (!activeCanvas() || !activeCanvas()->restoreViewState(state))
                                        return fail(QStringLiteral("estate"), QStringLiteral("视图状态复原失败"));
                                    return QVariant(true);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.legend.toggle"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!activeCanvas())
                                        return QVariant(false);
                                    activeCanvas()->setLegendVisible(!activeCanvas()->legendVisible());
                                    return QVariant(activeCanvas()->legendVisible());
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.series.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (activeChannelView())
                                        activeChannelView()->clearChannels();
                                    return QVariant(true);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.series.list"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantList list;
                                    if (PlotCanvas *c = activeCanvas())
                                        for (const PlotSeries &s : c->series())
                                            list.append(QVariantMap{
                                                {QStringLiteral("key"), s.key()},
                                                {QStringLiteral("dataset"), s.datasetId},
                                                {QStringLiteral("table"), s.table},
                                                {QStringLiteral("column"), s.column},
                                                {QStringLiteral("points"), s.lod.size()},
                                                {QStringLiteral("rightAxis"), s.rightAxis},
                                            });
                                    return QVariant(list);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.series.axis"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const bool right = params.value(QStringLiteral("right")).toBool();
                                    if (key.isEmpty())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesAxis(key, right))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    // 图型切换：workspace 直接处理（曲线在画布切、专用图型在 cell 内切页），
    // 不再需要外壳 requestPanel 替换整个中央——多画布形态得以保留。
    m_appCtx.registerViewAction(QStringLiteral("plot.setType"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString kind = params.value(QStringLiteral("type")).toString();
                                    const bool handled = setCellType(m_active, kind);
                                    QVariantMap r;
                                    r.insert(QStringLiteral("handled"), handled);
                                    r.insert(QStringLiteral("kind"), kind);
                                    return QVariant(r);
                                });

    // —— page.*：页面/画布布局操作（抄老 page 面板体验）——
    m_appCtx.registerViewAction(QStringLiteral("page.addCanvas"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(addCell());
                                });
    m_appCtx.registerViewAction(QStringLiteral("page.removeCanvas"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(removeAt(m_active));
                                });
    m_appCtx.registerViewAction(QStringLiteral("page.splitHorizontal"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(split(true));
                                });
    m_appCtx.registerViewAction(QStringLiteral("page.splitVertical"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(split(false));
                                });
    m_appCtx.registerViewAction(QStringLiteral("page.grid"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int r = params.value(QStringLiteral("rows")).toInt();
                                    const int c = params.value(QStringLiteral("cols")).toInt();
                                    if (!setGrid(r, c))
                                        return fail(QStringLiteral("egrid"), QStringLiteral("网格规格非法"));
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("rows"), m_rows},
                                                    {QStringLiteral("cols"), m_cols}};
                                    return QVariant(out);
                                });
    m_appCtx.registerViewAction(QStringLiteral("page.state"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("active"), m_active},
                                                    {QStringLiteral("rows"), m_rows},
                                                    {QStringLiteral("cols"), m_cols}};
                                    return QVariant(out);
                                });
}

QWidget *createCanvasWorkspace(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
{
    return new CanvasWorkspace(appCtx, executor, parent);
}

// 视图层自注册：中央工作区，替代单画布承载多画布 grid。
NG_REGISTER_PANEL("canvas-workspace", canvasWorkspace, createCanvasWorkspace)

} // namespace nextpilot::plotscope::gui


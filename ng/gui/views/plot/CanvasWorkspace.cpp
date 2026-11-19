#include "CanvasWorkspace.h"

#include "AppContext.h"
#include "ChannelView.h"
#include "IIntentExecutor.h"
#include "InstrumentView.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "PropertySchema.h"
#include "Result.h"
#include "Transform.h"
#include "TransformEditorDialog.h"
#include <fastplot/Axes.h>

#include <QEvent>
#include <QGridLayout>
#include <QJsonDocument>
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
    registerPropertyProviders();
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
    // 画布选中请求 -> 统一 selection（属性面板据此刷新）。
    connectCanvasSignals(canvas);

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
    // 画布选中请求 -> 统一 selection（属性面板据此刷新）。
    connectCanvasSignals(canvas);

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

void CanvasWorkspace::connectCanvasSignals(PlotCanvas *canvas)
{
    if (!canvas)
        return;
    QObject::connect(canvas, &PlotCanvas::seriesSelectionRequested, this, [this, canvas](const QString &key) {
        const PlotSeries *s = canvas->seriesByKey(key);
        if (!s)
            return;
        Selection sel;
        sel.kind = SelectionKind::Series;
        sel.label = s->legendName();
        sel.ids.insert(QStringLiteral("seriesKey"), key);
        sel.ids.insert(QStringLiteral("dataset"), s->datasetId);
        sel.ids.insert(QStringLiteral("table"), s->table);
        sel.ids.insert(QStringLiteral("column"), s->column);
        m_appCtx.setSelection(sel);
    });
    QObject::connect(canvas, &PlotCanvas::axisSelectionRequested, this, [this](const QString &axis) {
        Selection sel;
        sel.kind = SelectionKind::Axis;
        sel.label = axis == QStringLiteral("x") ? QStringLiteral("X 轴")
                  : axis == QStringLiteral("y-left") ? QStringLiteral("左 Y 轴")
                                                      : QStringLiteral("右 Y 轴");
        sel.ids.insert(QStringLiteral("axis"), axis);
        m_appCtx.setSelection(sel);
    });
    QObject::connect(canvas, &PlotCanvas::canvasSelectionRequested, this, [this] {
        Selection sel;
        sel.kind = SelectionKind::Canvas;
        sel.label = QStringLiteral("画布");
        sel.ids.insert(QStringLiteral("canvas"), QStringLiteral("active"));
        m_appCtx.setSelection(sel);
    });
    // 曲线 Y 变换：打开统一编辑器（按 CapAxisY 过滤 + 在线调试），回写配方。
    QObject::connect(canvas, &PlotCanvas::seriesTransformRequested, this, [this, canvas](const QString &key) {
        const PlotSeries *s = canvas->seriesByKey(key);
        QJsonObject initial;
        if (s && !s->yTransformId.isEmpty())
        {
            TransformRequest req;
            req.descriptorId = s->yTransformId;
            req.kind = TransformRegistry::get(s->yTransformId).kind;
            req.params = QJsonDocument::fromJson(s->yTransformParamsJson.toUtf8()).object();
            initial = req.toJson();
        }
        const QJsonObject req = TransformEditorDialog::run(
            this, m_appCtx, CapAxisY,
            s ? QStringLiteral("曲线 Y 变换 · %1").arg(s->legendName()) : QStringLiteral("曲线 Y 变换"),
            initial);
        if (req.isEmpty())
            return;
        canvas->setSeriesYTransform(key,
            req.value(QStringLiteral("descriptorId")).toString(),
            QString::fromUtf8(QJsonDocument(req.value(QStringLiteral("params")).toObject()).toJson(QJsonDocument::Compact)));
    });
}

// 曲线/轴/画布的属性 provider + setter。全部纯数据操作，不含业务类型分发。
void CanvasWorkspace::registerPropertyProviders()
{
    auto row = [](const QString &key, const QString &label, const QString &value,
                  PropKind kind = PropKind::ReadOnly, bool editable = false) {
        PropRow r;
        r.key = key;
        r.label = label;
        r.value = value;
        r.kind = kind;
        r.editable = editable;
        return r;
    };

    // --- 曲线 ---
    m_appCtx.registerPropertyProvider(SelectionKind::Series, [this, row](const Selection &sel) {
        const QString key = sel.ids.value(QStringLiteral("seriesKey")).toString();
        PlotCanvas *canvas = activeCanvas();
        const PlotSeries *s = canvas ? canvas->seriesByKey(key) : nullptr;
        PropertySchema schema;
        schema.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("series");
        o.label = QStringLiteral("曲线");
        o.rows << row(QStringLiteral("title"), QStringLiteral("标题"), s ? s->title : sel.label);
        o.rows << row(QStringLiteral("channel"), QStringLiteral("通道"),
                      s ? s->legendName() : sel.label);
        if (s)
        {
            o.rows << row(QStringLiteral("visible"), QStringLiteral("显示"),
                          s->visible ? QStringLiteral("true") : QStringLiteral("false"),
                          PropKind::Bool, true);
            o.rows << row(QStringLiteral("color"), QStringLiteral("颜色"), s->color.name(),
                          PropKind::Color, true);
            o.rows << row(QStringLiteral("width"), QStringLiteral("线宽"),
                          QString::number(s->lineWidth, 'f', 2), PropKind::Number, true);
            o.rows << row(QStringLiteral("axis"), QStringLiteral("坐标轴"),
                          s->rightAxis ? QStringLiteral("右 Y 轴") : QStringLiteral("左 Y 轴"),
                          PropKind::Enum, true);
            o.rows[o.rows.size() - 1].options = {QStringLiteral("左 Y 轴"), QStringLiteral("右 Y 轴")};
        }
        schema.objects << o;

        // 变换组：X 轴时间 offset + Y 轴变换（算子/表达式/Lua），按能力过滤，防止选错。
        PropObject tr;
        tr.id = QStringLiteral("transform");
        tr.label = QStringLiteral("变换");
        tr.rows << row(QStringLiteral("xTransform"), QStringLiteral("X 轴时间偏移(s)"),
                       s ? QString::number(s->xTimeOffset, 'f', 3) : QStringLiteral("0.000"),
                       PropKind::Number, true);
        const QString yDesc = s && !s->yTransformId.isEmpty()
            ? TransformRegistry::get(s->yTransformId).label : QStringLiteral("无");
        tr.rows << row(QStringLiteral("yTransform"), QStringLiteral("Y 轴变换"), yDesc,
                       PropKind::Text, true);
        tr.rows.last().tooltip = QStringLiteral("可选算子/表达式/Lua；在右键菜单或脚本里用 "
                                                "plot.series.transform 设置（能力：轴Y）。可选清单见 "
                                                "TransformRegistry::idsByCapability(CapAxisY)");
        schema.objects << tr;
        return schema.toJson();
    });

    // --- 轴 ---
    m_appCtx.registerPropertyProvider(SelectionKind::Axis, [this, row](const Selection &sel) {
        const QString axis = sel.ids.value(QStringLiteral("axis")).toString();
        PlotCanvas *canvas = activeCanvas();
        PropertySchema schema;
        schema.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("axis");
        o.label = sel.label;
        o.rows << row(QStringLiteral("id"), QStringLiteral("轴"), axis);
        if (canvas && canvas->plotWidget())
        {
            o.rows << row(QStringLiteral("grid"), QStringLiteral("网格"),
                          canvas->plotWidget()->gridVisible() ? QStringLiteral("true") : QStringLiteral("false"),
                          PropKind::Bool, true);
        }
        const QString tfId = canvas ? canvas->axisTransform(axis) : QString();
        const QString tfLabel = tfId.isEmpty()
            ? QStringLiteral("无（填 Transform id 或算子/表达式/Lua）")
            : TransformRegistry::get(tfId).label;
        if (axis == QStringLiteral("x"))
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("X 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("X 轴变换 id。可选：TransformRegistry::idsByCapability(CapAxisX)，"
                                                    "如 builtin.scale 的 time_offset_sec / expression");
        }
        else
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("Y 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("Y 轴变换 id。可选：TransformRegistry::idsByCapability(CapAxisY)");
        }
        schema.objects << o;
        return schema.toJson();
    });

    // --- 画布 ---
    m_appCtx.registerPropertyProvider(SelectionKind::Canvas, [this, row](const Selection &sel) {
        PlotCanvas *canvas = activeCanvas();
        PropertySchema schema;
        schema.title = QStringLiteral("画布属性");
        PropObject o;
        o.id = QStringLiteral("canvas");
        o.label = QStringLiteral("画布");
        o.rows << row(QStringLiteral("kind"), QStringLiteral("图型"),
                      canvas ? canvas->plotKind() : QString());
        o.rows << row(QStringLiteral("series"), QStringLiteral("曲线数"),
                      QString::number(canvas ? canvas->seriesCount() : 0));
        o.rows << row(QStringLiteral("legend"), QStringLiteral("图例"),
                      canvas && canvas->legendVisible() ? QStringLiteral("true") : QStringLiteral("false"),
                      PropKind::Bool, true);
        schema.objects << o;
        return schema.toJson();
    });

    // --- setter：把属性写回活动画布 ---
    m_appCtx.registerPropertySetter(SelectionKind::Series,
        [this](const Selection &sel, const QString &, const QString &key, const QString &value, QString *err) {
            PlotCanvas *canvas = activeCanvas();
            if (!canvas)
            {
                if (err) *err = QStringLiteral("没有活动画布");
                return false;
            }
            const QString k = sel.ids.value(QStringLiteral("seriesKey")).toString();
            if (key == QStringLiteral("visible"))
                return canvas->setSeriesVisible(k, value == QStringLiteral("true"));
            if (key == QStringLiteral("color"))
                return canvas->setSeriesInk(k, QColor(value));
            if (key == QStringLiteral("width"))
                return canvas->setSeriesWidth(k, value.toDouble());
            if (key == QStringLiteral("axis"))
                return canvas->setSeriesAxis(k, value == QStringLiteral("右 Y 轴"));
            if (key == QStringLiteral("xTransform"))
                return canvas->setSeriesXOffset(k, value.toDouble());
            if (key == QStringLiteral("yTransform"))
                return canvas->setSeriesYTransform(k, value);
            if (err) *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });

    m_appCtx.registerPropertySetter(SelectionKind::Axis,
        [this](const Selection &sel, const QString &, const QString &key, const QString &value, QString *err) {
            PlotCanvas *canvas = activeCanvas();
            if (!canvas || !canvas->plotWidget())
            {
                if (err) *err = QStringLiteral("没有活动画布");
                return false;
            }
            if (key == QStringLiteral("grid"))
            {
                canvas->plotWidget()->grid(value == QStringLiteral("true"));
                return true;
            }
            if (key == QStringLiteral("transform"))
            {
                // 轴变换通过 TransformRegistry 的 id 生效；实际执行在曲线内（Task #123）。
                if (!value.isEmpty() && !TransformRegistry::contains(value))
                {
                    if (err) *err = QStringLiteral("未知变换 id: %1").arg(value);
                    return false;
                }
                canvas->setAxisTransform(sel.ids.value(QStringLiteral("axis")).toString(), value);
                return true;
            }
            if (err) *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });

    m_appCtx.registerPropertySetter(SelectionKind::Canvas,
        [this](const Selection &, const QString &, const QString &key, const QString &value, QString *err) {
            PlotCanvas *canvas = activeCanvas();
            if (!canvas)
            {
                if (err) *err = QStringLiteral("没有活动画布");
                return false;
            }
            if (key == QStringLiteral("legend"))
            {
                canvas->setLegendVisible(value == QStringLiteral("true"));
                return true;
            }
            if (err) *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });
}

void CanvasWorkspace::registerViewActions()
{
    // 所有 plot.* 只在这里注册一次，路由到 active cell —— 多画布不会被后建者劫持。
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

    // —— plotting 全套操作意图（抄老 PlottingRibbon / PlottingCanvas 的体验）——
    // 全部落到 active cell 的 PlotCanvas 真实能力，不造空按钮。
    m_appCtx.registerViewAction(QStringLiteral("plot.series.visible"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const bool vis = params.value(QStringLiteral("visible"), true).toBool();
                                    if (key.isEmpty())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesVisible(key, vis))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.series.color"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const QColor color = params.value(QStringLiteral("color")).value<QColor>();
                                    if (key.isEmpty() || !color.isValid())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key 或颜色"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesInk(key, color))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.series.remove"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    if (key.isEmpty())
                                        return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->removeSeries(key))
                                        return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.zoom"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const double factor = params.value(QStringLiteral("factor"), 0.5).toDouble();
                                    if (!activeCanvas())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    activeCanvas()->zoomRange(factor <= 0.0 ? 0.5 : factor);
                                    return QVariant(true);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.autoscale"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!activeCanvas())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    activeCanvas()->autoScale();
                                    return QVariant(true);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.grid"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    const bool on = params.contains(QStringLiteral("on"))
                                                        ? params.value(QStringLiteral("on")).toBool()
                                                        : !c->plotWidget()->gridVisible();
                                    c->plotWidget()->grid(on);
                                    return QVariant(on);
                                });
    m_appCtx.registerViewAction(QStringLiteral("plot.axis.range"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    const QString axis = params.value(QStringLiteral("axis"), QStringLiteral("x")).toString();
                                    const double lo = params.value(QStringLiteral("lo")).toDouble();
                                    const double hi = params.value(QStringLiteral("hi")).toDouble();
                                    if (hi <= lo)
                                        return fail(QStringLiteral("erange"), QStringLiteral("范围非法"));
                                    if (axis == QStringLiteral("y2"))
                                        c->plotWidget()->setYRange2(lo, hi);
                                    else if (axis == QStringLiteral("y"))
                                        c->plotWidget()->setYRange(lo, hi);
                                    else
                                        c->plotWidget()->setXRange(lo, hi);
                                    c->plotWidget()->update();
                                    return QVariant(true);
                                });
    // 文本读数覆盖层：时间标记 / A-B 测量 / 播放头共用，诚实呈现为读数条
    // （老代码那套交互式拖拽游标是 P2，这里不伪装成已实现）。
    m_appCtx.registerViewAction(QStringLiteral("plot.overlay"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(QStringLiteral("enoent"), QStringLiteral("无活动画布"));
                                    const QString text = params.value(QStringLiteral("text")).toString();
                                    if (text.isEmpty())
                                        c->plotWidget()->clearOverlay();
                                    else
                                        c->plotWidget()->setOverlayText(text);
                                    return QVariant(true);
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
    // 老 PlottingRibbon"页面"组的"新建页"：ng 用多画布工作区承载页概念，
    // 新建页 = 重置为单个空画布（真实能力，非占位）。
    m_appCtx.registerViewAction(QStringLiteral("page.new"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!setGrid(1, 1))
                                        return fail(QStringLiteral("epage"), QStringLiteral("新建页失败"));
                                    return QVariant(true);
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


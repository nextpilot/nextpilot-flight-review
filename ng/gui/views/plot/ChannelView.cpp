#include "ChannelView.h"

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "PanelFactoryRegistry.h"
#include "Result.h"

#include <QJsonObject>
#include <QTimer>

namespace nextpilot::plotscope::gui
{

using namespace nextpilot::plotscope::core;

ChannelView::ChannelView(IIntentExecutor &executor, PlotCanvas *canvas, QObject *parent)
    : QObject(parent), m_executor(executor), m_canvas(canvas)
{
    if (!m_canvas)
        return;

    // 合并定时器：视口连续变化时只重取最后一次。
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(120);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });

    // 缩放/平移改变可见时间窗 → 按新窗口重取全部曲线（带合并定时器）。
    connect(m_canvas, &PlotCanvas::rangeChanged, this, [this](double t0, double t1) {
        m_windowSet = true;
        m_winT0 = t0;
        m_winT1 = t1;
        m_refreshTimer.start();
    });

    // 视口尺寸变化 → 按新像素宽重新取 LOD（分辨率正确性）。
    connect(m_canvas, &PlotCanvas::viewportChanged, this, [this] {
        m_refreshTimer.start();
    });
}

QString ChannelView::makeKey(DatasetId datasetId, const QString &table, const QString &column)
{
    return QStringLiteral("%1|%2|%3").arg(datasetId).arg(table, column);
}

int ChannelView::channelCount() const
{
    return m_channels.size();
}

bool ChannelView::addChannel(DatasetId datasetId, const QString &table, const QString &column,
                             QString *err)
{
    if (!m_canvas)
        return false;
    const QString key = makeKey(datasetId, table, column);

    // 去重：同通道已存在则只刷新它，不重复叠加。
    for (PlotSeries &s : m_channels)
    {
        if (s.key() == key)
            return renderOne(s, err);
    }

    PlotSeries s;
    s.datasetId = datasetId;
    s.table = table;
    s.column = column;
    s.title = QStringLiteral("%1 / %2.%3").arg(datasetId).arg(table, column);
    // 颜色由画布按当前已有条数分配；这里留空，renderOne 会补上。
    if (!renderOne(s, err))
        return false;
    m_channels.append(s);
    return true;
}

bool ChannelView::showChannel(DatasetId datasetId, const QString &table, const QString &column,
                              QString *err)
{
    return addChannel(datasetId, table, column, err);
}

bool ChannelView::removeChannel(DatasetId datasetId, const QString &table, const QString &column)
{
    const QString key = makeKey(datasetId, table, column);
    bool removed = false;
    for (int i = m_channels.size() - 1; i >= 0; --i)
    {
        if (m_channels.at(i).key() == key)
        {
            m_channels.removeAt(i);
            removed = true;
        }
    }
    if (removed)
        renderAll(nullptr);
    return removed;
}

void ChannelView::clearChannels()
{
    m_channels.clear();
    m_windowSet = false;
    if (m_canvas)
        m_canvas->clear();
}

bool ChannelView::renderOne(PlotSeries &series, QString *err)
{
    // 取数走意图层：GUI 与 CLI/脚本同一条路径，行为一致、可录制回放。
    Intent intent;
    intent.op = QStringLiteral("plot.channel");
    intent.params[QStringLiteral("dataset")] = series.datasetId;
    intent.params[QStringLiteral("table")] = series.table;
    intent.params[QStringLiteral("column")] = series.column;
    const int px = qMax(64, m_canvas->plotPixelWidth());
    intent.params[QStringLiteral("px")] = px;
    if (m_windowSet && m_winT1 > m_winT0)
    {
        intent.params[QStringLiteral("t0Us")] = qint64(m_winT0 * 1e6);
        intent.params[QStringLiteral("t1Us")] = qint64(m_winT1 * 1e6);
    }

    const Result<IntentResult> result = m_executor.execute(intent);
    if (!result)
    {
        if (err)
            *err = result.error().message;
        return false;
    }

    const QVariantMap payload = result.value().payload.toMap();
    const QVariantList xs = payload.value(QStringLiteral("x")).toList();
    const QVariantList mins = payload.value(QStringLiteral("min")).toList();
    const QVariantList maxs = payload.value(QStringLiteral("max")).toList();

    QVector<LodTriple> lod;
    lod.reserve(xs.size());
    for (int i = 0; i < xs.size(); ++i)
    {
        LodTriple triple;
        triple.x = xs.at(i).toDouble();
        triple.min = mins.at(i).toDouble();
        triple.max = maxs.at(i).toDouble();
        lod.append(triple);
    }

    series.lod = lod;
    series.t0 = payload.value(QStringLiteral("t0")).toDouble();
    series.t1 = payload.value(QStringLiteral("t1")).toDouble();
    if (series.title.isEmpty())
        series.title = payload.value(QStringLiteral("title")).toString();
    // 把这条曲线写进画布（画布按 key 更新/追加，并分配颜色）。
    m_canvas->setSeries(series);
    // 回读画布分配的颜色，保持稳定（下次重取不换色）。
    const int idx = m_canvas->seriesCount() - 1;
    if (const PlotSeries *inCanvas = m_canvas->seriesAt(idx))
        if (inCanvas->key() == series.key())
            series.color = inCanvas->color;
    return true;
}

bool ChannelView::renderAll(QString *err)
{
    bool allOk = true;
    for (PlotSeries &s : m_channels)
    {
        if (!renderOne(s, err))
            allOk = false;
    }
    return allOk;
}

void ChannelView::refresh()
{
    if (m_channels.isEmpty())
        return;
    renderAll(nullptr);
}

QWidget *createPlotCanvas(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
{
    // 画布：纯绘制，不认识数据。
    auto *canvas = new PlotCanvas(parent);

    // 通道视图：负责"取数 + 填画布"，走意图层。
    auto *view = new ChannelView(executor, canvas, canvas);

    // 订阅共享的"活动通道"：数据集树双击 → 追加一条曲线。
    // 两个视图互不认识——协作全靠 AppContext 这一份共享状态。
    QObject::connect(&appCtx, &AppContext::activeChannelChanged, view,
                     [view](int datasetId, const QString &t, const QString &c) {
                         view->addChannel(datasetId, t, c, nullptr);
                     });

    // 数据集树的"列"节点拖到画布：画布只解码 MIME 发信号，这里接住后
    // 走与双击完全相同的 addChannel（→ plot.channel 意图）取数渲染。
    QObject::connect(canvas, &PlotCanvas::channelDropped, view,
                     [&appCtx](int datasetId, const QString &t, const QString &c) {
                         appCtx.setActiveChannel(datasetId, t, c);
                     });

    // 视图动作：导出 / 视图状态 / 图例开关 / 多曲线管理，由画布与视图执行。
    // 模块与外壳都不持有画布指针——外壳只按 id 派发，处理器在这里登记。
    appCtx.registerViewAction(QStringLiteral("plot.export.png"),
                              [canvas](const QVariantMap &params) -> Result<QVariant> {
                                  const QString path = params.value(QStringLiteral("path")).toString();
                                  if (path.isEmpty())
                                      return fail(QStringLiteral("epath"), QStringLiteral("缺少导出路径"));
                                  if (!canvas->exportPng(path))
                                      return fail(QStringLiteral("eio"), QStringLiteral("PNG 导出失败"));
                                  return QVariant(path);
                              });
    appCtx.registerViewAction(QStringLiteral("plot.export.csv"),
                              [canvas](const QVariantMap &params) -> Result<QVariant> {
                                  const QString path = params.value(QStringLiteral("path")).toString();
                                  if (path.isEmpty())
                                      return fail(QStringLiteral("epath"), QStringLiteral("缺少导出路径"));
                                  if (!canvas->exportCsv(path))
                                      return fail(QStringLiteral("eio"), QStringLiteral("CSV 导出失败"));
                                  return QVariant(path);
                              });
    appCtx.registerViewAction(QStringLiteral("plot.state.save"),
                              [canvas](const QVariantMap &) -> Result<QVariant> {
                                  return QVariant(canvas->saveViewState());
                              });
    appCtx.registerViewAction(QStringLiteral("plot.state.restore"),
                              [canvas](const QVariantMap &params) -> Result<QVariant> {
                                  const auto state = params.value(QStringLiteral("state")).toJsonObject();
                                  if (!canvas->restoreViewState(state))
                                      return fail(QStringLiteral("estate"), QStringLiteral("视图状态复原失败"));
                                  return QVariant(true);
                              });
    appCtx.registerViewAction(QStringLiteral("plot.legend.toggle"),
                              [canvas](const QVariantMap &) -> Result<QVariant> {
                                  canvas->setLegendVisible(!canvas->legendVisible());
                                  return QVariant(canvas->legendVisible());
                              });
    // 清空全部曲线。
    appCtx.registerViewAction(QStringLiteral("plot.series.clear"),
                              [view](const QVariantMap &) -> Result<QVariant> {
                                  view->clearChannels();
                                  return QVariant(true);
                              });
    // 当前曲线清单（供属性面板与脚本查询）。
    appCtx.registerViewAction(QStringLiteral("plot.series.list"),
                              [canvas](const QVariantMap &) -> Result<QVariant> {
                                  QVariantList list;
                                  for (const PlotSeries &s : canvas->series())
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
    // 双 Y 轴：把某条曲线挂到左轴或右轴（用户选择入口）。
    // params: key=曲线稳定键, right=true→右轴 / false→左轴。
    appCtx.registerViewAction(QStringLiteral("plot.series.axis"),
                              [canvas](const QVariantMap &params) -> Result<QVariant> {
                                  const QString key = params.value(QStringLiteral("key")).toString();
                                  const bool right = params.value(QStringLiteral("right")).toBool();
                                  if (key.isEmpty())
                                      return fail(QStringLiteral("ekey"), QStringLiteral("缺少曲线 key"));
                                  if (!canvas->setSeriesAxis(key, right))
                                      return fail(QStringLiteral("enoent"), QStringLiteral("曲线不存在"));
                                  return QVariant(true);
                              });
    // 图型切换：曲线/散点在本画布内切；仪表/地图/3D 本画布不处理（返回 handled=false），
    // 由外壳 dispatcher 切到专用 InstrumentView。
    appCtx.registerViewAction(QStringLiteral("plot.setType"),
                              [canvas](const QVariantMap &params) -> Result<QVariant> {
                                  const QString kind = params.value(QStringLiteral("type")).toString();
                                  const bool handled = canvas->setPlotKind(kind);
                                  QVariantMap r;
                                  r.insert(QStringLiteral("handled"), handled);
                                  r.insert(QStringLiteral("kind"), kind);
                                  return QVariant(r);
                              });

    return canvas;
}

// 视图层自注册：绘图视图的工厂登记。外壳与其它功能都不认识它。
NG_REGISTER_PANEL("plot-canvas", plotCanvas, createPlotCanvas)

} // namespace nextpilot::plotscope::gui

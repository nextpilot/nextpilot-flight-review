#include "ChannelView.h"

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "PanelFactoryRegistry.h"
#include "Result.h"

#include <QJsonObject>
#include <QTimer>

namespace ng::gui
{

using namespace ng::core;

ChannelView::ChannelView(IIntentExecutor &executor, PlotCanvas *canvas, QObject *parent)
    : QObject(parent), m_executor(executor), m_canvas(canvas)
{
    if (!m_canvas)
        return;

    // 合并定时器：视口连续变化时只重取最后一次。
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(120);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });

    // 缩放/平移改变可见时间窗 → 按新窗口重取 LOD（带合并定时器）。
    // 不重取的话，放大后看到的仍是整段数据的包络，糊成一片。
    connect(m_canvas, &PlotCanvas::rangeChanged, this, [this](double t0, double t1) {
        m_windowSet = true;
        m_winT0 = t0;
        m_winT1 = t1;
        m_refreshTimer.start();
    });

    // 视口尺寸变化 → 按新像素宽重新取 LOD。
    //
    // 这是**分辨率正确性**的前提：抽稀的桶数必须等于画布实际像素宽。
    // 若按旧宽度取数再拉伸，曲线会呈现梳齿状（每桶跨多个采样周期，
    // min/max 被撑到值域两端），看起来像方波——是假象，不是数据。
    //
    // 用**合并定时器**而非直接重取：拖动窗口时 resize 事件连发，
    // 每次都全量重扫百万行会把界面卡死。合并成一次即可——
    // 反正用户停下来后只需要最终分辨率那一版。
    connect(m_canvas, &PlotCanvas::viewportChanged, this, [this] {
        m_refreshTimer.start();
    });
}

bool ChannelView::showChannel(DatasetId datasetId, const QString &table, const QString &column,
                               QString *err)
{
    if (!m_canvas)
        return false;

    // 记住当前通道：视口变化时按它重新取数。
    m_datasetId = datasetId;
    m_table = table;
    m_column = column;
    // 换通道 = 换数据，旧的时间窗不再适用。
    m_windowSet = false;
    return doRender(err);
}

bool ChannelView::doRender(QString *err)
{
    if (!m_canvas)
        return false;
    const DatasetId datasetId = m_datasetId;
    const QString table = m_table;
    const QString column = m_column;

    // 取数走意图层：GUI 与 CLI/脚本同一条路径，行为一致、可录制回放。
    Intent intent;
    intent.op = QStringLiteral("plot.channel");
    intent.params[QStringLiteral("dataset")] = datasetId;
    intent.params[QStringLiteral("table")] = table;
    intent.params[QStringLiteral("column")] = column;
    // 像素级请求量按画布**实际绘图区**宽度定：绘制成本与像素宽相关，
    // 与行数无关。由画布自己回答"我多宽"（PlotCanvas::plotPixelWidth），
    // 上层不重复推导边距常量。
    const int px = qMax(64, m_canvas->plotPixelWidth());
    intent.params[QStringLiteral("px")] = px;
    // 可见时间窗（秒 → 微秒）：缩放后按窗口抽稀，细节才跟得上。
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

    m_canvas->setChannel(datasetId, table, column,
                         payload.value(QStringLiteral("title")).toString());
    m_canvas->setLodData(lod, payload.value(QStringLiteral("t0")).toDouble(),
                          payload.value(QStringLiteral("t1")).toDouble());

    // 画布尺寸在**布局稳定之后**才是最终值（dock 区域、ribbon 重建都会改变它）。
    // 所以取完数再排一次合并重取：等布局落定后按最终像素宽再取一版。
    // 否则桶数会按"中间态宽度"算，与最终画布差几十像素——
    // 表现为曲线右侧留白/拉伸，观感上像是"没画满"。
    m_refreshTimer.start();
    return true;
}

void ChannelView::refresh()
{
    if (m_datasetId == kInvalidDataset || m_table.isEmpty() || m_column.isEmpty())
        return;
    doRender(nullptr);
}

QWidget *createPlotCanvas(AppContext &appCtx, IIntentExecutor &executor, QWidget *parent)
{
    // 画布：纯绘制，不认识数据。
    auto *canvas = new PlotCanvas(parent);

    // 通道视图：负责"取数 + 填画布"，走意图层。
    auto *view = new ChannelView(executor, canvas, canvas);

    // 订阅共享的"活动通道"：数据集树双击 → 这里自动重绘。
    // 两个视图互不认识——协作全靠 AppContext 这一份共享状态。
    QObject::connect(&appCtx, &AppContext::activeChannelChanged, view,
                     [view](int datasetId, const QString &t, const QString &c) {
                         view->showChannel(datasetId, t, c);
                     });

    // 视图动作：导出 / 视图状态 / 图例开关，由画布自己执行。
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

    return canvas;
}

// 视图层自注册：绘图视图的工厂登记。外壳与其它功能都不认识它。
NG_REGISTER_PANEL("plot-canvas", plotCanvas, createPlotCanvas)

} // namespace ng::gui

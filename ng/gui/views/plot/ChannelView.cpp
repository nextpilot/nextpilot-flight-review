#include "ChannelView.h"

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "PanelFactoryRegistry.h"
#include "Result.h"

namespace ng::gui
{

using namespace ng::core;

ChannelView::ChannelView(IIntentExecutor &executor, PlotCanvas *canvas, QObject *parent)
    : QObject(parent), m_executor(executor), m_canvas(canvas)
{
}

bool ChannelView::showChannel(DatasetId datasetId, const QString &table, const QString &column,
                               QString *err)
{
    if (!m_canvas)
        return false;

    // 取数走意图层：GUI 与 CLI/脚本同一条路径，行为一致、可录制回放。
    Intent intent;
    intent.op = QStringLiteral("plot.channel");
    intent.params[QStringLiteral("dataset")] = datasetId;
    intent.params[QStringLiteral("table")] = table;
    intent.params[QStringLiteral("column")] = column;
    // 像素级请求量按画布实际宽度定：绘制成本与像素宽相关，与行数无关。
    intent.params[QStringLiteral("px")] = qMax(64, m_canvas->width() - 80);

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
    return true;
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
                     [view](int datasetId, const QString &table, const QString &column) {
                         view->showChannel(datasetId, table, column);
                     });

    return canvas;
}

// 视图层自注册：绘图视图的工厂登记。外壳与其它功能都不认识它。
NG_REGISTER_PANEL("plot-canvas", plotCanvas, createPlotCanvas)

} // namespace ng::gui

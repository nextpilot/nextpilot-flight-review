#include "ChannelView.h"

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "Result.h"
#include "ErrorCodes.h"
#include "Transform.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace nextpilot::plotscope::gui
{

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

ChannelView::ChannelView(const TransformHub &transforms, IIntentExecutor &executor,
                         AxesView *canvas, QObject *parent)
    : m_transforms(transforms), QObject(parent), m_executor(executor), m_canvas(canvas)
{
    if (!m_canvas)
        return;

    // 合并定时器：视口连续变化时只重取最后一次。
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(120);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });

    // 缩放/平移改变可见时间窗 → 按新窗口重取全部曲线（带合并定时器）。
    connect(m_canvas, &AxesView::rangeChanged, this, [this](double t0, double t1) {
        m_windowSet = true;
        m_winT0 = t0;
        m_winT1 = t1;
        m_refreshTimer.start();
    });

    // 视口尺寸变化 → 按新像素宽重新取 LOD（分辨率正确性）。
    connect(m_canvas, &AxesView::viewportChanged, this, [this] {
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
                             QString *err, const QString &title, const QColor &color)
{
    if (!m_canvas)
        return false;
    const QString key = makeKey(datasetId, table, column);

    // 去重：同通道已存在则只刷新它，不重复叠加。
    for (PlotSeries &s : m_channels)
    {
        if (s.key() == key)
        {
            // 恢复链显式带色时，即便曲线已存在也要把存盘色写回（覆盖 palette 色）。
            if (color.isValid())
            {
                s.color = color;
                m_canvas->setSeriesInk(s.key(), color);
            }
            return renderOne(s, err);
        }
    }

    PlotSeries s;
    s.datasetId = datasetId;
    s.table = table;
    s.column = column;
    s.title = title.isEmpty() ? QStringLiteral("%1 / %2.%3").arg(datasetId).arg(table, column) : title;
    // 显式色（恢复链读回的存盘色）优先；否则留空由画布 palette 分配。
    if (color.isValid())
        s.color = color;
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
    if (removed && m_canvas)
    {
        // 画布上这条也得摘掉：renderAll 只重画"还在 m_channels 里的"，
        // 不处理已删除的，否则源画布图上会留一条幽灵曲线（跨画布移动时最明显）。
        if (m_channels.isEmpty())
        {
            m_canvas->clear();
            m_windowSet = false;
        }
        else
        {
            m_canvas->removeSeries(key);
            renderAll(nullptr);
        }
    }
    return removed;
}

void ChannelView::clearChannels()
{
    m_channels.clear();
    m_windowSet = false;
    if (m_canvas)
        m_canvas->clear();
}

int ChannelView::rebindDataset(const std::function<int(DatasetId, const QString &, const QString &)> &resolve)
{
    int rebound = 0;
    for (int i = 0; i < m_channels.size(); ++i)
    {
        PlotSeries &s = m_channels[i];
        if (s.datasetId < 0)
            continue;
        const int oldId = s.datasetId;
        const int newId = resolve(oldId, s.table, s.column);
        if (newId < 0 || newId == oldId)
            continue;

        // 曲线属性（显式色、Y 变换配方、标题）都留在 s 上，跟着换 id——
        // 对标 PJ4 kept_derived：派生对象的身份不随数据集消失而丢。
        const QString oldKey = s.key();
        s.datasetId = newId;
        QString err;
        if (renderOne(s, &err))
        {
            // 新 key 已由 renderOne 写进画布；旧 key 的条目摘掉，不留幽灵曲线。
            if (m_canvas)
                m_canvas->removeSeries(oldKey);
            ++rebound;
        }
        else
        {
            // 新数据集取数失败（列没了/格式变了）：回滚到旧 id，曲线留原地
            // 等下一次重绑机会，不静默丢曲线。
            s.datasetId = oldId;
        }
    }
    return rebound;
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
        intent.params[QStringLiteral("t0Ns")] = qint64(m_winT0 * 1e9);
        intent.params[QStringLiteral("t1Ns")] = qint64(m_winT1 * 1e9);
    }

    // plot 级 Y 变换（非破坏视图，只影响本曲线）：曲线配方经 plot.channel 的
    // transform 参数下到统一求值器；dataset 级配方由意图层自查（全后端生效）。
    // 只对已注册（非空）配方生效；清除配方后走普通路径。
    if (!series.yTransformId.isEmpty())
    {
        // kind 从注册表补全（TransformRequest::fromJson 依赖 kind 分派执行器）。
        const TransformDescriptor d = m_transforms.get(series.yTransformId);
        QJsonObject req{
            {QStringLiteral("descriptorId"), series.yTransformId},
            {QStringLiteral("kind"), transformKindName(d.kind)},
        };
        if (!series.yTransformParamsJson.isEmpty())
        {
            const QJsonObject params =
                QJsonDocument::fromJson(series.yTransformParamsJson.toUtf8()).object();
            if (!params.isEmpty())
                req[QStringLiteral("params")] = params;
        }
        intent.params[QStringLiteral("transform")] =
            QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact));
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


} // namespace nextpilot::plotscope::gui

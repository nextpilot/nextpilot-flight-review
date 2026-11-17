#include "PlotModule.h"

#include "DataRepository.h"
#include "Dataset.h"
#include "Intent.h"

using namespace ng::core;
using namespace ng::host;

// 把 plot.render 意图编译成 PlotSpec。headless 下这就是最终产物（可导出 JSON）；
// GUI 下渲染器消费同一份 spec。意图层与视图层由此解耦。
class PlotModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(DataRepository &repo, QObject *parent)
        : QObject(parent), m_repo(repo) {}

    bool canHandle(const QString &op) const override
    {
        return op == QStringLiteral("plot.render");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        const DatasetId id = intent.params.value(QStringLiteral("dataset")).toInt();
        const Dataset *ds = m_repo.get(id);
        if (!ds)
            return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(id));

        const QString channel = intent.params.value(QStringLiteral("channel")).toString();
        const SeriesData *series = channel.isEmpty() ? ds->series(0) : ds->series(channel);
        if (!series || series->empty())
            return fail(QStringLiteral("echan"), QStringLiteral("通道不可用: %1").arg(channel));

        // PlotSpec：与 GUI 无关的纯描述。
        QVariantMap spec;
        spec[QStringLiteral("type")] = QStringLiteral("timeseries");
        spec[QStringLiteral("title")] = QStringLiteral("%1 / %2").arg(ds->name(), series->name);
        spec[QStringLiteral("channel")] = series->name;
        spec[QStringLiteral("points")] = series->size();
        spec[QStringLiteral("t0")] = ds->timeMin();
        spec[QStringLiteral("t1")] = ds->timeMax();

        IntentResult r;
        r.payload = spec;
        return r;
    }

private:
    DataRepository &m_repo;
};

ModuleInfo PlotModule::info() const
{
    return ModuleInfo{
        QStringLiteral("mod.plot"),
        QStringLiteral("绘图模块"),
        QStringLiteral("1.0"),
        ModuleKind::Builtin,
        {QStringLiteral("plot.renderer")},
        {QStringLiteral("data.session")} // 依赖数据模块的能力，不 include 它的头
    };
}

void PlotModule::initialize(ModuleContext &ctx)
{
    m_handler = new Handler(ctx.data(), this);
    ctx.registerIntentHandler(m_handler);
}

#include "PlotModule.moc"

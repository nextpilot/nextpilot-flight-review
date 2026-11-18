#include "PlotModule.h"

#include "DataRepository.h"
#include "Dataset.h"
#include "Intent.h"

using namespace ng::core;
using namespace ng::host;

// 把 plot.render 意图编译成 PlotSpec。headless 下这就是最终产物（可导出 JSON）；
// GUI 下渲染器消费同一份 spec。意图层与视图层由此解耦。
// 数据访问全部经 Dataset 门面走 Rust C ABI：通道读 ps_dataset_read_window，
// 视口抽稀走 ps_dataset_lod_minmax（降采样在 Rust 侧完成）。绘图模块不认识任何格式。
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

        // 通道限定符："table.column"；缺省时取全库第一个通道。
        const QString qualified = intent.params.value(QStringLiteral("channel")).toString();
        std::optional<ChannelView> view;
        if (qualified.isEmpty())
        {
            const QVector<ChannelView> all = ds->allChannels();
            if (!all.isEmpty())
                view = all.first();
        }
        else
        {
            view = ds->channel(qualified);
        }
        if (!view || view->empty())
            return fail(QStringLiteral("echan"), QStringLiteral("通道不可用: %1").arg(qualified));

        // PlotSpec：与 GUI 无关的纯描述。points 来自 Rust read_window 的结果。
        QVariantMap spec;
        spec[QStringLiteral("type")] = QStringLiteral("timeseries");
        spec[QStringLiteral("title")] = QStringLiteral("%1 / %2").arg(ds->name(), view->name);
        spec[QStringLiteral("table")] = view->table;
        spec[QStringLiteral("column")] = view->column;
        spec[QStringLiteral("valueType")] = QString::fromLatin1(valueTypeName(view->type));
        spec[QStringLiteral("points")] = view->size();
        spec[QStringLiteral("t0")] = view->time.isEmpty() ? 0.0 : view->time.first();
        spec[QStringLiteral("t1")] = view->time.isEmpty() ? 0.0 : view->time.last();

        // 视口 LOD：若意图带像素宽度 px，调用 Rust 侧抽稀并把三元组数写入 spec。
        // 证明"降采样在 Rust 侧完成"，C++ 不持有全量大数据副本。
        const int px = intent.params.value(QStringLiteral("px"), 0).toInt();
        if (px > 0)
        {
            const TimeUs t0 = static_cast<TimeUs>(spec[QStringLiteral("t0")].toDouble() * 1000000.0);
            const TimeUs t1 = static_cast<TimeUs>(spec[QStringLiteral("t1")].toDouble() * 1000000.0);
            const QVector<LodTriple> lod = ds->lodMinMax(view->table, view->column, t0, t1, px);
            spec[QStringLiteral("lodTriples")] = lod.size();
            spec[QStringLiteral("px")] = px;
        }

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

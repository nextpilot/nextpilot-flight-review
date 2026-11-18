#include "PlotModule.h"

#include "BuiltinModuleRegistry.h"
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

    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("plot.render")
            || intent.op == QStringLiteral("plot.channel");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        if (intent.op == QStringLiteral("plot.channel"))
            return renderChannel(intent);
        return renderSpec(intent);
    }

private:
    // plot.channel：取一条通道的**像素级**绘图数据（GUI 画布直接消费）。
    //
    // 内存铁律：返回的 (x,min,max) 三元组个数恒定 <= px，与数据总行数无关。
    // 抽稀（min/max 分桶）在 Rust kernel 内完成，这里只做一次调用与转义。
    Result<IntentResult> renderChannel(const Intent &intent)
    {
        const DatasetId id = intent.params.value(QStringLiteral("dataset")).toInt();
        const Dataset *ds = m_repo.get(id);
        if (!ds)
            return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(id));

        const QString table = intent.params.value(QStringLiteral("table")).toString();
        const QString column = intent.params.value(QStringLiteral("column")).toString();
        if (table.isEmpty() || column.isEmpty())
            return fail(QStringLiteral("echan"), QStringLiteral("缺少 table.column"));

        const Table *t = ds->table(table);
        if (!t || !t->column(column))
            return fail(QStringLiteral("echan"), QStringLiteral("通道不可用: %1.%2").arg(table, column));

        // 像素宽度由调用方给（画布宽度）；缺省 1024，恒定有界。
        const int px = qBound(16, intent.params.value(QStringLiteral("px"), 1024).toInt(), 8192);

        const QVector<LodTriple> lod =
            ds->lodMinMax(table, column, t->firstTimeUs, t->lastTimeUs, px);
        if (lod.isEmpty())
            return fail(QStringLiteral("echan"), QStringLiteral("通道无数据: %1.%2").arg(table, column));

        QVariantList xs, mins, maxs;
        xs.reserve(lod.size());
        mins.reserve(lod.size());
        maxs.reserve(lod.size());
        for (const LodTriple &triple : lod)
        {
            xs.append(triple.x);
            mins.append(triple.min);
            maxs.append(triple.max);
        }

        IntentResult r;
        r.payload = QVariantMap{
            {QStringLiteral("dataset"), id},
            {QStringLiteral("table"), table},
            {QStringLiteral("column"), column},
            {QStringLiteral("title"), QStringLiteral("%1 / %2.%3").arg(ds->name(), table, column)},
            {QStringLiteral("points"), lod.size()},
            {QStringLiteral("t0"), t->firstTimeUs / 1000000.0},
            {QStringLiteral("t1"), t->lastTimeUs / 1000000.0},
            {QStringLiteral("x"), xs},
            {QStringLiteral("min"), mins},
            {QStringLiteral("max"), maxs},
        };
        return r;
    }

    // plot.render：编译 PlotSpec（纯数据），headless 下即为最终产物。
    Result<IntentResult> renderSpec(const Intent &intent)
    {
        const DatasetId id = intent.params.value(QStringLiteral("dataset")).toInt();
        const Dataset *ds = m_repo.get(id);
        if (!ds)
            return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(id));

        // 通道限定符："table.column"；缺省时取全库第一个通道名。
        QString qualified = intent.params.value(QStringLiteral("channel")).toString();
        if (qualified.isEmpty())
        {
            const QStringList tables = ds->tableNames();
            if (tables.isEmpty() || ds->table(tables.first())->columns.isEmpty())
                return fail(QStringLiteral("echan"), QStringLiteral("数据集无通道"));
            qualified = tables.first() + QLatin1Char('.')
                      + ds->table(tables.first())->columns.first().name;
        }
        const int dot = qualified.indexOf(QLatin1Char('.'));
        if (dot <= 0)
            return fail(QStringLiteral("echan"), QStringLiteral("通道名格式错误: %1").arg(qualified));
        const QString tableName = qualified.left(dot);
        const QString columnName = qualified.mid(dot + 1);
        const Table *t = ds->table(tableName);
        if (!t || !t->column(columnName))
            return fail(QStringLiteral("echan"), QStringLiteral("通道不可用: %1").arg(qualified));
        const Column *col = t->column(columnName);

        // 绘图路径的内存铁律：只看屏幕像素级数据，与总行数无关。
        //   - 带 px：直接走 Rust LOD，输出恒定为 px 个 (x,min,max) 三元组，不读 full channel。
        //   - 无 px：走有界 channel（默认 cap 4096），kernel 内等步长抽稀。
        const int px = intent.params.value(QStringLiteral("px"), 0).toInt();
        QVariantMap spec;
        spec[QStringLiteral("type")] = QStringLiteral("timeseries");
        spec[QStringLiteral("title")] = QStringLiteral("%1 / %2").arg(ds->name(), qualified);
        spec[QStringLiteral("table")] = tableName;
        spec[QStringLiteral("column")] = columnName;
        spec[QStringLiteral("valueType")] = QString::fromLatin1(valueTypeName(col->type));
        spec[QStringLiteral("rows")] = t->rows; // 数据总行数（元数据，不读数据）

        if (px > 0)
        {
            const QVector<LodTriple> lod = ds->lodMinMax(
                tableName, columnName, t->firstTimeUs, t->lastTimeUs, px);
            spec[QStringLiteral("mode")] = QStringLiteral("lod");
            spec[QStringLiteral("points")] = lod.size();
            spec[QStringLiteral("lodTriples")] = lod.size();
            spec[QStringLiteral("px")] = px;
            spec[QStringLiteral("t0")] = t->firstTimeUs / 1000000.0;
            spec[QStringLiteral("t1")] = t->lastTimeUs / 1000000.0;
        }
        else
        {
            const auto view = ds->channel(tableName, columnName);
            if (!view || view->empty())
                return fail(QStringLiteral("echan"), QStringLiteral("通道无数据: %1").arg(qualified));
            spec[QStringLiteral("mode")] = QStringLiteral("window");
            spec[QStringLiteral("points")] = view->size();
            spec[QStringLiteral("t0")] = view->time.first();
            spec[QStringLiteral("t1")] = view->time.last();
        }

        IntentResult r;
        r.payload = spec;
        return r;
    }

private:
    DataRepository &m_repo;
};

ModuleInfo PlotModule::moduleInfo()
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

IModule *PlotModule::create()
{
    return new PlotModule();
}

ModuleInfo PlotModule::info() const
{
    return moduleInfo();
}

void PlotModule::initialize(ModuleContext &ctx)
{
    m_handler = new Handler(ctx.data(), this);
    ctx.registerIntentHandler(m_handler);
}

void PlotModule::onAttach(UiContext &ui)
{
    // GUI 环境下：声明本模块的界面入口。
    //
    // 绘图模块往 ribbon 的"视图"分组放一个动作，说明自己提供"重新渲染"能力；
    // 插槽由 app 骨架声明，模块只往里填内容——新增模块自带界面时，
    // 只需在自己的 onAttach 里 declareSlot/addAction，其他文件一律不动。
    ui.declareSlot(QStringLiteral("ribbon.group.home.view"), QStringLiteral("视图"),
                   SlotKind::RibbonGroup, QStringLiteral("ribbon.page.home"));

    UiAction action;
    action.id = QStringLiteral("plot.action.redraw");
    action.label = QStringLiteral("重绘");
    action.area = QStringLiteral("ribbon.group.home.view");
    action.intentOp = QStringLiteral("plot.render");
    ui.addAction(action.area, action);
}

#include "PlotModule.moc"

// 编译期自注册：新增绘图模块无需改 App / 宿主。
NG_REGISTER_MODULE(PlotModule)

#include "PlotModule.h"

#include "PlotDocument.h"

#include "BuiltinModuleHub.h"
#include "ChannelTransformConfig.h"
#include "DatasetHub.h"
#include "Dataset.h"
#include "Intent.h"
#include "RuntimeHost.h"
#include "SettingsStore.h"
#include "SourceHub.h"
#include "Transform.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::plot;
using namespace nextpilot::plotscope::host;

// 把 plot.render 意图编译成 PlotSpec。headless 下这就是最终产物（可导出 JSON）；
// GUI 下渲染器消费同一份 spec。意图层与视图层由此解耦。
// 数据访问全部经 Dataset 门面走 Rust C ABI：通道读 ps_dataset_read_window，
// 视口抽稀走 ps_dataset_lod_minmax（降采样在 Rust 侧完成）。绘图模块不认识任何格式。

namespace
{
// 变换路径的 C++ min/max 分桶抽稀：与 Rust lodMinMax 同语义（每桶取最小/最大，
// x 用桶首样本时间）。桶数恒 <= px。
QVector<LodTriple> minmaxBuckets(const QVector<double> &t, const QVector<double> &v, int n, int px)
{
    QVector<LodTriple> out;
    if (n <= 0 || px <= 0)
        return out;
    if (n <= px)
    {
        out.reserve(n);
        for (int i = 0; i < n; ++i)
            out.append({t.at(i), v.at(i), v.at(i)});
        return out;
    }
    const double bin = double(n) / px;
    out.reserve(px);
    int i = 0;
    for (int b = 0; b < px && i < n; ++b)
    {
        const int end = qMin(n, int((b + 1) * bin));
        if (end <= i)
            continue;
        double mn = v.at(i), mx = v.at(i);
        for (int k = i + 1; k < end; ++k)
        {
            mn = qMin(mn, v.at(k));
            mx = qMax(mx, v.at(k));
        }
        out.append({t.at(i), mn, mx});
        i = end;
    }
    return out;
}
} // namespace

class PlotModule::Handler final : public QObject, public IIntentHandler
{
    Q_OBJECT
public:
    explicit Handler(nextpilot::plotscope::context::AppContext &appCtx, QObject *parent)
        : QObject(parent), m_appCtx(appCtx), m_repo(appCtx.data()), m_settings(appCtx.settings()),
          m_sources(appCtx.sources()), m_interactions(appCtx.interactions()) {}

    bool canHandle(const Intent &intent) const override
    {
        return intent.op == QStringLiteral("plot.render")
            || intent.op == QStringLiteral("plot.channel")
            || intent.op == QStringLiteral("plot.layout.save")
            || intent.op == QStringLiteral("plot.layout.load")
            || intent.op == QStringLiteral("plot.instrument");
    }

    Result<IntentResult> execute(const Intent &intent) override
    {
        if (intent.op == QStringLiteral("plot.channel"))
            return renderChannel(intent);
        if (intent.op == QStringLiteral("plot.instrument"))
            return renderInstrument(intent);
        if (intent.op == QStringLiteral("plot.layout.save"))
            return saveLayout(intent);
        if (intent.op == QStringLiteral("plot.layout.load"))
            return loadLayout(intent);
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
            return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(id));

        const QString table = intent.params.value(QStringLiteral("table")).toString();
        const QString column = intent.params.value(QStringLiteral("column")).toString();
        if (table.isEmpty() || column.isEmpty())
            return fail(err::Chan, QStringLiteral("缺少 table.column"));

        const Table *t = ds->table(table);
        if (!t || !t->column(column))
        {
            // 派生名穿透（独立派生字段）：column 命中派生注册表 → 缓存结果
            // 走 C++ min/max 抽稀返回（画布可直接画派生序列）。
            const auto *entry = m_appCtx.derivedHub().get(column);
            if (entry && entry->datasetId == id)
                return derivedChannelPayload(id, table, column, ds, entry->t, entry->v,
                                             qBound(16, intent.params.value(QStringLiteral("px"), 1024).toInt(), 8192),
                                             intent);
            return fail(err::Chan, QStringLiteral("通道不可用: %1.%2").arg(table, column));
        }

        // 像素宽度由调用方给（画布宽度）；缺省 1024，恒定有界。
        const int px = qBound(16, intent.params.value(QStringLiteral("px"), 1024).toInt(), 8192);

        // 可见时间窗（微秒，可选）：缩放后画布只关心这一段。
        // 缺省用全表范围；越界一律夹回表范围，避免取空。
        TimeNs t0Ns = t->firstTimeNs;
        TimeNs t1Ns = t->lastTimeNs;
        if (intent.params.contains(QStringLiteral("t0Ns"))
            && intent.params.contains(QStringLiteral("t1Ns")))
        {
            const qint64 reqT0 = intent.params.value(QStringLiteral("t0Ns")).toLongLong();
            const qint64 reqT1 = intent.params.value(QStringLiteral("t1Ns")).toLongLong();
            const qint64 lo = qMin(reqT0, reqT1);
            const qint64 hi = qMax(reqT0, reqT1);
            t0Ns = qMax<TimeNs>(t->firstTimeNs, TimeNs(lo));
            t1Ns = qMin<TimeNs>(t->lastTimeNs, TimeNs(hi));
            if (t1Ns <= t0Ns)
            {
                t0Ns = t->firstTimeNs;
                t1Ns = t->lastTimeNs;
            }
        }

        const QVector<LodTriple> lod = ds->lodMinMax(table, column, t0Ns, t1Ns, px);
        if (lod.isEmpty())
            return fail(err::Chan, QStringLiteral("通道无数据: %1.%2").arg(table, column));

        // ===== 变换路径（变换 = 非破坏视图：原始 dataset 不动、字段名不变）=====
        // 显式 params["transform"]（plot 级曲线配方）优先；其次 dataset 级配方
        // （data.transform.set 持久化的通道变换，全后端生效）。
        // 变换必须在抽稀前施加——min/max 分桶后再变换，非单调算子（abs/rms）
        // 的包络会错，所以这条路径不走 Rust LOD，改为：窗口原始点 → 统一变换
        // → C++ min/max 抽稀。点数有界（窗口内行数），语义与 LOD 一致。
        QJsonObject transformReq;
        if (intent.params.contains(QStringLiteral("transform")))
        {
            const QVariant tv = intent.params.value(QStringLiteral("transform"));
            transformReq =
                (tv.type() == QVariant::String || tv.type() == QVariant::ByteArray)
                ? QJsonDocument::fromJson(tv.type() == QVariant::ByteArray
                                              ? tv.toByteArray()
                                              : tv.toString().toUtf8())
                      .object()
                : QJsonObject::fromVariantMap(tv.toMap());
        }
        else
        {
            const int srcId = m_sources.sourceOfDataset(id);
            if (srcId >= 0)
            {
                if (const DataSourceInfo *info = m_sources.get(srcId))
                {
                    const ChannelTransformConfig cfg = ChannelTransformConfig::load(m_settings);
                    if (const ChannelTransformSpec *spec =
                            cfg.find(info->origin, table, column))
                        transformReq = spec->transform;
                }
            }
        }

        if (!transformReq.isEmpty() && transformReq.contains(QStringLiteral("descriptorId")))
        {
            if (!m_interactions.hasTransformEvaluator())
                return fail(err::NoEval,
                            QStringLiteral("变换求值器未注册（script 模块未装载）"));

            std::optional<ChannelView> ch = ds->channel(table, column);
            if (!ch)
                return fail(err::Chan, QStringLiteral("读取通道失败: %1.%2").arg(table, column));

            // 时间窗过滤（ChannelView.time 是秒；窗口 t0Ns/t1Ns 是微秒）。
            QVector<double> wt, wv;
            const double w0 = t0Ns / 1000000000.0, w1 = t1Ns / 1000000000.0;
            wt.reserve(ch->size());
            wv.reserve(ch->size());
            for (int i = 0; i < ch->size(); ++i)
            {
                const double ts = ch->time.at(i);
                if (ts >= w0 && ts <= w1)
                {
                    wt.append(ts);
                    wv.append(ch->value.at(i));
                }
            }
            if (wt.isEmpty())
                return fail(err::Chan, QStringLiteral("窗口内无数据: %1.%2").arg(table, column));

            // 统一变换（算子/表达式/Lua）。能力位 CapDerived——通道级变换与
            // 数据源衍生共用"channel 变换"能力，编辑器 UI 按同一位过滤。
            Result<QVector<double>> ev{QVector<double>()};
            m_interactions.evalTransform(transformReq, CapDerived, wt, wv, ev);
            if (!ev)
                return fail(ev.error().code, ev.error().message);

            const QVector<double> &vals = ev.value();
            const int n = qMin(wt.size(), vals.size());
            if (n <= 0)
                return fail(err::Transform, QStringLiteral("变换结果为空"));

            // C++ min/max 分桶抽稀（对齐 Rust lodMinMax 语义：每桶取最小/最大，
            // x 用桶首样本时间）。桶数恒 <= px，内存有界。
            QVector<LodTriple> lodT = minmaxBuckets(wt, vals, n, px);

            QVariantList xs2, mins2, maxs2;
            xs2.reserve(lodT.size());
            mins2.reserve(lodT.size());
            maxs2.reserve(lodT.size());
            for (const LodTriple &triple : lodT)
            {
                xs2.append(triple.x);
                mins2.append(triple.min);
                maxs2.append(triple.max);
            }
            IntentResult r2;
            r2.payload = QVariantMap{
                {QStringLiteral("dataset"), id},
                {QStringLiteral("table"), table},
                {QStringLiteral("column"), column},
                {QStringLiteral("title"),
                 QStringLiteral("%1 / %2.%3").arg(ds->name(), table, column)},
                {QStringLiteral("points"), lodT.size()},
                {QStringLiteral("t0"), t0Ns / 1000000000.0},
                {QStringLiteral("t1"), t1Ns / 1000000000.0},
                {QStringLiteral("x"), xs2},
                {QStringLiteral("min"), mins2},
                {QStringLiteral("max"), maxs2},
                {QStringLiteral("transformed"), true},
            };
            return r2;
        }

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
            {QStringLiteral("t0"), t0Ns / 1000000000.0},
            {QStringLiteral("t1"), t1Ns / 1000000000.0},
            {QStringLiteral("x"), xs},
            {QStringLiteral("min"), mins},
            {QStringLiteral("max"), maxs},
        };
        return r;
    }

    // plot.instrument：为专用仪表（姿态球/罗盘/表盘/轨迹/3D/瀑布）取有界数据。
    //
    // 与 plot.channel 的区别：
    //   - channel 给一条曲线的像素级 LOD；instrument 给仪表需要的标量末值 + 有界序列；
    //   - 一条仪表可能要多个角色通道（roll/pitch、lon/lat、x/y/z），角色由调用方在
    //     roles 里按"角色 → table.column"指定，模块不猜通道语义；
    //   - 数据仍走 Dataset 门面，C++ 不攒全量（cap 内等步长抽稀在 Rust 侧）。
    Result<IntentResult> renderInstrument(const Intent &intent)
    {
        const DatasetId id = intent.params.value(QStringLiteral("dataset")).toInt();
        const Dataset *ds = m_repo.get(id);
        if (!ds)
            return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(id));

        const QString kind = intent.params.value(QStringLiteral("kind")).toString();
        const int cap = qBound(64, intent.params.value(QStringLiteral("cap"), 4096).toInt(), 65536);
        const QVariantMap roles = intent.params.value(QStringLiteral("roles")).toMap();
        const QVariantList channelList = intent.params.value(QStringLiteral("channels")).toList();

        auto readChannel = [&](const QString &qualified) -> std::optional<ChannelView> {
            if (qualified.isEmpty())
                return std::nullopt;
            return ds->channel(qualified, cap);
        };

        QVariantMap scalars;
        QVariantMap series;
        QVariantList channelsOut;
        double dataLo = std::numeric_limits<double>::infinity();
        double dataHi = -std::numeric_limits<double>::infinity();
        bool rangeFromData = false;

        // 一条角色通道：塞进 series（有界序列），末值记进 scalars（表盘/姿态/罗盘用）。
        auto putRole = [&](const QString &role, const QString &qualified) -> bool {
            auto view = readChannel(qualified);
            if (!view || view->empty())
                return false;
            QVariantList tList, vList;
            tList.reserve(view->time.size());
            vList.reserve(view->value.size());
            for (double x : view->time) tList.append(x);
            for (double x : view->value) vList.append(x);
            QVariantMap m;
            m[QStringLiteral("name")] = view->name;
            m[QStringLiteral("time")] = tList;
            m[QStringLiteral("value")] = vList;
            series[role] = m;
            scalars[role] = view->value.last();
            for (const double x : view->value)
            {
                dataLo = std::min(dataLo, x);
                dataHi = std::max(dataHi, x);
                rangeFromData = true;
            }
            return true;
        };

        if (kind == QStringLiteral("attitude"))
        {
            bool ok = false;
            if (roles.contains(QStringLiteral("roll")))
                ok |= putRole(QStringLiteral("roll"), roles.value(QStringLiteral("roll")).toString());
            if (roles.contains(QStringLiteral("pitch")))
                ok |= putRole(QStringLiteral("pitch"), roles.value(QStringLiteral("pitch")).toString());
            if (!ok)
                return fail(err::Chan,
                            QStringLiteral("姿态仪至少需要 roll / pitch 中的一个通道"));
        }
        else if (kind == QStringLiteral("compass"))
        {
            QString yaw = roles.value(QStringLiteral("yaw")).toString();
            if (yaw.isEmpty())
                yaw = roles.value(QStringLiteral("heading")).toString();
            if (!putRole(QStringLiteral("yaw"), yaw))
                return fail(err::Chan, QStringLiteral("罗盘需要 yaw / heading 通道"));
        }
        else if (kind == QStringLiteral("gauge") || kind == QStringLiteral("histogram") ||
                 kind == QStringLiteral("fft"))
        {
            // 表盘/直方图/频谱共用"一条序列"取数：表盘取末值，直方图分箱，频谱做 DFT。
            // 差异在视图端怎么画，模块只负责按 value 角色取有界序列。
            if (!putRole(QStringLiteral("value"), roles.value(QStringLiteral("value")).toString()))
                return fail(err::Chan, kind + QStringLiteral(" 需要 value 通道"));
        }
        else if (kind == QStringLiteral("track"))
        {
            if (!putRole(QStringLiteral("lon"), roles.value(QStringLiteral("lon")).toString()))
                return fail(err::Chan, QStringLiteral("轨迹需要 lon 通道"));
            if (!putRole(QStringLiteral("lat"), roles.value(QStringLiteral("lat")).toString()))
                return fail(err::Chan, QStringLiteral("轨迹需要 lat 通道"));
        }
        else if (kind == QStringLiteral("traj3d"))
        {
            if (!putRole(QStringLiteral("x"), roles.value(QStringLiteral("x")).toString()))
                return fail(err::Chan, QStringLiteral("3D 轨迹需要 x 通道"));
            if (!putRole(QStringLiteral("y"), roles.value(QStringLiteral("y")).toString()))
                return fail(err::Chan, QStringLiteral("3D 轨迹需要 y 通道"));
            if (roles.contains(QStringLiteral("z")))
                putRole(QStringLiteral("z"), roles.value(QStringLiteral("z")).toString());
        }
        else if (kind == QStringLiteral("waterfall") || kind == QStringLiteral("bar"))
        {
            // 瀑布/柱状共用"多通道"取数：瀑布画时间×通道热图，柱状取各通道末值成柱。
            if (channelList.isEmpty())
                return fail(err::Chan, kind + QStringLiteral(" 需要 channels 列表"));
            for (const QVariant &q : channelList)
            {
                auto view = readChannel(q.toString());
                if (!view || view->empty())
                    continue;
                QVariantList tList, vList;
                tList.reserve(view->time.size());
                vList.reserve(view->value.size());
                for (double x : view->time) tList.append(x);
                for (double x : view->value) vList.append(x);
                QVariantMap m;
                m[QStringLiteral("name")] = view->name;
                m[QStringLiteral("time")] = tList;
                m[QStringLiteral("value")] = vList;
                channelsOut.append(m);
            }
            if (channelsOut.isEmpty())
                return fail(err::Chan, kind + QStringLiteral(" 通道全部无数据"));
        }
        else
        {
            return fail(err::Kind, QStringLiteral("未知仪表类型: %1").arg(kind));
        }

        // 表盘量程：调用方显式给了就用；否则按数据 min/max 各留 5% 余量。
        QVariantList range;
        if (intent.params.contains(QStringLiteral("range")))
            range = intent.params.value(QStringLiteral("range")).toList();
        else if (rangeFromData)
        {
            const double pad = (dataHi - dataLo) * 0.05 + 1e-9;
            range = QVariantList{dataLo - pad, dataHi + pad};
        }

        static const QHash<QString, QString> kKindLabel{
            {QStringLiteral("attitude"), QStringLiteral("姿态仪")},
            {QStringLiteral("compass"), QStringLiteral("罗盘")},
            {QStringLiteral("gauge"), QStringLiteral("表盘")},
            {QStringLiteral("track"), QStringLiteral("经纬轨迹")},
            {QStringLiteral("traj3d"), QStringLiteral("3D 轨迹")},
            {QStringLiteral("waterfall"), QStringLiteral("瀑布图")},
            {QStringLiteral("histogram"), QStringLiteral("直方图")},
            {QStringLiteral("fft"), QStringLiteral("频谱")},
            {QStringLiteral("bar"), QStringLiteral("柱状图")},
        };

        IntentResult r;
        r.payload = QVariantMap{
            {QStringLiteral("dataset"), id},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("title"),
             QStringLiteral("%1 · %2").arg(ds->name(), kKindLabel.value(kind, kind))},
            {QStringLiteral("scalars"), scalars},
            {QStringLiteral("series"), series},
            {QStringLiteral("channels"), channelsOut},
            {QStringLiteral("range"), range},
        };
        return r;
    }

    // plot.layout.save：保存 plot 布局（文档作用域）。
    //
    // 布局是**声明式**的：只描述"画布是什么、放在哪、什么样式"，
    // 于是可存盘、可分享、可跨会话恢复；恢复时按 id 匹配，匹配不上的安全跳过。
    Result<IntentResult> saveLayout(const Intent &intent)
    {
        SettingsStore &store = m_settings;
        const QString name = intent.params.value(QStringLiteral("name"),
                                                QStringLiteral("default")).toString();
        const QJsonObject obj = intent.params.value(QStringLiteral("layout")).toJsonObject();
        const PlotDocumentSpec spec = PlotDocumentSpec::fromJson(obj);
        store.setJson(SettingsScope::Document, QStringLiteral("plot/%1").arg(name),
                      spec.toJson());
        IntentResult r;
        r.payload = true;
        return r;
    }

    // plot.layout.load：读回 plot 布局。
    Result<IntentResult> loadLayout(const Intent &intent)
    {
        const QString name = intent.params.value(QStringLiteral("name"),
                                                QStringLiteral("default")).toString();
        const QJsonObject obj = m_settings.json(SettingsScope::Document,
                                                 QStringLiteral("plot/%1").arg(name));
        if (obj.isEmpty())
            return fail(err::NoEnt, QStringLiteral("plot 布局不存在: %1").arg(name));
        IntentResult r;
        r.payload = obj;
        return r;
    }

    // plot.render：编译 PlotSpec（纯数据），headless 下即为最终产物。
    Result<IntentResult> renderSpec(const Intent &intent)
    {
        const DatasetId id = intent.params.value(QStringLiteral("dataset")).toInt();
        const Dataset *ds = m_repo.get(id);
        if (!ds)
            return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(id));

        // 通道限定符："table.column"；缺省时取全库第一个通道名。
        QString qualified = intent.params.value(QStringLiteral("channel")).toString();
        if (qualified.isEmpty())
        {
            const QStringList tables = ds->tableNames();
            if (tables.isEmpty() || ds->table(tables.first())->columns.isEmpty())
                return fail(err::Chan, QStringLiteral("数据集无通道"));
            qualified = tables.first() + QLatin1Char('.')
                      + ds->table(tables.first())->columns.first().name;
        }
        const int dot = qualified.indexOf(QLatin1Char('.'));
        if (dot <= 0)
            return fail(err::Chan, QStringLiteral("通道名格式错误: %1").arg(qualified));
        const QString tableName = qualified.left(dot);
        const QString columnName = qualified.mid(dot + 1);
        const Table *t = ds->table(tableName);
        if (!t || !t->column(columnName))
            return fail(err::Chan, QStringLiteral("通道不可用: %1").arg(qualified));
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
                tableName, columnName, t->firstTimeNs, t->lastTimeNs, px);
            spec[QStringLiteral("mode")] = QStringLiteral("lod");
            spec[QStringLiteral("points")] = lod.size();
            spec[QStringLiteral("lodTriples")] = lod.size();
            spec[QStringLiteral("px")] = px;
            spec[QStringLiteral("t0")] = t->firstTimeNs / 1000000000.0;
            spec[QStringLiteral("t1")] = t->lastTimeNs / 1000000000.0;
        }
        else
        {
            const auto view = ds->channel(tableName, columnName);
            if (!view || view->empty())
                return fail(err::Chan, QStringLiteral("通道无数据: %1").arg(qualified));
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
    DatasetHub &m_repo;
    SettingsStore &m_settings;
    SourceHub &m_sources;
    nextpilot::plotscope::context::InteractionHub &m_interactions;
    nextpilot::plotscope::context::AppContext &m_appCtx;

    // 派生序列取数（独立派生字段）：缓存 t/v → C++ min/max 抽稀 → payload。
    Result<IntentResult> derivedChannelPayload(int id, const QString &table, const QString &column,
                                               const Dataset *ds, const QVector<double> &t,
                                               const QVector<double> &v, int px, const Intent &intent)
    {
        const int n = qMin(t.size(), v.size());
        if (n <= 0)
            return fail(err::Chan, QStringLiteral("派生序列无样本: %1").arg(column));

        // 可选时间窗过滤（秒）。
        double w0 = -1e18, w1 = 1e18;
        if (intent.params.contains(QStringLiteral("t0Ns"))
            && intent.params.contains(QStringLiteral("t1Ns")))
        {
            w0 = intent.params.value(QStringLiteral("t0Ns")).toLongLong() / 1000000000.0;
            w1 = intent.params.value(QStringLiteral("t1Ns")).toLongLong() / 1000000000.0;
        }
        QVector<double> wt, wv;
        wt.reserve(n);
        wv.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const double ts = t.at(i);
            if (ts >= w0 && ts <= w1)
            {
                wt.append(ts);
                wv.append(v.at(i));
            }
        }
        if (wt.isEmpty())
            return fail(err::Chan, QStringLiteral("窗口内无派生数据: %1").arg(column));

        const QVector<LodTriple> lodT = minmaxBuckets(wt, wv, wt.size(), px);
        QVariantList xs, mins, maxs;
        xs.reserve(lodT.size());
        mins.reserve(lodT.size());
        maxs.reserve(lodT.size());
        for (const LodTriple &triple : lodT)
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
            {QStringLiteral("title"), QStringLiteral("%1 / %2").arg(ds->name(), column)},
            {QStringLiteral("points"), lodT.size()},
            {QStringLiteral("t0"), wt.first()},
            {QStringLiteral("t1"), wt.last()},
            {QStringLiteral("x"), xs},
            {QStringLiteral("min"), mins},
            {QStringLiteral("max"), maxs},
            {QStringLiteral("derived"), true},
        };
        return r;
    }
};

ModuleInfo PlotModule::moduleInfo()
{
    return ModuleInfo{
        QStringLiteral("mod.plot"),
        QStringLiteral("绘图模块"),
        QStringLiteral("1.0"),
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

void PlotModule::initialize(RuntimeHost &host)
{
    m_handler = new Handler(host.app(), this);
    host.registerIntentHandler(m_handler);
}

void PlotModule::onAttach(IUiContext &ui)
{
    // GUI 环境下：声明本模块的 Activity 与界面入口。
    //
    // Activity：plot 域的 Activity Bar 入口；工作区是中央画布（centralPanel），
    // 本域没有左栏 section 面板，panelTypes 留空。
    ActivitySpec plotActivity;
    plotActivity.id = QStringLiteral("plot");
    plotActivity.label = QStringLiteral("绘图");
    plotActivity.icon = QStringLiteral("plot_refresh");
    plotActivity.order = 3;
    plotActivity.ribbonPage = QStringLiteral("ribbon.page.plot");
    plotActivity.centralPanel = QStringLiteral("plot-workspace");
    ui.declareActivity(plotActivity);

    // 导出组插槽：模块声明自己的 Ribbon 分组（c4c 起 plot 页/组归模块声明）。
    ui.declareSlot(QStringLiteral("ribbon.group.plot.action"), QStringLiteral("导出"),
                   SlotKind::RibbonGroup, QStringLiteral("ribbon.page.plot"));

    // 申请中央画布工作区：实现由绘图视图层自注册（"plot-workspace" 类型），
    // 它内部管理 N 个画布 cell（多曲线/拆分/网格），并集中路由 plot.* view action。
    // 本模块不依赖 Qt Widgets——CLI 构建保持纯净。
    ui.requestPanel(QStringLiteral("canvas.center"), QStringLiteral("plot-workspace"));

    UiAction action;
    action.id = QStringLiteral("plot.action.redraw");
    action.label = QStringLiteral("重绘");
    action.area = QStringLiteral("ribbon.group.plot.display");
    action.intentOp = QStringLiteral("plot.render");
    // 图标名：外壳据此从资源里取 QIcon。图标名是**描述**，
    // 具体资源由外壳管理——模块不直接依赖 GUI 资源。
    action.icon = QStringLiteral("plot_refresh");
    action.order = 0;   // 显示组首个大按钮（用户要求：重绘排第一）
    action.large = true;
    ui.addAction(action);

    // 导出 / 图例：处理器由画布视图登记到 AppContext，外壳按 id 派发。
    UiAction exportPng;
    exportPng.id = QStringLiteral("plot.export.png");
    exportPng.label = QStringLiteral("导出 PNG");
    exportPng.area = QStringLiteral("ribbon.group.plot.action");
    exportPng.intentOp = QStringLiteral("plot.export.png");
    exportPng.icon = QStringLiteral("export");
    exportPng.order = 1;
    ui.addAction(exportPng);

    UiAction exportCsv;
    exportCsv.id = QStringLiteral("plot.export.csv");
    exportCsv.label = QStringLiteral("导出 CSV");
    exportCsv.area = QStringLiteral("ribbon.group.plot.action");
    exportCsv.intentOp = QStringLiteral("plot.export.csv");
    exportCsv.icon = QStringLiteral("export_data");
    exportCsv.order = 2;
    ui.addAction(exportCsv);

    UiAction legend;
    // id 与 view action 同名：外壳先查视图登记的处理器，按钮点击直接命中，无需回落意图层。
    legend.id = QStringLiteral("plot.legend.toggle");
    legend.label = QStringLiteral("图例");
    legend.area = QStringLiteral("ribbon.group.plot.display");
    legend.intentOp = QStringLiteral("plot.legend.toggle");
    legend.icon = QStringLiteral("legend");
    legend.checkable = true;
    legend.order = 3;
    ui.addAction(legend);
}

#include "PlotModule.moc"

#include "ErrorCodes.h"
// 编译期自注册：新增绘图模块无需改 App / 宿主。
PS_REGISTER_MODULE(PlotModule)

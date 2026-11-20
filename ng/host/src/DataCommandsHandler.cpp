#include "DataCommandsHandler.h"

#include "AppContext.h"
#include "DataSource.h"
#include "SourceRegistry.h"
#include "Dataset.h"
#include "IIntentExecutor.h"
#include "Result.h"
#include "SettingsStore.h"
#include "WorkspaceDataConfig.h"
#include "ChannelTransformConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVector>

#include <cmath>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

namespace
{

// 最近文件：QStringList 存进 settings，最多 10 条，去重、最新在前。
QStringList recentFiles(AppContext &app)
{
    const QVariant v = app.settings().value(QLatin1String(DataCommandsHandler::kRecentKey));
    return v.toStringList();
}

QString cachePathForSource(const DataSourceInfo &info, const QString &parserId)
{
    const QString abs = QFileInfo(info.origin).absoluteFilePath();
    return QStringLiteral("%1.%2.psc").arg(abs, parserId);
}

// 状态视图取数上限：大于绝大多数日志行数，保证不触发 kernel 抽稀（RLE 需要原始样本）。
constexpr int kStateMaxPoints = 200000;
} // namespace

bool DataCommandsHandler::canHandle(const Intent &intent) const
{
    const QString &op = intent.op;
    return op == QLatin1String("data.reload") || op == QLatin1String("data.export")
        || op == QLatin1String("data.recent") || op == QLatin1String("data.clearCache")
        || op == QLatin1String("data.remove")
        || op == QLatin1String("data.pause") || op == QLatin1String("data.resume")
        || op == QLatin1String("data.derive")         || op == QLatin1String("data.filter") || op == QLatin1String("data.marker")
        || op == QLatin1String("data.state")          || op == QLatin1String("data.series")
        || op == QLatin1String("data.workspace.list")
        || op == QLatin1String("data.workspace.save")
        || op == QLatin1String("data.workspace.restore")
        || op == QLatin1String("data.transform.set")
        || op == QLatin1String("data.transform.clear")
        || op == QLatin1String("data.derive.list");
}

Result<IntentResult> DataCommandsHandler::execute(const Intent &intent)
{
    if (intent.op == QLatin1String("data.reload"))
        return reload(intent);
    if (intent.op == QLatin1String("data.export"))
        return exportCsv(intent);
    if (intent.op == QLatin1String("data.recent"))
        return recent(intent);
    if (intent.op == QLatin1String("data.clearCache"))
        return clearCache();
    if (intent.op == QLatin1String("data.remove"))
        return removeSource(intent);
    if (intent.op == QLatin1String("data.pause"))
        return setSourcePaused(intent, true);
    if (intent.op == QLatin1String("data.resume"))
        return setSourcePaused(intent, false);
    if (intent.op == QLatin1String("data.derive"))
        return derive(intent);
    if (intent.op == QLatin1String("data.filter"))
        return filterSeries(intent);
    if (intent.op == QLatin1String("data.marker"))
        return runMarker(intent);
    if (intent.op == QLatin1String("data.state"))
        return stateSeries(intent);
    if (intent.op == QLatin1String("data.series"))
        return series(intent);
    if (intent.op == QLatin1String("data.workspace.list"))
        return workspaceList();
    if (intent.op == QLatin1String("data.workspace.save"))
        return workspaceSave();
    if (intent.op == QLatin1String("data.workspace.restore"))
        return workspaceRestore();
    if (intent.op == QLatin1String("data.transform.set"))
        return setChannelTransform(intent);
    if (intent.op == QLatin1String("data.transform.clear"))
        return clearChannelTransform(intent);
    if (intent.op == QLatin1String("data.derive.list"))
        return deriveList(intent);
    return fail(QStringLiteral("eunknown"),
                QStringLiteral("DataCommandsHandler 不处理: %1").arg(intent.op));
}

Result<IntentResult> DataCommandsHandler::reload(const Intent &intent)
{
    // 活动源：显式 id 参数优先，否则用 AppContext 的活动源。
    int sourceId = intent.params.value(QStringLiteral("id"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    const DataSourceInfo *info = m_app.sources().get(sourceId);
    if (!info || info->origin.isEmpty())
        return fail(QStringLiteral("enosource"), QStringLiteral("没有可重载的数据源"));

    // 记住原绑定（一个源可扇出多个解析器）与产出的数据集 id。
    const QString origin = info->origin;
    const QString format = info->formatHint;
    const QVector<int> oldDatasets = info->datasetIds;

    QVector<int> removed;
    m_app.sources().remove(sourceId, &removed);
    for (int ds : removed)
    {
        // 派生序列是数据集的附属（原始数据不写回，但派生结果引用它）——
        // 数据集移除时同步清理对应派生序列，防止悬空引用。
        m_app.derivedRegistry().removeAllForDataset(ds);
        m_app.data().remove(ds);
    }

    // 复用 data.load 这一条路径——重载与首次加载行为完全一致。
    // 稳定 id：单数据集时用原 id 重新入仓，订阅者（绘图视图）不会因重载失联。
    Intent load;
    load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = origin;
    if (!format.isEmpty())
        load.params[QStringLiteral("format")] = format;
    load.params[QStringLiteral("nocache")] = true; // 重载强制重新解析
    if (oldDatasets.size() == 1)
        load.params[QStringLiteral("id")] = oldDatasets.first();
    return m_executor.execute(load);
}

Result<IntentResult> DataCommandsHandler::exportCsv(const Intent &intent)
{
    // 通道来源：显式指定 dataset/table/column，否则用活动通道。
    int datasetId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    const QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();

    // 活动通道的 datasetId 由 setActiveChannel 记在 AppContext，但这里没有
    // 单独 getter（活动通道以 table/column 为准）；若未显式给 dataset，
    // 取唯一数据集或最近加入的数据集作为默认。
    if (datasetId < 0 && m_app.data().count() == 1)
        datasetId = m_app.data().ids().first();
    if (datasetId < 0)
        return fail(QStringLiteral("edataset"),
                    QStringLiteral("请指定 dataset 参数（当前有多个数据集）"));

    const Dataset *ds = m_app.data().get(datasetId);
    if (!ds)
        return fail(QStringLiteral("edataset"),
                    QStringLiteral("数据集 #%1 不存在").arg(datasetId));

    const QString targetTable = table.isEmpty() && ds->tableCount() == 1
        ? ds->tableNames().first() : table;
    if (!ds->table(targetTable))
        return fail(QStringLiteral("etable"), QStringLiteral("表不存在: %1").arg(targetTable));

    // 没指定列则导出该表全部列。
    QStringList columns;
    if (!column.isEmpty())
        columns << column;
    else if (const Table *t = ds->table(targetTable))
        for (const Column &c : t->columns)
            columns << c.name;
    if (columns.isEmpty())
        return fail(QStringLiteral("ecolumn"), QStringLiteral("没有可导出的列"));

    const QString outPath = intent.params.value(QStringLiteral("path")).toString();
    if (outPath.isEmpty())
        return fail(QStringLiteral("epath"), QStringLiteral("请指定导出路径 path"));

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail(QStringLiteral("ewrite"), QStringLiteral("无法写入: %1").arg(outPath));

    QTextStream out(&file);
    // 表头
    out << QStringLiteral("time");
    for (const QString &c : columns)
        out << ',' << c;
    out << '\n';

    // 逐列取数（channel 有 cap 上限，默认抽稀到 4096 点，保护内存）。
    QVector<ChannelView> views;
    for (const QString &c : columns)
    {
        auto v = ds->channel(targetTable, c);
        if (!v)
            return fail(QStringLiteral("eread"), QStringLiteral("读取列失败: %1").arg(c));
        views.append(*v);
    }
    const int rows = views.isEmpty() ? 0 : views.first().size();
    for (int i = 0; i < rows; ++i)
    {
        out << views.first().time.value(i);
        for (const ChannelView &v : views)
            out << ',' << v.value.value(i);
        out << '\n';
    }
    file.close();

    IntentResult r;
    r.payload = outPath;
    return r;
}

Result<IntentResult> DataCommandsHandler::recent(const Intent &intent)
{
    const QString op = intent.params.value(QStringLiteral("action")).toString();
    QStringList files = recentFiles(m_app);
    if (op == QLatin1String("clear"))
    {
        m_app.settings().setValue(QLatin1String(kRecentKey), QStringList());
        files.clear();
    }
    IntentResult r;
    r.payload = files;
    return r;
}

Result<IntentResult> DataCommandsHandler::clearCache()
{
    int removed = 0;
    for (int id : m_app.sources().ids())
    {
        const DataSourceInfo *info = m_app.sources().get(id);
        if (!info || info->origin.isEmpty())
            continue;
        // 源文件级缓存（parserId 为空的那种）+ 每个解析器的缓存。
        QStringList parsers;
        for (const ParserBinding &b : info->parsers)
            parsers << b.parserId;
        if (parsers.isEmpty())
            parsers << QString();
        for (const QString &pid : parsers)
        {
            const QString path = pid.isEmpty()
                ? QFileInfo(info->origin).absoluteFilePath() + QStringLiteral(".psc")
                : cachePathForSource(*info, pid);
            if (QFile::exists(path) && QFile::remove(path))
                ++removed;
        }
    }
    IntentResult r;
    r.payload = removed;
    return r;
}

Result<IntentResult> DataCommandsHandler::removeSource(const Intent &intent)
{
    // 显式 id 优先，否则移除活动源。对齐老界面垃圾桶：移除源 + 它产出的数据集。
    int sourceId = intent.params.value(QStringLiteral("id"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    const DataSourceInfo *info = m_app.sources().get(sourceId);
    if (!info)
        return fail(QStringLiteral("enosource"), QStringLiteral("没有可移除的数据源"));

    QVector<int> removedDatasets;
    m_app.sources().remove(sourceId, &removedDatasets);
    for (int ds : removedDatasets)
    {
        // 派生序列随数据集移除清理（引用悬空防护）。
        m_app.derivedRegistry().removeAllForDataset(ds);
        m_app.data().remove(ds);
    }

    // 若移除的是活动源，清空活动源，避免后续操作指向已消失的 id。
    if (m_app.activeSourceId() == sourceId)
        m_app.setActiveSourceId(-1);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("id"), sourceId},
        {QStringLiteral("datasets"), removedDatasets.size()},
    };
    return r;
}

// MIMO 派生：四元数 → roll/pitch/yaw（ps-ops::quat::to_rpy，公式对齐 PJ4 toolbox_quaternion）。
//
//   data.derive { dataset, table, inputs: [w列,x列,y列,z列], filter: "quat_to_rpy",
//                 outputs: [roll名,pitch名,yaw名]（缺省 "<w列>.roll/.pitch/.yaw"）,
//                 degrees: false, unwrap: true, interp: "linear" }
//
// 输入元素两种写法：
//   "q.w"                       —— 字符串列名，用顶层 table（四列同表常规用法）
//   {table:"att", column:"q.w"} —— 对象形式，可跨表（列名可能含点，不用限定名字符串避免歧义）
//
// 时间同步语义（统一机制：基准轴 + 显式对齐）：
//   · 所有非基准输入统一对齐到第一个输入的时间轴（基准轴）。时间点完全重合时
//     插值恒等于原值，精度无损。
//   · 同表、轴完全一致（无 null 剔除）走快路径直取；否则插值。这样门面
//     readWindow 剔除 null 样本（绘图语义）造成的同表行数不一致、以及跨表
//     不同采样率，都被同一机制兜住——时间同步不再依赖"行对齐"假设。
//   · interp: linear（缺省，两点线性插值）| step（前向填充）| nearest（最近样本）；
//     范围外取端点值（边界延拓，不产 NaN）。基准轴防御性校验非降序。
//   · 输出与基准同轴：基准列自身被剔除 null 时，输出点数随之（少而准，不猜值）。
//
// 防线：任何输入（对齐后）含 NaN/null 样本（kernel 读 null 即 NaN）→ 明确失败，
// 绝不静默产出垃圾角度。派生语义（不写回）：三条输出全部登记派生注册表 +
// derivedSeriesReady，原始 dataset 零改动。
Result<IntentResult> DataCommandsHandler::deriveMimoQuat(const Intent &intent)
{
    DatasetId dsId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    if (dsId < 0 && m_app.data().count() == 1)
        dsId = m_app.data().ids().first();
    if (dsId < 0)
        return fail(QStringLiteral("edataset"), QStringLiteral("请指定 dataset 参数"));
    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(dsId));

    const QVariantList inputs = intent.params.value(QStringLiteral("inputs")).toList();
    if (inputs.size() != 4)
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("quat_to_rpy 需要 inputs（w/x/y/z 四个输入）"));
    const QString topTable = intent.params.value(QStringLiteral("table")).toString();
    const QString filterId = intent.params.value(QStringLiteral("filter")).toString();
    if (filterId != QLatin1String("quat_to_rpy"))
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("MIMO 派生当前只支持 quat_to_rpy，收到: %1").arg(filterId));

    // 解析输入：对象 {table,column} 或字符串列名（用顶层 table）。
    struct MimoInput
    {
        QString table, column;
    };
    QVector<MimoInput> ins;
    ins.reserve(inputs.size());
    for (const QVariant &c : inputs)
    {
        MimoInput in;
        const QVariantMap m = c.toMap();
        if (!m.isEmpty())
        {
            in.table = m.value(QStringLiteral("table")).toString();
            in.column = m.value(QStringLiteral("column")).toString();
        }
        else
        {
            in.table = topTable;
            in.column = c.toString();
        }
        if (in.table.isEmpty() || in.column.isEmpty())
            return fail(QStringLiteral("eargs"),
                        QStringLiteral("inputs 元素需为列名或 {table,column} 对象: %1")
                            .arg(c.toString()));
        ins.append(in);
    }

    // 读四列（各自可来自不同表）。派生读上限放宽到 200k（同 data.state 的
    // kStateMaxPoints）：真实日志远超默认 4096，等步长抽稀时间/值成对取、行对齐保持。
    constexpr int kDeriveMaxPoints = 200000;
    QVector<ChannelView> cols;
    for (const MimoInput &in : ins)
    {
        auto ch = ds->channel(in.table, in.column, kDeriveMaxPoints);
        if (!ch)
            return fail(QStringLiteral("echan"),
                        QStringLiteral("读取通道失败: %1.%2").arg(in.table, in.column));
        cols.append(std::move(*ch));
    }

    // 基准时间轴 = 第一个输入的轴；防御性校验非降序（kernel 表轴有序，这里是防线）。
    const QVector<double> baseT = cols[0].time;
    for (int i = 1; i < baseT.size(); ++i)
    {
        if (baseT.at(i) < baseT.at(i - 1))
            return fail(QStringLiteral("etime"),
                        QStringLiteral("基准时间轴非单调: %1.%2")
                            .arg(ins[0].table, ins[0].column));
    }
    const int n = baseT.size();
    if (n <= 0)
        return fail(QStringLiteral("eempty"), QStringLiteral("四元数通道无样本"));

    // 跨表输入重采样到基准轴。同表输入也统一走这里：门面 readWindow 会把 null/NaN
    // 样本成对剔除（绘图语义），同表各列读出点数可能不同——行对齐不能假设，
    // 必须显式对齐。基准轴有序 → 源列游标单调推进，O(n)。
    const QString interpMode =
        intent.params.value(QStringLiteral("interp"), QStringLiteral("linear")).toString();
    if (interpMode != QLatin1String("linear") && interpMode != QLatin1String("step")
        && interpMode != QLatin1String("nearest"))
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("interp 仅支持 linear/step/nearest: %1").arg(interpMode));
    for (int k = 1; k < 4; ++k)
    {
        const QVector<double> &st = cols[k].time;
        const QVector<double> &sv = cols[k].value;
        if (st.isEmpty())
            return fail(QStringLiteral("eempty"),
                        QStringLiteral("输入无样本: %1.%2").arg(ins[k].table, ins[k].column));
        if (cols[k].table == cols[0].table && cols[k].value.size() == n
            && std::equal(st.cbegin(), st.cend(), baseT.cbegin()))
            continue; // 快路径：轴完全一致（无 null 剔除）→ 已行对齐
        QVector<double> out;
        out.reserve(n);
        int j = 0; // 源列游标
        for (int i = 0; i < n; ++i)
        {
            const double t = baseT.at(i);
            if (t <= st.first()) // 范围外：边界延拓取端点（不产 NaN）
            {
                out.append(sv.first());
                continue;
            }
            if (t >= st.last())
            {
                out.append(sv.last());
                continue;
            }
            while (j + 1 < st.size() - 1 && st.at(j + 1) <= t)
                ++j; // 保持 st[j] <= t < st[j+1]
            if (interpMode == QLatin1String("step"))
            {
                out.append(sv.at(j));
                continue;
            }
            const double t0 = st.at(j), t1 = st.at(j + 1);
            if (interpMode == QLatin1String("nearest"))
                out.append(t - t0 <= t1 - t ? sv.at(j) : sv.at(j + 1));
            else if (t1 > t0) // 线性插值（重复时间戳防御：退化取左样本）
                out.append(sv.at(j) * (1.0 - (t - t0) / (t1 - t0))
                           + sv.at(j + 1) * ((t - t0) / (t1 - t0)));
            else
                out.append(sv.at(j));
        }
        cols[k].value = out;
    }

    // NaN 防线：null 样本经 kernel 读出即 NaN，quat 归一化会把它传播成垃圾角度——
    // 拒绝派生并指明位置，绝不静默产出。
    for (int k = 0; k < 4; ++k)
    {
        for (int i = 0; i < cols[k].value.size(); ++i)
        {
            if (std::isnan(cols[k].value.at(i)))
                return fail(QStringLiteral("enan"),
                            QStringLiteral("输入含 NaN/null 样本，拒绝派生: %1.%2 @[%3]")
                                .arg(ins[k].table, ins[k].column)
                                .arg(i));
        }
    }

    const bool degrees = intent.params.value(QStringLiteral("degrees"), false).toBool();
    const bool unwrap = intent.params.value(QStringLiteral("unwrap"), true).toBool();

    QVector<double> r(n), p(n), yaw(n);
    char errBuf[512] = {0};
    const int rc = ps_quat_to_rpy(static_cast<uint32_t>(n),
                                  cols[0].value.constData(), cols[1].value.constData(),
                                  cols[2].value.constData(), cols[3].value.constData(),
                                  degrees ? 1 : 0, unwrap ? 1 : 0,
                                  r.data(), p.data(), yaw.data(),
                                  errBuf, sizeof(errBuf));
    if (rc != 0)
        return fail(QStringLiteral("ederive"),
                    QStringLiteral("quat_to_rpy 失败: %1").arg(QString::fromUtf8(errBuf)));

    // 输出名：缺省 "<w列>.roll/.pitch/.yaw"（w 列取其列名，跨表对象输入同样适用）。
    const QString prefix = intent.params.value(QStringLiteral("name")).toString();
    QStringList outNames = intent.params.value(QStringLiteral("outputs")).toStringList();
    if (outNames.size() != 3)
    {
        const QString base = prefix.isEmpty() ? ins.first().column : prefix;
        outNames = { base + QStringLiteral(".roll"), base + QStringLiteral(".pitch"),
                     base + QStringLiteral(".yaw") };
    }

    QVector<double> time;
    time.reserve(n);
    for (int i = 0; i < n; ++i)
        time.append(cols[0].time.at(i));

    const QVector<QVector<double>> vals = { r, p, yaw };
    QVariantList outPayload;
    const QString unit = degrees ? QStringLiteral("deg") : QStringLiteral("rad");
    for (int k = 0; k < 3; ++k)
    {
        // 登记（独立派生字段）+ 画布事件。
        nextpilot::plotscope::context::DerivedSeriesRegistry::Entry entry;
        entry.name = outNames.at(k);
        entry.datasetId = dsId;
        entry.table = ins.first().table;
        entry.column = ins.first().column;
        entry.filterId = QStringLiteral("quat_to_rpy");
        entry.t = time;
        entry.v = vals[k];
        m_app.derivedRegistry().registerSeries(entry);
        Q_EMIT m_app.derivedSeriesReady(outNames.at(k), time, vals[k]);

        QVariantList vv;
        vv.reserve(n);
        for (int i = 0; i < n; ++i)
            vv << vals[k].at(i);
        QVariantList tt;
        tt.reserve(n);
        for (int i = 0; i < n; ++i)
            tt << time.at(i);
        outPayload.append(QVariantMap{
            {QStringLiteral("name"), outNames.at(k)},
            {QStringLiteral("unit"), unit},
            {QStringLiteral("count"), n},
            {QStringLiteral("time"), QVariant::fromValue(tt)},
            {QStringLiteral("value"), QVariant::fromValue(vv)},
        });
    }

    IntentResult res;
    res.payload = QVariantMap{
        {QStringLiteral("dataset"), dsId},
        {QStringLiteral("table"), ins.first().table},
        {QStringLiteral("filter"), QStringLiteral("quat_to_rpy")},
        {QStringLiteral("outputs"), outPayload},
        {QStringLiteral("count"), n},
    };
    return res;
}

// live 源暂停/继续（#193）：
//   data.pause  → SourceState::Paused   （停止向数据集推送新样本）
//   data.resume → SourceState::Streaming（恢复推送）
// 边界：仅 SourceKind::Live 源可操作——文件/回放源没有"实时"语义，明确拒绝；
// 幂等：已处于目标状态时直接成功（重复点击/脚本重试不报错）。
// 状态变化经 SourceRegistry::setState 触发 sourceChanged 信号，面板与视图自行刷新。
Result<IntentResult> DataCommandsHandler::setSourcePaused(const Intent &intent, bool paused)
{
    int sourceId = intent.params.value(QStringLiteral("id"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    const DataSourceInfo *info = m_app.sources().get(sourceId);
    if (!info)
        return fail(QStringLiteral("enosource"), QStringLiteral("没有可操作的数据源"));
    if (info->kind != SourceKind::Live)
        return fail(QStringLiteral("enotsupported"),
                    QStringLiteral("仅实时(live)源支持暂停/继续，当前源不是实时源"));

    const SourceState target = paused ? SourceState::Paused : SourceState::Streaming;
    if (info->state != target)
    {
        if (!m_app.sources().setState(sourceId, target))
            return fail(QStringLiteral("estate"),
                        QStringLiteral("数据源状态切换失败（当前状态 %1）").arg(int(info->state)));
    }

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("id"), sourceId},
        {QStringLiteral("paused"), paused},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::derive(const Intent &intent)
{
    // MIMO 分支：inputs 数组 + quat_to_rpy（多输入多输出，如 q0-q3 → roll/pitch/yaw）。
    // 派生语义不变：结果登记派生注册表，原始 dataset 零改动。
    if (intent.params.contains(QStringLiteral("inputs")))
        return deriveMimoQuat(intent);

    const DatasetId dsId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    const QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    const QString filterId = intent.params.value(QStringLiteral("filter")).toString();
    if (dsId < 0 || table.isEmpty() || column.isEmpty() || filterId.isEmpty())
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("data.derive 需要 dataset/table/column/filter 参数"));

    // const 取数：派生只读，不碰仓库里的 Dataset——原数据零改动。
    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    const Table *t = ds->table(table);
    if (!t || !t->column(column))
        return fail(QStringLiteral("echan"), QStringLiteral("通道不存在: %1").arg(input));

    // params 允许调用方给 JSON 字符串或 QVariantMap（脚本/Job 场景）。
    QByteArray paramsJson;
    const QVariant pv = intent.params.value(QStringLiteral("params"));
    if (pv.type() == QVariant::String)
        paramsJson = pv.toString().toUtf8();
    else if (pv.isValid() && !pv.toMap().isEmpty())
        paramsJson = QJsonDocument(QJsonObject::fromVariantMap(pv.toMap())).toJson(QJsonDocument::Compact);

    // Rust 实时算，结果有界（默认上限见下）；数据集本身不变。
    constexpr int kMaxDerivedPoints = 200000; // 画布/脚本够用，防全量派生灌爆 UI 内存
    int cap = intent.params.value(QStringLiteral("cap"), kMaxDerivedPoints).toInt();
    if (cap <= 0 || cap > kMaxDerivedPoints)
        cap = kMaxDerivedPoints;

    QString name = intent.params.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = input + QLatin1Char('.') + filterId;

    // 统一变换（算子/表达式/Lua）：params 里带 transform 请求时走 TransformEvaluator。
    // 能力位 CapDerived——选错（如 checksum专用）在 evaluator 内被挡住。
    // 未指定则兼容原 previewFilter 路径（内置算子直接走 Rust，零额外开销）。
    QVariant transformVar = intent.params.value(QStringLiteral("transform"));
    if (transformVar.isValid())
    {
        const QJsonObject req = transformVar.type() == QVariant::String
            ? QJsonDocument::fromJson(transformVar.toString().toUtf8()).object()
            : QJsonObject::fromVariantMap(transformVar.toMap());

        // 取原始通道（cap 有界），再交给统一变换器。
        QVector<double> rawT, rawV;
        const Table *srcTable = ds->table(table);
        if (!srcTable)
            return fail(QStringLiteral("etable"), QStringLiteral("表不存在: %1").arg(table));
        auto raw = ds->channel(table, column);
        if (!raw)
            return fail(QStringLiteral("echan"), QStringLiteral("读取通道失败: %1").arg(input));
        const int take = qMin(raw->size(), cap);
        rawT.reserve(take);
        rawV.reserve(take);
        for (int i = 0; i < take; ++i)
        {
            rawT.append(raw->time.value(i));
            rawV.append(raw->value.value(i));
        }

        if (!m_app.interactions().hasTransformEvaluator())
            return fail(QStringLiteral("enoeval"), QStringLiteral("变换求值器未注册（script 模块未装载）"));
        Result<QVector<double>> ev{QVector<double>()};
        m_app.interactions().evalTransform(req, CapDerived, rawT, rawV, ev);
        if (!ev)
            return fail(ev.error().code, ev.error().message);

        QVector<double> time = rawT;
        QVector<double> value = ev.value();
        const int n = value.size();
        if (n <= 0)
            return fail(QStringLiteral("ederive"), QStringLiteral("变换结果为空"));
        IntentResult r;
        r.payload = QVariantMap{
            {QStringLiteral("dataset"), dsId},
            {QStringLiteral("table"), table},
            {QStringLiteral("column"), column},
            {QStringLiteral("transform"), req},
            {QStringLiteral("name"), name},
            {QStringLiteral("count"), n},
            {QStringLiteral("total"), n},
            {QStringLiteral("truncated"), false},
            {QStringLiteral("time"), QVariant::fromValue(time)},
            {QStringLiteral("value"), QVariant::fromValue(value)},
        };
        // 登记派生序列（独立派生字段：不写回 dataset，全后端按 name 可读）。
        nextpilot::plotscope::context::DerivedSeriesRegistry::Entry entry;
        entry.name = name;
        entry.datasetId = dsId;
        entry.table = table;
        entry.column = column;
        entry.filterId = QStringLiteral("@transform");
        entry.transform = req;
        entry.t = time;
        entry.v = value;
        m_app.derivedRegistry().registerSeries(entry);
        Q_EMIT m_app.derivedSeriesReady(name, time, value);
        return r;
    }

    QVector<double> time, value;
    int total = 0;
    QString err;
    const int n = ds->previewFilter(input, filterId, paramsJson, cap, time, value, &total, &err);
    if (n <= 0)
        return fail(QStringLiteral("ederive"), err.isEmpty() ? QStringLiteral("派生失败") : err);

    // payload 给脚本/CLI：time/value 数组 + 元信息（可序列化为 Job 结果）。
    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("dataset"), dsId},
        {QStringLiteral("table"), table},
        {QStringLiteral("column"), column},
        {QStringLiteral("filter"), filterId},
        {QStringLiteral("name"), name},
        {QStringLiteral("count"), n},
        {QStringLiteral("total"), total},
        {QStringLiteral("truncated"), total > n},
        {QStringLiteral("time"), QVariant::fromValue(time)},
        {QStringLiteral("value"), QVariant::fromValue(value)},
    };
    // 登记派生序列（独立派生字段：不写回 dataset，全后端按 name 可读）。
    {
        nextpilot::plotscope::context::DerivedSeriesRegistry::Entry entry;
        entry.name = name;
        entry.datasetId = dsId;
        entry.table = table;
        entry.column = column;
        entry.filterId = filterId;
        entry.paramsJson = QJsonDocument::fromJson(paramsJson).object();
        entry.t = time;
        entry.v = value;
        m_app.derivedRegistry().registerSeries(entry);
    }
    // GUI 画布订阅此事件，把派生序列作为一条 generated curve 叠加显示。
    // 数据集零改动——老代码改 DataSource 时间基准那套副作用，这里彻底没有。
    Q_EMIT m_app.derivedSeriesReady(name, time, value);
    return r;
}

Result<IntentResult> DataCommandsHandler::filterSeries(const Intent &intent)
{
    const DatasetId dsId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    const QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    const QString filterId = intent.params.value(QStringLiteral("filter")).toString();
    if (dsId < 0 || table.isEmpty() || column.isEmpty() || filterId.isEmpty())
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("data.filter 需要 dataset/table/column/filter 参数"));

    if (!m_app.interactions().hasFilterOps())
        return fail(QStringLiteral("enofilterops"), QStringLiteral("过滤器能力未注册（script 模块未装载）"));

    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    auto channel = ds->channel(table, column);
    if (!channel)
        return fail(QStringLiteral("echan"), QStringLiteral("通道不存在: %1").arg(input));

    // params 允许 JSON 字符串或 QVariantMap（脚本/CLI 与 GUI 两种来路）。
    QJsonObject paramsObj;
    const QVariant pv = intent.params.value(QStringLiteral("params"));
    if (pv.type() == QVariant::String)
        paramsObj = QJsonDocument::fromJson(pv.toString().toUtf8()).object();
    else if (pv.isValid() && !pv.toMap().isEmpty())
        paramsObj = QJsonObject::fromVariantMap(pv.toMap());

    constexpr int kMaxPoints = 200000;
    int cap = intent.params.value(QStringLiteral("cap"), kMaxPoints).toInt();
    if (cap <= 0 || cap > kMaxPoints)
        cap = kMaxPoints;
    const int take = qMin(channel->size(), cap);
    QVector<double> t, v;
    t.reserve(take);
    v.reserve(take);
    for (int i = 0; i < take; ++i)
    {
        t.append(channel->time.value(i));
        v.append(channel->value.value(i));
    }

    Result<QJsonObject> res = m_app.interactions().filterOps().applySeries(filterId, paramsObj, t, v);
    if (!res)
        return fail(res.error().code, res.error().message);

    QVector<double> outT, outV;
    for (const QJsonValue &jv : res.value().value(QStringLiteral("time")).toArray())
        outT.append(jv.toDouble());
    for (const QJsonValue &jv : res.value().value(QStringLiteral("value")).toArray())
        outV.append(jv.toDouble());
    if (outT.isEmpty() || outT.size() != outV.size())
        return fail(QStringLiteral("efilter"), QStringLiteral("过滤器 %1 结果为空或长度不一致").arg(filterId));

    QString name = intent.params.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = input + QLatin1Char('.') + filterId;

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("dataset"), dsId},
        {QStringLiteral("table"), table},
        {QStringLiteral("column"), column},
        {QStringLiteral("filter"), filterId},
        {QStringLiteral("params"), QVariant(paramsObj)},
        {QStringLiteral("name"), name},
        {QStringLiteral("count"), outT.size()},
        {QStringLiteral("total"), take},
        {QStringLiteral("truncated"), take < channel->size()},
        {QStringLiteral("time"), QVariant::fromValue(outT)},
        {QStringLiteral("value"), QVariant::fromValue(outV)},
    };
    Q_EMIT m_app.derivedSeriesReady(name, outT, outV);
    return r;
}

Result<IntentResult> DataCommandsHandler::runMarker(const Intent &intent)
{
    const DatasetId dsId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    const QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    QByteArray code = intent.params.value(QStringLiteral("code")).toByteArray();
    if (code.isEmpty())
        code = intent.params.value(QStringLiteral("source")).toByteArray(); // 脚本/CLI 别名
    if (dsId < 0 || table.isEmpty() || column.isEmpty() || code.isEmpty())
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("data.marker 需要 dataset/table/column/code 参数"));

    if (!m_app.interactions().hasMarkerOps())
        return fail(QStringLiteral("enomarkerops"), QStringLiteral("Marker 规则能力未注册（script 模块未装载）"));

    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    auto channel = ds->channel(table, column);
    if (!channel)
        return fail(QStringLiteral("echan"), QStringLiteral("通道不存在: %1").arg(input));

    constexpr int kMaxPoints = 200000;
    int cap = intent.params.value(QStringLiteral("cap"), kMaxPoints).toInt();
    if (cap <= 0 || cap > kMaxPoints)
        cap = kMaxPoints;
    const int take = qMin(channel->size(), cap);
    QJsonArray timeArr, valueArr;
    for (int i = 0; i < take; ++i)
    {
        timeArr.append(channel->time.value(i));
        valueArr.append(channel->value.value(i));
    }
    QJsonObject seriesObj;
    seriesObj.insert(QStringLiteral("name"), column);
    seriesObj.insert(QStringLiteral("time"), timeArr);
    seriesObj.insert(QStringLiteral("value"), valueArr);
    QJsonArray seriesJson;
    seriesJson.append(seriesObj);

    Result<QJsonArray> res = m_app.interactions().markerOps().run(code, seriesJson);
    if (!res)
        return fail(res.error().code, res.error().message);
    const QJsonArray markers = res.value();

    // 画到活动画布（经 view action，host 不依赖 GUI 类型）。
    QVariantMap draw;
    draw.insert(QStringLiteral("markers"), markers.toVariantList());
    Result<QVariant> viewOut{QVariant()};
    m_app.interactions().dispatchViewAction(QStringLiteral("plot.markers.set"), draw, viewOut);

    QString name = intent.params.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = input + QStringLiteral(".markers");
    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("dataset"), dsId},
        {QStringLiteral("table"), table},
        {QStringLiteral("column"), column},
        {QStringLiteral("name"), name},
        {QStringLiteral("count"), markers.size()},
        {QStringLiteral("points"), take},
        {QStringLiteral("markers"), markers.toVariantList()},
        {QStringLiteral("drawn"), viewOut.ok()},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::series(const Intent &intent)
{
    // 通道定位：dataset/table/column，dataset/table 可缺省（唯一数据集/唯一表兜底），
    // 与 data.export 同一套口径。这是脚本/CLI/MCP 读数据的标准出口——VBA 能力的
    // 数据基石：脚本拿到 t/v 数组就能做统计、判断、条件导出。
    int datasetId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    if (datasetId < 0 && m_app.data().count() == 1)
        datasetId = m_app.data().ids().first();
    if (datasetId < 0)
        return fail(QStringLiteral("edataset"),
                    QStringLiteral("请指定 dataset 参数（当前有多个数据集）"));
    const Dataset *ds = m_app.data().get(datasetId);
    if (!ds)
        return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(datasetId));

    QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    if (table.isEmpty() && ds->tableCount() == 1)
        table = ds->tableNames().first();
    if (column.isEmpty())
        return fail(QStringLiteral("eargs"), QStringLiteral("data.series 需要 column 参数"));

    // 派生名穿透（独立派生字段）：column 命中派生注册表 → 直接返回缓存结果。
    // 数据集需匹配（防跨数据集撞名）；datasetId 缺省由第一个数据集兜底的
    // 场景下，若派生属于其他数据集则视为不命中（不穿帮）。
    {
        const auto *entry = m_app.derivedRegistry().get(column);
        if (entry && entry->datasetId == datasetId)
        {
            const int n = qMin(entry->t.size(), entry->v.size());
            if (n <= 0)
                return fail(QStringLiteral("eempty"),
                            QStringLiteral("派生序列无样本: %1").arg(column));
            QVariantList ts, vs;
            ts.reserve(n);
            vs.reserve(n);
            for (int i = 0; i < n; ++i)
            {
                ts << entry->t.at(i);
                vs << entry->v.at(i);
            }
            IntentResult r;
            r.payload = QVariantMap{
                {QStringLiteral("dataset"), entry->datasetId},
                {QStringLiteral("table"), entry->table},
                {QStringLiteral("column"), column},
                {QStringLiteral("derived"), true},
                {QStringLiteral("filter"), entry->filterId},
                {QStringLiteral("count"), n},
                {QStringLiteral("t"), ts},
                {QStringLiteral("v"), vs},
            };
            return r;
        }
    }

    const Table *t = ds->table(table);
    if (!t || !t->column(column))
        return fail(QStringLiteral("echan"),
                    QStringLiteral("通道不存在: %1.%2").arg(table, column));

    // cap 默认 4096（有界读取口径，与 plot.render 无 px 路径一致），可调但不超过
    // 内核不抽稀上限（对齐 kStateMaxPoints 的量级，防止一次意图拖走整列百万行）。
    int limit = intent.params.value(QStringLiteral("limit"), 4096).toInt();
    limit = qBound(1, limit, 200000);

    auto channel = ds->channel(table, column, limit);
    if (!channel)
        return fail(QStringLiteral("echan"),
                    QStringLiteral("读取通道失败: %1.%2").arg(table, column));
    const int n = channel->size();
    if (n <= 0)
        return fail(QStringLiteral("eempty"), QStringLiteral("通道无样本"));

    QVariantList ts, vs;
    ts.reserve(n);
    vs.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        ts << channel->time.value(i);
        vs << channel->value.value(i);
    }

    // dataset 级通道变换（变换 = 非破坏视图）：无显式 transform 参数时查
    // data.transform.set 的持久化配方，命中则统一变换后再返回——读数后端
    // （脚本/CLI/MCP）与画布看到同一份变换后数据，字段名不变，原始数据不动。
    bool transformed = false;
    QJsonObject tvReq;
    if (intent.params.contains(QStringLiteral("transform")))
    {
        const QVariant tv = intent.params.value(QStringLiteral("transform"));
        tvReq = tv.type() == QVariant::String
            ? QJsonDocument::fromJson(tv.toString().toUtf8()).object()
            : QJsonObject::fromVariantMap(tv.toMap());
    }
    else
    {
        const int srcId = m_app.sources().sourceOfDataset(datasetId);
        if (srcId >= 0)
        {
            if (const DataSourceInfo *info = m_app.sources().get(srcId))
            {
                const ChannelTransformConfig cfg =
                    ChannelTransformConfig::load(m_app.settings());
                if (const ChannelTransformSpec *spec =
                        cfg.find(info->origin, table, column))
                    tvReq = spec->transform;
            }
        }
    }
    if (!tvReq.isEmpty() && tvReq.contains(QStringLiteral("descriptorId")))
    {
        if (!m_app.interactions().hasTransformEvaluator())
            return fail(QStringLiteral("enoeval"),
                        QStringLiteral("变换求值器未注册（script 模块未装载）"));
        QVector<double> wt, wv;
        wt.reserve(n);
        wv.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            wt.append(channel->time.value(i));
            wv.append(channel->value.value(i));
        }
        Result<QVector<double>> ev{QVector<double>()};
        m_app.interactions().evalTransform(tvReq, CapDerived, wt, wv, ev);
        if (!ev)
            return fail(ev.error().code, ev.error().message);
        const int m = qMin(ev.value().size(), n);
        vs.clear();
        vs.reserve(m);
        for (int i = 0; i < m; ++i)
            vs << ev.value().at(i);
        ts.resize(m);
        transformed = true;
    }

    QVariantMap payload;
    payload[QStringLiteral("dataset")] = datasetId;
    payload[QStringLiteral("table")] = table;
    payload[QStringLiteral("column")] = column;
    payload[QStringLiteral("count")] = ts.size();
    payload[QStringLiteral("t")] = ts;
    payload[QStringLiteral("v")] = vs;
    payload[QStringLiteral("transformed")] = transformed;
    // 总行数大于 limit 即截断（抽稀后点数可能 < limit，不能拿 n==limit 判断）。
    payload[QStringLiteral("truncated")] = t->rowCount() > limit;
    payload[QStringLiteral("rows")] = t->rowCount();
    IntentResult r;
    r.payload = payload;
    return r;
}

Result<IntentResult> DataCommandsHandler::stateSeries(const Intent &intent)
{
    const DatasetId dsId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    const QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    if (dsId < 0 || table.isEmpty() || column.isEmpty())
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("data.state 需要 dataset/table/column 参数"));

    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(QStringLiteral("enoent"), QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    const Table *t = ds->table(table);
    const Column *col = t ? t->column(column) : nullptr;
    if (!t || !col)
        return fail(QStringLiteral("echan"), QStringLiteral("通道不存在: %1").arg(input));
    // 状态视图只接受离散值族：bool / 整型 / 仅含整数值的浮点（对齐 PJ4 的
    // PrimitiveType 离散家族 + whole_numbers_only 约束）。Text 通道的 string
    // 状态读取依赖 kernel string ABI（暂缺），此处明确报错而非静默空白。
    if (col->type == ValueType::Text)
        return fail(QStringLiteral("etype"),
                    QStringLiteral("string 状态读取依赖内核 string ABI（暂未实现）: %1").arg(input));

    auto channel = ds->channel(table, column, kStateMaxPoints);
    if (!channel)
        return fail(QStringLiteral("echan"), QStringLiteral("读取通道失败: %1").arg(input));
    const int n = channel->size();
    if (n <= 0)
        return fail(QStringLiteral("eempty"), QStringLiteral("通道无样本: %1").arg(input));

    // 标签格式化（对齐 StateSeriesAdapter::formatStateLabel）：
    //   bool → "true"/"false"；整型 → 十进制串；浮点 → 整数值用整数串。
    // 浮点列对齐 PJ4 ScalarFieldPayload::whole_numbers_only 的入口过滤语义：
    // 只要含一个非整数值就整列拒绝（避免逐样本断段产生碎片化误导展示）。
    // 注意取数用大 cap（不抽稀）：RLE 分段和整值探测都必须基于原始样本。
    const bool isBool = col->type == ValueType::Bool;
    const bool isFloat = col->type == ValueType::F64 || col->type == ValueType::F32;
    if (isFloat)
    {
        for (int i = 0; i < channel->size(); ++i)
        {
            const double v = channel->value.value(i);
            if (!std::isfinite(v) || v != std::floor(v)
                || v < -9.007199254740992e15 || v > 9.007199254740992e15)
                return fail(QStringLiteral("estate"),
                            QStringLiteral("浮点列含非整数值，不构成离散状态: %1").arg(input));
        }
    }
    auto formatLabel = [&](double v, bool *ok) -> QString {
        *ok = true;
        if (isBool)
            return v != 0.0 ? QStringLiteral("true") : QStringLiteral("false");
        if (isFloat)
            return QString::number(static_cast<qlonglong>(v));
        return QString::number(static_cast<qlonglong>(v));
    };

    // 游程编码（对齐 StateSeriesAdapter::rebuild）：
    //   相同值延续开放段；值变化时前段闭合于新样本时间戳；null 关闭开放段产生 gap；
    //   尾段开放，钳到最后样本时间（= 显示范围可用的最大值）。
    constexpr int kMaxPoints = 200000;
    const int take = qMin(n, kMaxPoints);
    struct RawSeg { double t0; double t1; QString label; };
    QVector<RawSeg> segs;
    int pendingIdx = -1; // 未决样本下标（-1 无）
    bool pendingNull = false;
    QString pendingLabel;
    auto flushPending = [&]()
    {
        if (pendingIdx < 0)
            return;
        const double ts = channel->time.value(pendingIdx);
        const bool open = !segs.isEmpty() && segs.last().t1 < 0.0; // t1<0 = 开放
        if (pendingNull)
        {
            if (open)
                segs.last().t1 = ts; // null 关闭开放段，随后是 gap
        }
        else if (open && segs.last().label == pendingLabel)
        {
            // 同值：游程延续
        }
        else
        {
            if (open)
                segs.last().t1 = ts;
            segs.append({ts, -1.0, pendingLabel});
        }
        pendingIdx = -1;
    };
    for (int i = 0; i < take; ++i)
    {
        bool ok = true;
        const QString label = formatLabel(channel->value.value(i), &ok);
        // 同时间戳折叠到最后一个（对齐 PJ4 collapse equal-timestamp samples）。
        if (pendingIdx >= 0 && channel->time.value(pendingIdx) == channel->time.value(i))
        {
            pendingIdx = i;
            pendingNull = !ok;
            pendingLabel = label;
            continue;
        }
        flushPending();
        pendingIdx = i;
        pendingNull = !ok;
        pendingLabel = label;
    }
    flushPending();
    // 尾段开放（t1<0）钳到最后样本时间。
    if (!segs.isEmpty() && segs.last().t1 < 0.0)
        segs.last().t1 = channel->time.value(take - 1);
    if (segs.isEmpty())
        return fail(QStringLiteral("estate"),
                    QStringLiteral("通道无有效状态段（浮点列需只含整数值）: %1").arg(input));

    // 组行 JSON → 画布（经 view action，host 不依赖 GUI 类型）。
    QJsonArray segArr;
    for (const RawSeg &s : segs)
        segArr.append(QJsonObject{
            {QStringLiteral("tStart"), s.t0},
            {QStringLiteral("tEnd"), s.t1},
            {QStringLiteral("label"), s.label},
        });
    QJsonArray rowsArr;
    rowsArr.append(QJsonObject{
        {QStringLiteral("channel"), input},
        {QStringLiteral("segments"), segArr},
    });
    QVariantMap draw;
    draw.insert(QStringLiteral("rows"), rowsArr.toVariantList());
    // append 模式（多通道多行叠加）：调用方对每个通道各发一次 data.state(append)。
    if (intent.params.value(QStringLiteral("append")).toBool())
        draw.insert(QStringLiteral("mode"), QStringLiteral("append"));
    Result<QVariant> viewOut{QVariant()};
    m_app.interactions().dispatchViewAction(QStringLiteral("plot.state.set"), draw, viewOut);

    QString name = intent.params.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = input + QStringLiteral(".state");
    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("dataset"), dsId},
        {QStringLiteral("table"), table},
        {QStringLiteral("column"), column},
        {QStringLiteral("name"), name},
        {QStringLiteral("segments"), segs.size()},
        {QStringLiteral("points"), take},
        {QStringLiteral("rows"), rowsArr.toVariantList()},
        {QStringLiteral("drawn"), viewOut.ok()},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::workspaceList()
{
    const WorkspaceDataConfig cfg = WorkspaceDataConfig::load(m_app.settings());
    IntentResult r;
    // 用 JSON 数组回传：CLI 直接可读，GUI 据此展示配置清单。
    QJsonArray arr;
    for (const SourceSpec &s : cfg.sources)
        arr.append(s.toJson());
    r.payload = QVariant::fromValue(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return r;
}

Result<IntentResult> DataCommandsHandler::workspaceSave()
{
    // 配置在 data.load 成功时已增量写入 settings；这里只是显式 flush 到磁盘。
    QString err;
    if (!m_app.settings().flush(&err))
        return fail(QStringLiteral("eio"), err);
    IntentResult r;
    r.payload = true;
    return r;
}

Result<IntentResult> DataCommandsHandler::workspaceRestore()
{
    // 恢复 = 重放：对每个源发 data.load（record=false，避免重复记录），
    // 再对每条派生配方发 data.derive（Rust 重算，不写回数据）。
    const WorkspaceDataConfig cfg = WorkspaceDataConfig::load(m_app.settings());
    int loadedSources = 0;
    int replayedDerives = 0;

    for (const SourceSpec &s : cfg.sources)
    {
        if (!s.isValid())
            continue;

        Intent load;
        load.op = QStringLiteral("data.load");
        load.params[QStringLiteral("path")] = s.origin;   // 文件/端点
        load.params[QStringLiteral("record")] = false;     // 恢复不当成新打开
        // 解析器：显式给定则带上（多解析器扇出）。
        if (!s.parserIds.isEmpty())
            load.params[QStringLiteral("parsers")] = QVariant::fromValue(s.parserIds);
        // 解析参数：按 parserId 分组存的申请单，重放时原样透传给 data.load。
        if (!s.parserIds.isEmpty() && s.parserParams.contains(s.parserIds.first()))
        {
            const QJsonObject params = s.parserParams.value(s.parserIds.first()).toObject();
            load.params[QStringLiteral("options")] =
                QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
        }
        const auto lr = m_executor.execute(load);
        if (!lr)
            continue; // 单个源失败不阻断其余（文件可能已移动/删除）
        ++loadedSources;

        // 反查刚载入的源 → 主数据集 id，供派生配方定位。
        const int sourceId = m_app.sources().sourceByOrigin(s.origin);
        if (sourceId < 0)
            continue;
        const DataSourceInfo *info = m_app.sources().get(sourceId);
        if (!info || info->datasetIds.isEmpty())
            continue;
        const int datasetId = info->datasetIds.first();

        // 重放派生配方：origin 匹配当前源（或留空表示默认作用该源）。
        for (const DeriveRecipeSpec &recipe : s.deriveRecipes)
        {
            if (!recipe.isValid())
                continue;
            if (!recipe.sourceOrigin.isEmpty() && recipe.sourceOrigin != s.origin)
                continue;
            Intent der;
            der.op = QStringLiteral("data.derive");
            der.params[QStringLiteral("dataset")] = datasetId;
            der.params[QStringLiteral("table")] = recipe.table;
            der.params[QStringLiteral("column")] = recipe.column;
            der.params[QStringLiteral("filter")] = recipe.filter;
            der.params[QStringLiteral("params")] = recipe.paramsJson;
            if (!recipe.name.isEmpty())
                der.params[QStringLiteral("name")] = recipe.name;
            const auto dr = m_executor.execute(der);
            if (dr)
                ++replayedDerives;
        }
    }

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("sources"), loadedSources},
        {QStringLiteral("derives"), replayedDerives},
    };
    return r;
}

// ===== dataset 级通道变换配方（变换 = 非破坏视图，全后端生效）=====
//
//   data.transform.set    {dataset|origin, table, column, transform(请求 JSON/string)}
//   data.transform.clear  {dataset|origin, table, column}
//
// 定位键持久化用 sourceOrigin（datasetId 会话间会变）；plot.channel / data.series
// 读数时统一消费，原始 dataset 零改动。
Result<IntentResult> DataCommandsHandler::setChannelTransform(const Intent &intent)
{
    QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    if (table.isEmpty() || column.isEmpty())
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("data.transform.set 需要 table/column 参数"));

    // origin：显式给优先（恢复重放场景），否则 dataset id 反查源。
    QString origin = intent.params.value(QStringLiteral("origin")).toString();
    int datasetId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    if (datasetId < 0 && origin.isEmpty() && m_app.data().count() == 1)
        datasetId = m_app.data().ids().first();
    if (origin.isEmpty())
    {
        const int srcId = datasetId >= 0
            ? datasetId
            : m_app.activeSourceId();
        const int realSrc = m_app.sources().sourceOfDataset(datasetId);
        const int lookupId = realSrc >= 0 ? realSrc : srcId;
        const DataSourceInfo *info = m_app.sources().get(lookupId);
        if (!info)
            return fail(QStringLiteral("enosource"),
                        QStringLiteral("无法定位数据源（给 origin 或有效 dataset）"));
        origin = info->origin;
    }
    // datasetId 缺省时用 origin 找回当前 id（payload 回给调用方）。
    if (datasetId < 0)
    {
        const int srcId = m_app.sources().sourceByOrigin(origin);
        if (srcId >= 0)
        {
            if (const DataSourceInfo *srcInfo = m_app.sources().get(srcId);
                srcInfo && !srcInfo->datasetIds.isEmpty())
                datasetId = srcInfo->datasetIds.first();
        }
    }

    // transform 请求：JSON 字符串/字节数组（QJsonDocument::toJson 的产物）或对象。
    const QVariant tv = intent.params.value(QStringLiteral("transform"));
    QJsonObject req;
    if (tv.type() == QVariant::String || tv.type() == QVariant::ByteArray)
    {
        const QByteArray raw = tv.type() == QVariant::ByteArray
            ? tv.toByteArray()
            : tv.toString().toUtf8();
        QJsonParseError perr;
        req = QJsonDocument::fromJson(raw, &perr).object();
        if (req.isEmpty())
            return fail(QStringLiteral("eargs"),
                        QStringLiteral("transform JSON 解析失败: %1").arg(perr.errorString()));
    }
    else if (tv.isValid())
    {
        req = QJsonObject::fromVariantMap(tv.toMap());
    }
    if (!req.contains(QStringLiteral("descriptorId")))
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("transform 请求缺少 descriptorId"));

    ChannelTransformConfig cfg = ChannelTransformConfig::load(m_app.settings());
    ChannelTransformSpec spec;
    spec.origin = origin;
    spec.table = table;
    spec.column = column;
    spec.transform = req;
    cfg.upsert(spec);
    QString err;
    if (!cfg.save(m_app.settings(), &err))
        return fail(QStringLiteral("eio"), err);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("dataset"), datasetId},
        {QStringLiteral("origin"), origin},
        {QStringLiteral("table"), table},
        {QStringLiteral("column"), column},
        {QStringLiteral("descriptorId"), req.value(QStringLiteral("descriptorId")).toString()},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::clearChannelTransform(const Intent &intent)
{
    QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();
    if (table.isEmpty() || column.isEmpty())
        return fail(QStringLiteral("eargs"),
                    QStringLiteral("data.transform.clear 需要 table/column 参数"));

    QString origin = intent.params.value(QStringLiteral("origin")).toString();
    int datasetId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    if (datasetId < 0 && origin.isEmpty() && m_app.data().count() == 1)
        datasetId = m_app.data().ids().first();
    if (origin.isEmpty())
    {
        const int realSrc = m_app.sources().sourceOfDataset(datasetId);
        const DataSourceInfo *info = m_app.sources().get(realSrc >= 0 ? realSrc : m_app.activeSourceId());
        if (!info)
            return fail(QStringLiteral("enosource"),
                        QStringLiteral("无法定位数据源（给 origin 或有效 dataset）"));
        origin = info->origin;
    }

    ChannelTransformConfig cfg = ChannelTransformConfig::load(m_app.settings());
    const bool removed = cfg.removeByChannel(origin, table, column);
    QString err;
    if (!cfg.save(m_app.settings(), &err))
        return fail(QStringLiteral("eio"), err);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("removed"), removed},
        {QStringLiteral("origin"), origin},
        {QStringLiteral("table"), table},
        {QStringLiteral("column"), column},
    };
    return r;
}

// 派生序列目录（独立派生字段的可发现性）：数据树/脚本经此列出全部派生序列。
// 可选 dataset 过滤。
Result<IntentResult> DataCommandsHandler::deriveList(const Intent &intent)
{
    const int datasetFilter = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    const auto all = m_app.derivedRegistry().all();

    QVariantList list;
    for (const auto &e : all)
    {
        if (datasetFilter >= 0 && e.datasetId != datasetFilter)
            continue;
        list.append(QVariantMap{
            {QStringLiteral("name"), e.name},
            {QStringLiteral("dataset"), e.datasetId},
            {QStringLiteral("table"), e.table},
            {QStringLiteral("column"), e.column},
            {QStringLiteral("filter"), e.filterId},
            {QStringLiteral("points"), qMin(e.t.size(), e.v.size())},
        });
    }
    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("count"), list.size()},
        {QStringLiteral("series"), list},
    };
    return r;
}

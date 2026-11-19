#include "DataCommandsHandler.h"

#include "AppContext.h"
#include "DataSource.h"
#include "SourceRegistry.h"
#include "Dataset.h"
#include "IIntentExecutor.h"
#include "Result.h"
#include "SettingsStore.h"
#include "WorkspaceDataConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVector>

using namespace nextpilot::plotscope::core;

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
} // namespace

bool DataCommandsHandler::canHandle(const Intent &intent) const
{
    const QString &op = intent.op;
    return op == QLatin1String("data.reload") || op == QLatin1String("data.export")
        || op == QLatin1String("data.recent") || op == QLatin1String("data.clearCache")
        || op == QLatin1String("data.derive") || op == QLatin1String("data.workspace.list")
        || op == QLatin1String("data.workspace.save")
        || op == QLatin1String("data.workspace.restore");
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
    if (intent.op == QLatin1String("data.derive"))
        return derive(intent);
    if (intent.op == QLatin1String("data.workspace.list"))
        return workspaceList();
    if (intent.op == QLatin1String("data.workspace.save"))
        return workspaceSave();
    if (intent.op == QLatin1String("data.workspace.restore"))
        return workspaceRestore();
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
        m_app.data().remove(ds);

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

Result<IntentResult> DataCommandsHandler::derive(const Intent &intent)
{
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

        if (!m_app.hasTransformEvaluator())
            return fail(QStringLiteral("enoeval"), QStringLiteral("变换求值器未注册（script 模块未装载）"));
        Result<QVector<double>> ev{QVector<double>()};
        m_app.evalTransform(req, CapDerived, rawT, rawV, ev);
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
    // GUI 画布订阅此事件，把派生序列作为一条 generated curve 叠加显示。
    // 数据集零改动——老代码改 DataSource 时间基准那套副作用，这里彻底没有。
    Q_EMIT m_app.derivedSeriesReady(name, time, value);
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

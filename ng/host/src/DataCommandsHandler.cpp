#include "DataCommandsHandler.h"

#include "AppContext.h"
#include "DataSource.h"
#include "DataSourceRepository.h"
#include "Dataset.h"
#include "IIntentExecutor.h"
#include "Result.h"
#include "SettingsStore.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace ng::core;

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
        || op == QLatin1String("data.recent") || op == QLatin1String("data.clearCache");
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

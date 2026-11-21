#include "DataCommandsHandler.h"

#include "AppContext.h"
#include "DataSource.h"
#include "IngestOrchestrator.h"
#include "SourceRegistry.h"
#include "Dataset.h"
#include "IIntentExecutor.h"
#include "Result.h"
#include "ErrorCodes.h"
#include "SettingsStore.h"
#include "XlsxBook.h"

#include <QDir>
#include "WorkspaceDataConfig.h"
#include "ChannelTransformConfig.h"

#include <QFile>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QDateTime>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QVector>

#include <cmath>
#include <limits>
#include <map>
#include <memory>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
namespace rec = nextpilot::plotscope::rec;

namespace
{

// 序列 → (t, v) 两个 QVariantList：脚本/CLI/MCP 数据出口的标准形态。
QPair<QVariantList, QVariantList> toVariantLists(const QVector<double> &t,
                                                 const QVector<double> &v)
{
    const int n = qMin(t.size(), v.size());
    QVariantList ts, vs;
    ts.reserve(n);
    vs.reserve(n);
    vs.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        ts << t.at(i);
        vs << v.at(i);
    }
    return {ts, vs};
}

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

// 把源列对齐到基准轴（MIMO 派生的显式时间同步）。interpMode: linear/step/nearest，
// 范围外取端点值（边界延拓，不产 NaN）。基准轴有序 → 源列游标单调推进 O(n)。
// 注意：out 必须不与 st/sv 别名（调用方用局部量接收后回写）。
void resampleToBase(const QVector<double> &baseT, const QVector<double> &st,
                    const QVector<double> &sv, const QString &interpMode,
                    QVector<double> &out)
{
    out.clear();
    out.reserve(baseT.size());
    if (st.isEmpty())
        return;
    int j = 0; // 源列游标
    for (int i = 0; i < baseT.size(); ++i)
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
}
} // namespace

bool DataCommandsHandler::canHandle(const Intent &intent) const
{
    return m_dispatch.canHandle(intent);
}

Result<IntentResult> DataCommandsHandler::execute(const Intent &intent)
{
    return m_dispatch.execute(intent);
}

// op → 成员函数注册表：op 字符串只写一遍，canHandle/execute 查同一张表。
void DataCommandsHandler::buildDispatch()
{
    using I = nextpilot::plotscope::intent::Intent;
    m_dispatch.reg(QStringLiteral("data.reload"), this, &DataCommandsHandler::reload);
    m_dispatch.reg(QStringLiteral("data.export"), this, &DataCommandsHandler::exportCsv);
    m_dispatch.reg(QStringLiteral("data.exportSnapshot"), this,
                   &DataCommandsHandler::exportSnapshot);
    m_dispatch.reg(QStringLiteral("data.recent"), this, &DataCommandsHandler::recent);
    m_dispatch.reg(QStringLiteral("data.clearCache"),
                   [this](const I &) { return clearCache(); });
    m_dispatch.reg(QStringLiteral("data.cache.stats"),
                   [this](const I &) { return cacheStats(); });
    m_dispatch.reg(QStringLiteral("data.remove"), this, &DataCommandsHandler::removeSource);
    m_dispatch.reg(QStringLiteral("data.pause"),
                   [this](const I &i) { return setSourcePaused(i, true); });
    m_dispatch.reg(QStringLiteral("data.resume"),
                   [this](const I &i) { return setSourcePaused(i, false); });
    m_dispatch.reg(QStringLiteral("data.derive"), this, &DataCommandsHandler::derive);
    m_dispatch.reg(QStringLiteral("data.filter"), this, &DataCommandsHandler::filterSeries);
    m_dispatch.reg(QStringLiteral("data.marker"), this, &DataCommandsHandler::runMarker);
    m_dispatch.reg(QStringLiteral("data.state"), this, &DataCommandsHandler::stateSeries);
    m_dispatch.reg(QStringLiteral("data.series"), this, &DataCommandsHandler::series);
    m_dispatch.reg(QStringLiteral("data.workspace.list"),
                   [this](const I &) { return workspaceList(); });
    m_dispatch.reg(QStringLiteral("data.workspace.save"),
                   [this](const I &) { return workspaceSave(); });
    m_dispatch.reg(QStringLiteral("data.workspace.restore"),
                   [this](const I &) { return workspaceRestore(); });
    m_dispatch.reg(QStringLiteral("data.connect"), this, &DataCommandsHandler::connectLive);
    m_dispatch.reg(QStringLiteral("data.disconnect"), this, &DataCommandsHandler::disconnectLive);
    m_dispatch.reg(QStringLiteral("data.live.stats"), this, &DataCommandsHandler::liveStats);
    m_dispatch.reg(QStringLiteral("data.reconnect"), this, &DataCommandsHandler::reconnectLive);
    m_dispatch.reg(QStringLiteral("data.source.list"), this, &DataCommandsHandler::sourceList);
    m_dispatch.reg(QStringLiteral("data.rate.set"), this, &DataCommandsHandler::setRate);
    m_dispatch.reg(QStringLiteral("data.record.start"), this, &DataCommandsHandler::recordStart);
    m_dispatch.reg(QStringLiteral("data.record.stop"), this, &DataCommandsHandler::recordStop);
    m_dispatch.reg(QStringLiteral("data.record.list"), this, &DataCommandsHandler::recordList);
    m_dispatch.reg(QStringLiteral("data.config"), this, &DataCommandsHandler::sourceConfig);
    m_dispatch.reg(QStringLiteral("data.time.align"), this, &DataCommandsHandler::alignTime);
    m_dispatch.reg(QStringLiteral("data.transform.set"), this, &DataCommandsHandler::setChannelTransform);
    m_dispatch.reg(QStringLiteral("data.transform.clear"), this, &DataCommandsHandler::clearChannelTransform);
    m_dispatch.reg(QStringLiteral("data.derive.list"), this, &DataCommandsHandler::deriveList);
    m_dispatch.reg(QStringLiteral("data.derive.refresh"), this, &DataCommandsHandler::refreshDerives);
}

// === 兜底解析资产：dataset/table 的"缺省取唯一"口径全 handler 统一 ===
Result<int> DataCommandsHandler::resolveDataset(const Intent &intent) const
{
    const int datasetId = intent.params.value(QStringLiteral("dataset"), -1).toInt();
    if (datasetId < 0 && m_app.data().count() == 1)
        return m_app.data().ids().first();
    if (datasetId < 0)
        return fail(err::Dataset,
                    QStringLiteral("请指定 dataset 参数（当前有多个数据集）"));
    if (!m_app.data().get(datasetId))
        return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(datasetId));
    return datasetId;
}

Result<QString> DataCommandsHandler::resolveTable(const Dataset &ds, const QString &table) const
{
    QString resolved = table;
    if (resolved.isEmpty() && ds.tableCount() == 1)
        resolved = ds.tableNames().first();
    if (!ds.table(resolved))
        return fail(err::Table, QStringLiteral("表不存在: %1").arg(resolved));
    return resolved;
}

Result<IntentResult> DataCommandsHandler::reload(const Intent &intent)
{
    // 活动源：显式 id 参数优先，否则用 AppContext 的活动源。
    int sourceId = intent.params.value(QStringLiteral("id"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    const DataSourceInfo *info = m_app.sources().get(sourceId);
    if (!info || info->origin.isEmpty())
        return fail(err::NoSource, QStringLiteral("没有可重载的数据源"));

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
    // 通道来源：显式指定 dataset/table/column，否则用唯一数据集/唯一表兜底。
    const QString table = intent.params.value(QStringLiteral("table")).toString();
    const QString column = intent.params.value(QStringLiteral("column")).toString();

    Result<int> dsId = resolveDataset(intent);
    if (!dsId)
        return dsId.error();
    const Dataset *ds = m_app.data().get(dsId.value());

    Result<QString> targetTableRes = resolveTable(*ds, table);
    if (!targetTableRes)
        return targetTableRes.error();
    const QString targetTable = targetTableRes.value();

    // 没指定列则导出该表全部列。
    QStringList columns;
    if (!column.isEmpty())
        columns << column;
    else if (const Table *t = ds->table(targetTable))
        for (const Column &c : t->columns)
            columns << c.name;
    if (columns.isEmpty())
        return fail(err::Column, QStringLiteral("没有可导出的列"));

    const QString outPath = intent.params.value(QStringLiteral("path")).toString();
    if (outPath.isEmpty())
        return fail(err::Path, QStringLiteral("请指定导出路径 path"));

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail(err::Write, QStringLiteral("无法写入: %1").arg(outPath));

    // 对齐参与导出（T3.4）：画布是在 push 副本上叠加源偏移渲染的，
    // 导出必须同一口径，否则"画布看到的 X"与"导出的 time 列"差一个 offset。
    const int alignSrc = m_app.sources().sourceOfDataset(dsId.value());
    const double alignOffsetSec = alignSrc >= 0 ? m_app.sourceTimeOffset(alignSrc) : 0.0;

    QTextStream out(&file);
    if (!qFuzzyIsNull(alignOffsetSec))
        out << "# time_mode=offset, offset_us="
            << QString::number(qRound64(alignOffsetSec * 1000000.0))
            << ", offset_s=" << QString::number(alignOffsetSec, 'g', 12) << '\n';
    // 表头
    out << QStringLiteral("time");
    for (const QString &c : columns)
        out << ',' << c;
    out << '\n';

    // 采样上限（M4/T4.4）：settings 的 export.csv.maxPoints。
    //   0 / 未设 = 不限（按表的实际行数取全量）；
    //   n > 0   = 均匀抽稀到 n 点，但**末点必须保留**（抽稀不是截断）。
    // 上限交给 Dataset::channel 的 cap 去做——Rust 侧等步长取样，宿主不重复稀释。
    const int totalRows = [&] {
        const Table *t = ds->table(targetTable);
        return t ? t->rows : 0;
    }();
    int maxPoints = m_app.settings().value(QStringLiteral("export.csv.maxPoints"), 0).toInt();
    if (maxPoints < 0)
        maxPoints = 0; // 负值视为"没设"，不当成 0 点
    // 取数上限：抽稀要"末点保留"，不能直接交给 Rust 的等步长 cap（它会丢掉尾巴）。
    // 所以先按行数取原样点（有 200000 点的硬保护），再由宿主均匀抽稀。
    const int fetchCap = qMin(qMax(1, totalRows), int(kStateMaxPoints));

    QVector<ChannelView> views;
    for (const QString &c : columns)
    {
        auto v = ds->channel(targetTable, c, fetchCap);
        if (!v)
            return fail(err::Read, QStringLiteral("读取列失败: %1").arg(c));
        views.append(*v);
    }
    const int rows = views.isEmpty() ? 0 : views.first().size();
    // 均匀抽稀 + 末点钉住：picks[k] = floor(k*rows/n)，最后一格强制 rows-1。
    QVector<int> picks;
    if (maxPoints > 0 && rows > maxPoints)
    {
        picks.reserve(maxPoints);
        for (int k = 0; k < maxPoints - 1; ++k)
            picks.append(int(double(k) * double(rows) / double(maxPoints)));
        picks.append(rows - 1);
    }
    else
    {
        picks.reserve(rows);
        for (int i = 0; i < rows; ++i)
            picks.append(i);
    }
    for (int i : picks)
    {
        out << (views.first().time.value(i) + alignOffsetSec);
        for (const ChannelView &v : views)
            out << ',' << v.value.value(i);
        out << '\n';
    }
    file.close();

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("path"), outPath},
        {QStringLiteral("rows"), int(picks.size())},
        {QStringLiteral("total_rows"), totalRows},
        {QStringLiteral("sampled"), maxPoints > 0 && totalRows > maxPoints},
        {QStringLiteral("max_points"), maxPoints},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::exportSnapshot(const Intent &intent)
{
    const QString format = intent.params.value(QStringLiteral("format"),
                                               QStringLiteral("xlsx")).toString().toLower();
    if (format != QLatin1String("xlsx") && format != QLatin1String("csv"))
        return fail(err::Format,
                    QStringLiteral("format 只支持 xlsx / csv，收到: %1").arg(format));
    const QString path = intent.params.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return fail(err::Path, QStringLiteral("请指定导出路径 path"));

    // 数据集集合：显式 datasets（多源快照）；否则沿用 dataset / 唯一数据集兜底。
    QVector<int> datasetIds;
    for (const QVariant &v : intent.params.value(QStringLiteral("datasets")).toList())
    {
        const int id = v.toInt();
        if (id >= 0)
            datasetIds << id;
    }
    if (datasetIds.isEmpty())
    {
        const Result<int> only = resolveDataset(intent);
        if (!only)
            return only.error();
        datasetIds << only.value();
    }

    // 表集合：tables 为空或 ["*"] 表示全部表。
    QStringList wanted;
    for (const QVariant &v : intent.params.value(QStringLiteral("tables")).toList())
    {
        const QString name = v.toString();
        if (!name.isEmpty() && name != QLatin1String("*"))
            wanted << name;
    }
    const bool allTables = wanted.isEmpty();

    const int maxPoints =
        m_app.settings().value(QStringLiteral("export.csv.maxPoints"), 0).toInt();

    // 每张表先算成"表头 + 行"，xlsx/csv 两种落盘共用这一份，避免两条路径漂移。
    struct SnapshotTable
    {
        QString sheet;
        QStringList headers;
        QVector<QVector<nextpilot::plotscope::host::XlsxBook::Cell>> rows;
        int totalRows = 0;
        bool sampled = false;
    };
    QVector<SnapshotTable> tables;

    for (int dsId : datasetIds)
    {
        const Dataset *ds = m_app.data().get(dsId);
        if (!ds)
            return fail(err::NoEnt,
                        QStringLiteral("数据集不存在: %1").arg(dsId));
        const QStringList tableNames = ds->tableNames();
        const QStringList picked = allTables ? tableNames : wanted;
        for (const QString &tableName : picked)
        {
            const Table *t = ds->table(tableName);
            if (!t)
                continue; // 明确跳过的表不进快照（表名写错的场景）
            SnapshotTable out;
            // 多数据集时加前缀：两张表同名（不同源）在 xlsx 里会撞名。
            out.sheet = datasetIds.size() > 1
                            ? QStringLiteral("ds%1.%2").arg(dsId).arg(tableName)
                            : tableName;
            out.headers << QStringLiteral("time");
            for (const Column &c : t->columns)
                out.headers << c.name;
            out.totalRows = t->rows;

            // 取数：与 data.export 同一口径（time 列叠加源 offset，抽稀钉住末点）。
            const int alignSrc = m_app.sources().sourceOfDataset(dsId);
            const double offsetSec = alignSrc >= 0 ? m_app.sourceTimeOffset(alignSrc) : 0.0;
            const int fetchCap = qMin(qMax(1, t->rows), int(kStateMaxPoints));
            QVector<ChannelView> views;
            for (const Column &c : t->columns)
            {
                auto v = ds->channel(tableName, c.name, fetchCap);
                if (!v)
                    return fail(err::Read, QStringLiteral("读取列失败: %1").arg(c.name));
                views.append(*v);
            }
            const int rows = views.isEmpty() ? 0 : views.first().size();
            QVector<int> picks;
            if (maxPoints > 0 && rows > maxPoints)
            {
                out.sampled = true;
                for (int k = 0; k < maxPoints - 1; ++k)
                    picks.append(int(double(k) * double(rows) / double(maxPoints)));
                picks.append(rows - 1); // 末点必须保留（抽稀不是截断）
            }
            else
            {
                for (int i = 0; i < rows; ++i)
                    picks.append(i);
            }
            // 空表也留 sheet：只写表头，行数为 0——静默丢掉是最难查的那种错。
            for (int i : picks)
            {
                QVector<nextpilot::plotscope::host::XlsxBook::Cell> row;
                row.append(nextpilot::plotscope::host::XlsxBook::Cell::ofNumber(views.isEmpty()
                                                        ? 0.0
                                                        : views.first().time.value(i) + offsetSec));
                for (const ChannelView &v : views)
                    row.append(nextpilot::plotscope::host::XlsxBook::Cell::ofNumber(v.value.value(i)));
                out.rows.append(row);
            }
            tables.append(out);
        }
    }
    if (tables.isEmpty())
        return fail(err::NoEnt, QStringLiteral("没有可导出的表（检查 tables 参数）"));

    QStringList written;
    if (format == QLatin1String("xlsx"))
    {
        nextpilot::plotscope::host::XlsxBook book;
        for (const SnapshotTable &t : tables)
        {
            nextpilot::plotscope::host::XlsxBook::Sheet &sheet = book.addSheet(t.sheet);
            sheet.headers = t.headers;
            sheet.rows = t.rows;
        }
        QString err;
        if (!book.save(path, &err))
            return fail(err::Write, err);
        written << path;
    }
    else
    {
        QDir dir(path);
        if (!dir.exists() && !QDir().mkpath(path))
            return fail(err::Write, QStringLiteral("无法创建目录: %1").arg(path));
        for (const SnapshotTable &t : tables)
        {
            const QString file = dir.filePath(t.sheet + QStringLiteral(".csv"));
            QFile f(file);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
                return fail(err::Write, QStringLiteral("无法写入: %1").arg(file));
            QTextStream out(&f);
            out << t.headers.join(QLatin1Char(',')) << '\n';
            for (const QVector<nextpilot::plotscope::host::XlsxBook::Cell> &row : t.rows)
            {
                for (int i = 0; i < row.size(); ++i)
                {
                    if (i)
                        out << ',';
                    out << QString::number(row.at(i).number, 'g', 15);
                }
                out << '\n';
            }
            f.close();
            written << file;
        }
    }

    QVariantList tableInfo;
    int totalRows = 0;
    for (const SnapshotTable &t : tables)
    {
        totalRows += t.rows.size();
        QVariantMap info;
        info.insert(QStringLiteral("sheet"), t.sheet);
        info.insert(QStringLiteral("rows"), int(t.rows.size()));
        info.insert(QStringLiteral("total_rows"), t.totalRows);
        info.insert(QStringLiteral("sampled"), t.sampled);
        info.insert(QStringLiteral("columns"), t.headers);
        tableInfo.append(info);
    }
    IntentResult r;
    QVariantMap payload;
    payload.insert(QStringLiteral("format"), format);
    payload.insert(QStringLiteral("path"), path);
    payload.insert(QStringLiteral("tables"), tableInfo);
    payload.insert(QStringLiteral("table_count"), int(tables.size()));
    payload.insert(QStringLiteral("rows"), totalRows);
    payload.insert(QStringLiteral("files"), written);
    r.payload = payload;
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
    // 清空后进程内计数归零：否则"命中率"会把上一次生命周期的账算进来。
    nextpilot::plotscope::context::resetParseCacheCounters();

    IntentResult r;
    r.payload = removed;
    return r;
}

Result<IntentResult> DataCommandsHandler::cacheStats()
{
    QVariantList items;
    qint64 totalBytes = 0;
    for (int id : m_app.sources().ids())
    {
        const DataSourceInfo *info = m_app.sources().get(id);
        if (!info || info->origin.isEmpty())
            continue;
        QStringList parsers;
        for (const ParserBinding &b : info->parsers)
            parsers << b.parserId;
        if (parsers.isEmpty())
            parsers << QString(); // 源文件级缓存（parserId 为空）
        for (const QString &pid : parsers)
        {
            const QString path = pid.isEmpty()
                ? QFileInfo(info->origin).absoluteFilePath() + QStringLiteral(".psc")
                : cachePathForSource(*info, pid);
            const QFileInfo fi(path);
            if (!fi.exists())
                continue;
            totalBytes += fi.size();
            items.append(QVariantMap{
                {QStringLiteral("source"), id},
                {QStringLiteral("name"), info->name},
                {QStringLiteral("parser"), pid},
                {QStringLiteral("path"), path},
                {QStringLiteral("bytes"), QVariant::fromValue<qlonglong>(fi.size())},
                {QStringLiteral("mtime"), fi.lastModified().toString(Qt::ISODate)},
            });
        }
    }
    const auto counters = nextpilot::plotscope::context::parseCacheCounters();
    QVariantMap out;
    out.insert(QStringLiteral("entries"), int(items.size()));
    out.insert(QStringLiteral("bytes"), QVariant::fromValue<qlonglong>(totalBytes));
    out.insert(QStringLiteral("hits"), QVariant::fromValue<qlonglong>(counters.hits));
    out.insert(QStringLiteral("misses"), QVariant::fromValue<qlonglong>(counters.misses));
    out.insert(QStringLiteral("writes"), QVariant::fromValue<qlonglong>(counters.writes));
    out.insert(QStringLiteral("items"), items);
    IntentResult r;
    r.payload = out;
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
        return fail(err::NoSource, QStringLiteral("没有可移除的数据源"));

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
//                 degrees: false, unwrap: true, interp: "linear", force: false }
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
//
// 流式增量（对齐 PJ4 增量引擎"只处理新样本"）：
//   同名重发 data.derive（配方指纹一致）= 增量刷新——只读 [waterUs, now] 新样本
//   窗口（其它列窗口含 waterUs 时刻样本作插值左锚点），只算新段并追加注册表缓存，
//   水位推进到新段最大时间戳。配方（inputs/interp/degrees/unwrap）变了或 force=true
//   自动回全量。增量前 refreshSchema（实时 append 不刷 C++ 投影；O(列数) 低频可接受）。
//   实时源自动触发待接（IngestPipeline 无样本级信号，需节流设计）；当前由
//   data.derive.refresh 意图或脚本周期重发驱动。
Result<IntentResult> DataCommandsHandler::deriveMimoQuat(const Intent &intent)
{
    Result<int> dsIdRes = resolveDataset(intent);
    if (!dsIdRes)
        return dsIdRes.error();
    const DatasetId dsId = dsIdRes.value();
    const Dataset *ds = m_app.data().get(dsId);

    const QVariantList inputs = intent.params.value(QStringLiteral("inputs")).toList();
    if (inputs.size() != 4)
        return fail(err::Args,
                    QStringLiteral("quat_to_rpy 需要 inputs（w/x/y/z 四个输入）"));
    const QString topTable = intent.params.value(QStringLiteral("table")).toString();
    const QString filterId = intent.params.value(QStringLiteral("filter")).toString();
    if (filterId != QLatin1String("quat_to_rpy"))
        return fail(err::Args,
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
            return fail(err::Args,
                        QStringLiteral("inputs 元素需为列名或 {table,column} 对象: %1")
                            .arg(c.toString()));
        ins.append(in);
    }

    // 输出名：缺省 "<w列>.roll/.pitch/.yaw"（w 列取其列名，跨表对象输入同样适用）。
    // 前移解析：增量判定需要第一输出名查注册表。
    const QString prefix = intent.params.value(QStringLiteral("name")).toString();
    QStringList outNames = intent.params.value(QStringLiteral("outputs")).toStringList();
    if (outNames.size() != 3)
    {
        const QString base = prefix.isEmpty() ? ins.first().column : prefix;
        outNames = { base + QStringLiteral(".roll"), base + QStringLiteral(".pitch"),
                     base + QStringLiteral(".yaw") };
    }

    const bool degrees = intent.params.value(QStringLiteral("degrees"), false).toBool();
    const bool unwrap = intent.params.value(QStringLiteral("unwrap"), true).toBool();
    const QString interpMode =
        intent.params.value(QStringLiteral("interp"), QStringLiteral("linear")).toString();
    if (interpMode != QLatin1String("linear") && interpMode != QLatin1String("step")
        && interpMode != QLatin1String("nearest"))
        return fail(err::Args,
                    QStringLiteral("interp 仅支持 linear/step/nearest: %1").arg(interpMode));

    // 配方指纹：inputs（对象 {table,column}，规避列名含点歧义）+ interp + degrees + unwrap。
    // 同名重发且指纹一致 → 增量；变了 → 自动回全量（覆盖旧缓存）。
    QJsonObject recipeObj;
    QJsonArray recipeInputs;
    for (const MimoInput &in : ins)
        recipeInputs.append(QJsonObject{
            { QStringLiteral("table"), in.table },
            { QStringLiteral("column"), in.column } });
    recipeObj.insert(QStringLiteral("inputs"), recipeInputs);
    recipeObj.insert(QStringLiteral("interp"), interpMode);
    recipeObj.insert(QStringLiteral("degrees"), degrees);
    recipeObj.insert(QStringLiteral("unwrap"), unwrap);
    const QByteArray recipe = QJsonDocument(recipeObj).toJson(QJsonDocument::Compact);

    // 增量判定：同名派生已存在、同数据集、同配方、已有水位，且未 force。
    auto &registry = m_app.derivedRegistry();
    const DerivedSeriesRegistry::Entry *prev = registry.get(outNames.first());
    const bool incremental = prev != nullptr
        && prev->datasetId == dsId
        && prev->filterId == QLatin1String("quat_to_rpy")
        && prev->recipe == recipe
        && prev->waterUs != std::numeric_limits<qint64>::min()
        && !intent.params.value(QStringLiteral("force"), false).toBool();

    constexpr int kDeriveMaxPoints = 200000;
    QVector<double> time;   // 本段输出时间轴（秒）= 基准轴（全量）或新段（增量）
    QVector<double> r, p, yaw;
    // 增量分支在 appendSeries 后 prev 悬空（registry 内部可能重分配）——
    // 所有需要的 prev 值在进入增量分支前拷贝成本地量。
    const qint64 waterUs = incremental ? prev->waterUs : 0;
    const int prevTotal = incremental ? prev->t.size() : 0;

    if (incremental)
    {
        // === 增量分支：只读 [waterUs, now] 新样本窗口，只算新段 ===
        // 实时 appendFrame 不刷 C++ schema 投影（O(列数)，不能每批调）——
        // 增量刷新是低频操作，先刷一次拿到最新 lastTimeUs。
        Dataset *dsMut = m_app.data().getMutable(dsId);
        if (!dsMut)
            return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(dsId));
        dsMut->refreshSchema();

        // 窗口右界 = 各输入表最新 lastTimeUs 的最大值（schema 投影，O(1)）。
        TimeUs tNow = std::numeric_limits<TimeUs>::min();
        for (const MimoInput &in : ins)
        {
            const Table *t = dsMut->table(in.table);
            if (t)
                tNow = qMax(tNow, t->lastTimeUs);
        }
        if (tNow <= waterUs)
        {
            // 无新样本：no-op 成功（幂等刷新）。
            IntentResult res;
            res.payload = QVariantMap{
                {QStringLiteral("dataset"), dsId},
                {QStringLiteral("filter"), QStringLiteral("quat_to_rpy")},
                {QStringLiteral("incremental"), true},
                {QStringLiteral("noop"), true},
                {QStringLiteral("count"), 0},
                {QStringLiteral("total"), prevTotal},
            };
            return res;
        }

        // 读四列窗口 [waterUs, tNow]（kernel 左闭右闭）。其它列窗口含 waterUs
        // 时刻样本——它是基准新段的合法插值左锚点（输出行时间全部 > waterUs，
        // 锚点本身不会重复输出）。
        QVector<ChannelView> cols;
        for (const MimoInput &in : ins)
        {
            auto ch = dsMut->readWindow(in.table, in.column, waterUs, tNow,
                                        kDeriveMaxPoints);
            if (!ch)
                return fail(err::Chan,
                            QStringLiteral("读取通道失败: %1.%2").arg(in.table, in.column));
            cols.append(std::move(*ch));
        }

        // 基准新段：丢弃 t <= waterUs（上次已处理；kernel 窗口左闭含 waterUs）。
        // 比较在微秒域做（llround 往返精确），避免 double 秒/微秒换算的 1ulp 误判。
        time.clear();
        for (int i = 0; i < cols[0].time.size(); ++i)
        {
            if (static_cast<qint64>(std::llround(cols[0].time.at(i) * 1000000.0))
                > waterUs)
                time.append(cols[0].time.at(i));
        }
        const int n = time.size();
        if (n <= 0)
        {
            // 其它表有新数据但基准列没有 → 输出轴不动，no-op。
            IntentResult res;
            res.payload = QVariantMap{
                {QStringLiteral("dataset"), dsId},
                {QStringLiteral("filter"), QStringLiteral("quat_to_rpy")},
                {QStringLiteral("incremental"), true},
                {QStringLiteral("noop"), true},
                {QStringLiteral("count"), 0},
                {QStringLiteral("total"), prevTotal},
            };
            return res;
        }

        // 基准列新段值 + 其它列对齐到新段。
        QVector<double> baseV;
        baseV.reserve(n);
        for (int i = 0; i < cols[0].value.size(); ++i)
        {
            if (static_cast<qint64>(std::llround(cols[0].time.at(i) * 1000000.0))
                > waterUs)
                baseV.append(cols[0].value.at(i));
        }
        cols[0].time = time;
        cols[0].value = baseV;
        for (int k = 1; k < 4; ++k)
        {
            const QVector<double> &st = cols[k].time;
            if (st.isEmpty())
                return fail(err::Empty,
                            QStringLiteral("输入无新样本: %1.%2")
                                .arg(ins[k].table, ins[k].column));
            QVector<double> aligned;
            resampleToBase(time, st, cols[k].value, interpMode, aligned);
            cols[k].value = aligned;
        }

        // NaN 防线（增量段同样适用）。
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < cols[k].value.size(); ++i)
            {
                if (std::isnan(cols[k].value.at(i)))
                    return fail(err::Nan,
                                QStringLiteral("输入含 NaN/null 样本，拒绝派生: %1.%2 @[%3]")
                                    .arg(ins[k].table, ins[k].column)
                                    .arg(i));
            }
        }

        r.resize(n);
        p.resize(n);
        yaw.resize(n);
        char errBuf[512] = {0};
        const int rc = ps_quat_to_rpy(static_cast<uint32_t>(n),
                                      cols[0].value.constData(), cols[1].value.constData(),
                                      cols[2].value.constData(), cols[3].value.constData(),
                                      degrees ? 1 : 0, unwrap ? 1 : 0,
                                      r.data(), p.data(), yaw.data(),
                                      errBuf, sizeof(errBuf));
        if (rc != 0)
            return fail(err::Derive,
                        QStringLiteral("quat_to_rpy 失败: %1").arg(QString::fromUtf8(errBuf)));

        // 追加注册表缓存 + 水位推进到新段最大时间戳。
        const qint64 newWater =
            static_cast<qint64>(std::llround(time.last() * 1000000.0));
        const QVector<QVector<double>> vals = { r, p, yaw };
        QVariantList outPayload;
        const QString unit = degrees ? QStringLiteral("deg") : QStringLiteral("rad");
        for (int k = 0; k < 3; ++k)
        {
            registry.appendSeries(outNames.at(k), time, vals[k], newWater);
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
            {QStringLiteral("incremental"), true},
            {QStringLiteral("total"), prevTotal + n},
        };
        return res;
    }

    // === 全量分支（首次派生 / 配方变更 / force）===
    // 读四列（各自可来自不同表）。派生读上限放宽到 200k（同 data.state 的
    // kStateMaxPoints）：真实日志远超默认 4096，等步长抽稀时间/值成对取、行对齐保持。
    QVector<ChannelView> cols;
    for (const MimoInput &in : ins)
    {
        auto ch = ds->channel(in.table, in.column, kDeriveMaxPoints);
        if (!ch)
            return fail(err::Chan,
                        QStringLiteral("读取通道失败: %1.%2").arg(in.table, in.column));
        cols.append(std::move(*ch));
    }

    // 基准时间轴 = 第一个输入的轴；防御性校验非降序（kernel 表轴有序，这里是防线）。
    const QVector<double> baseT = cols[0].time;
    for (int i = 1; i < baseT.size(); ++i)
    {
        if (baseT.at(i) < baseT.at(i - 1))
            return fail(err::Time,
                        QStringLiteral("基准时间轴非单调: %1.%2")
                            .arg(ins[0].table, ins[0].column));
    }
    const int n = baseT.size();
    if (n <= 0)
        return fail(err::Empty, QStringLiteral("四元数通道无样本"));

    // 跨表输入重采样到基准轴。同表输入也统一走这里：门面 readWindow 会把 null/NaN
    // 样本成对剔除（绘图语义），同表各列读出点数可能不同——行对齐不能假设，
    // 必须显式对齐。基准轴有序 → 源列游标单调推进，O(n)。
    for (int k = 1; k < 4; ++k)
    {
        const QVector<double> &st = cols[k].time;
        const QVector<double> &sv = cols[k].value;
        if (st.isEmpty())
            return fail(err::Empty,
                        QStringLiteral("输入无样本: %1.%2").arg(ins[k].table, ins[k].column));
        if (cols[k].table == cols[0].table && cols[k].value.size() == n
            && std::equal(st.cbegin(), st.cend(), baseT.cbegin()))
            continue; // 快路径：轴完全一致（无 null 剔除）→ 已行对齐
        QVector<double> aligned;
        resampleToBase(baseT, st, sv, interpMode, aligned);
        cols[k].value = aligned;
    }

    // NaN 防线：null 样本经 kernel 读出即 NaN，quat 归一化会把它传播成垃圾角度——
    // 拒绝派生并指明位置，绝不静默产出。
    for (int k = 0; k < 4; ++k)
    {
        for (int i = 0; i < cols[k].value.size(); ++i)
        {
            if (std::isnan(cols[k].value.at(i)))
                return fail(err::Nan,
                            QStringLiteral("输入含 NaN/null 样本，拒绝派生: %1.%2 @[%3]")
                                .arg(ins[k].table, ins[k].column)
                                .arg(i));
        }
    }

    r.resize(n);
    p.resize(n);
    yaw.resize(n);
    char errBuf[512] = {0};
    const int rc = ps_quat_to_rpy(static_cast<uint32_t>(n),
                                  cols[0].value.constData(), cols[1].value.constData(),
                                  cols[2].value.constData(), cols[3].value.constData(),
                                  degrees ? 1 : 0, unwrap ? 1 : 0,
                                  r.data(), p.data(), yaw.data(),
                                  errBuf, sizeof(errBuf));
    if (rc != 0)
        return fail(err::Derive,
                    QStringLiteral("quat_to_rpy 失败: %1").arg(QString::fromUtf8(errBuf)));

    time = baseT;
    const QVector<QVector<double>> vals = { r, p, yaw };
    QVariantList outPayload;
    const QString unit = degrees ? QStringLiteral("deg") : QStringLiteral("rad");
    // 全量登记时写配方指纹 + 水位（基准轴最大时间戳），为下次同名重发的增量做准备。
    const QByteArray recipeCopy = recipe;
    const qint64 fullWater = static_cast<qint64>(std::llround(baseT.last() * 1000000.0));
    for (int k = 0; k < 3; ++k)
    {
        // 登记（独立派生字段）+ 画布事件。
        nextpilot::plotscope::context::DerivedSeriesRegistry::Entry entry;
        entry.name = outNames.at(k);
        entry.datasetId = dsId;
        entry.table = ins.first().table;
        entry.column = ins.first().column;
        entry.filterId = QStringLiteral("quat_to_rpy");
        entry.recipe = recipeCopy;
        entry.waterUs = fullWater;
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
        {QStringLiteral("incremental"), false},
    };
    return res;
}

// data.derive.refresh {dataset}：对该数据集全部 MIMO（quat_to_rpy）派生执行增量刷新。
// 配方从注册表指纹重建（inputs/interp/degrees/unwrap + 注册时的三条输出名），
// 逐组重发同名 data.derive——同名重发即增量（见 deriveMimoQuat），同一配方的
// 第 2/3 条输出在首条刷新后水位已推进，自动 no-op。
// 返回 refreshed = 实际产出新样本的派生组数；无实时源自动触发的钩子前，
// 这是实时链路刷新派生的入口（脚本/CLI/MCP 周期调用）。
Result<IntentResult> DataCommandsHandler::refreshDerives(const Intent &intent)
{
    Result<int> dsIdRes = resolveDataset(intent);
    if (!dsIdRes)
        return dsIdRes.error();
    const DatasetId dsId = dsIdRes.value();

    // 按 recipe 指纹分组（同组 = 同一配方派生的三条输出，注册顺序 roll/pitch/yaw）。
    QHash<QByteArray, QStringList> groups;
    for (const DerivedSeriesRegistry::Entry &e : m_app.derivedRegistry().all())
    {
        if (e.datasetId != dsId || e.filterId != QLatin1String("quat_to_rpy")
            || e.recipe.isEmpty())
            continue;
        groups[e.recipe].append(e.name);
    }

    int refreshed = 0;
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
    {
        const QStringList outNames = it.value();
        if (outNames.size() != 3)
            continue; // 防御：配方组不完整（部分被删）跳过，等下次全量重建
        const QJsonObject obj = QJsonDocument::fromJson(it.key()).object();
        QVariantList insV;
        for (const auto &v : obj.value(QStringLiteral("inputs")).toArray())
        {
            const QJsonObject io = v.toObject();
            insV.append(QVariantMap{
                { QStringLiteral("table"), io.value(QStringLiteral("table")).toString() },
                { QStringLiteral("column"), io.value(QStringLiteral("column")).toString() } });
        }
        if (insV.size() != 4)
            continue;

        Intent d;
        d.op = QStringLiteral("data.derive");
        d.params[QStringLiteral("dataset")] = dsId;
        d.params[QStringLiteral("inputs")] = insV;
        d.params[QStringLiteral("outputs")] = outNames;
        d.params[QStringLiteral("interp")] = obj.value(QStringLiteral("interp")).toString();
        d.params[QStringLiteral("degrees")] = obj.value(QStringLiteral("degrees")).toBool(false);
        d.params[QStringLiteral("unwrap")] = obj.value(QStringLiteral("unwrap")).toBool(true);
        const auto r = deriveMimoQuat(d);
        if (r && r.value().payload.toMap().value(QStringLiteral("count")).toInt() > 0)
            ++refreshed;
    }

    IntentResult res;
    res.payload = QVariantMap{
        { QStringLiteral("dataset"), dsId },
        { QStringLiteral("groups"), groups.size() },
        { QStringLiteral("refreshed"), refreshed },
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
        return fail(err::NoSource, QStringLiteral("没有可操作的数据源"));
    if (info->kind != SourceKind::Live)
        return fail(err::NotSupported,
                    QStringLiteral("仅实时(live)源支持暂停/继续，当前源不是实时源"));
    // 套接字侧同步暂停态：暂停时把内核缓冲读空但不喂解析器（不做补帧）。
    if (auto it = m_live.find(sourceId); it != m_live.end())
        it->second.paused = paused;

    const SourceState target = paused ? SourceState::Paused : SourceState::Streaming;
    if (info->state != target)
    {
        if (!m_app.sources().setState(sourceId, target))
            return fail(err::State,
                        QStringLiteral("数据源状态切换失败（当前状态 %1）").arg(int(info->state)));
    }

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("id"), sourceId},
        {QStringLiteral("paused"), paused},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::connectLive(const Intent &intent)
{
    // 只有一个前端：udp 套接字。解析器可多路（同一份字节扇出）——
    // 这正是 IngestPipeline 的形态，宿主这里只负责"把字节送进去 + 记源"。
    const QString url = intent.params.value(QStringLiteral("url")).toString();
    if (url.isEmpty())
        return fail(err::Args, QStringLiteral("缺少 url（例：udp://0.0.0.0:14550）"));
    const QUrl qurl(url);
    if (qurl.scheme() == QLatin1String("serial"))
        return connectSerial(intent);
    if (qurl.scheme() == QLatin1String("tcp"))
        return connectTcp(intent);
    if (qurl.scheme() != QLatin1String("udp"))
        return fail(err::Args, QStringLiteral("支持 udp:// 与 serial://（收到 %1）").arg(qurl.scheme()));
    const int port = qurl.port(0);
    if (port <= 0 || port > 65535)
        return fail(err::Args, QStringLiteral("端口无效: %1").arg(qurl.port()));

    const QString parserId = intent.params.value(QStringLiteral("parser")).toString();
    if (parserId.isEmpty())
        return fail(err::Args, QStringLiteral("请指定 parser（如 custom-protocol）"));

    // 解析参数：template 内联 JSON 优先；templatePath 由宿主读成内联再注入
    // （解析器不读文件，这条边界从协议模块起就一致）。
    QByteArray parserOptions;
    if (const QVariant tv = intent.params.value(QStringLiteral("template")); tv.isValid()
        && !tv.toString().isEmpty())
    {
        parserOptions = QJsonDocument(QJsonObject{{QStringLiteral("template"), tv.toString()}})
                            .toJson(QJsonDocument::Compact);
    }
    else if (const QString tp = intent.params.value(QStringLiteral("templatePath")).toString();
             !tp.isEmpty())
    {
        QFile f(tp);
        if (!f.open(QIODevice::ReadOnly))
            return fail(err::Path, QStringLiteral("无法读取模板: %1").arg(tp));
        parserOptions = QJsonDocument(QJsonObject{{QStringLiteral("template"),
                                                   QString::fromUtf8(f.readAll())}})
                            .toJson(QJsonDocument::Compact);
    }

    DataSourceInfo info;
    info.name = intent.params.value(QStringLiteral("name")).toString();
    if (info.name.isEmpty())
        info.name = url;
    info.origin = url;
    info.kind = SourceKind::Live;
    info.state = SourceState::Streaming;
    ParserBinding binding;
    binding.parserId = parserId;
    binding.active = true;
    binding.options = parserOptions;
    info.parsers << binding;

    const int sourceId = m_app.sources().add(info);
    if (sourceId < 0)
        return fail(err::Io, QStringLiteral("数据源登记失败"));

    const auto bound = m_app.ingest().bindParser(sourceId, binding, kInvalidDataset);
    if (!bound)
    {
        m_app.sources().remove(sourceId);
        return bound.error();
    }
    m_app.sources().addDataset(sourceId, bound.value());

    auto sock = std::make_unique<QUdpSocket>();
    const QString host = qurl.host().isEmpty() ? QStringLiteral("0.0.0.0") : qurl.host();
    if (!sock->bind(QHostAddress(host), quint16(port)))
    {
        m_app.sources().remove(sourceId);
        return fail(err::Io, QStringLiteral("绑定 %1:%2 失败: %3")
                                  .arg(host).arg(port).arg(sock->errorString()));
    }

    // 收包即喂：扇出由管道负责（多解析器共享同一份字节）。
    LiveSession session;
    session.sourceId = sourceId;
    QUdpSocket *raw = sock.get();
    QObject::connect(raw, &QUdpSocket::readyRead, raw, [this, raw, sourceId] {
        // 只入环：解析由泵按预算做，避免一包一解析把事件循环占住。
        while (raw->hasPendingDatagrams())
        {
            const QNetworkDatagram dg = raw->receiveDatagram();
            enqueueLive(sourceId, dg.data());
        }
    });
    session.since.start();
    session.url = url;
    session.parserId = parserId;
    session.parserOptions = parserOptions;
    session.socket = std::move(sock);
    m_live[sourceId] = std::move(session);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("source"), sourceId},
        {QStringLiteral("dataset"), bound.value()},
        {QStringLiteral("url"), url},
        {QStringLiteral("parser"), parserId},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::connectTcp(const Intent &intent)
{
    // 与 UDP 同一出口：字节进管道扇出。差别只在"字节从哪来"。
    const QString url = intent.params.value(QStringLiteral("url")).toString();
    const QUrl qurl(url);
    const int port = qurl.port(0);
    if (port <= 0 || port > 65535)
        return fail(err::Args, QStringLiteral("TCP 端口无效: %1").arg(url));
    const QString parserId = intent.params.value(QStringLiteral("parser")).toString();
    if (parserId.isEmpty())
        return fail(err::Args, QStringLiteral("请指定 parser（如 custom-protocol）"));

    QByteArray parserOptions;
    if (const QVariant tv = intent.params.value(QStringLiteral("template")); tv.isValid()
        && !tv.toString().isEmpty())
        parserOptions = QJsonDocument(QJsonObject{{QStringLiteral("template"), tv.toString()}})
                            .toJson(QJsonDocument::Compact);
    else if (const QString tp = intent.params.value(QStringLiteral("templatePath")).toString();
             !tp.isEmpty())
    {
        QFile f(tp);
        if (!f.open(QIODevice::ReadOnly))
            return fail(err::Path, QStringLiteral("无法读取模板: %1").arg(tp));
        parserOptions = QJsonDocument(QJsonObject{{QStringLiteral("template"),
                                                   QString::fromUtf8(f.readAll())}})
                            .toJson(QJsonDocument::Compact);
    }

    DataSourceInfo info;
    info.name = intent.params.value(QStringLiteral("name")).toString();
    if (info.name.isEmpty())
        info.name = url;
    info.origin = url;
    info.kind = SourceKind::Live;
    info.state = SourceState::Streaming;
    ParserBinding binding;
    binding.parserId = parserId;
    binding.active = true;
    binding.options = parserOptions;
    info.parsers << binding;

    const int sourceId = m_app.sources().add(info);
    if (sourceId < 0)
        return fail(err::Io, QStringLiteral("数据源登记失败"));
    const auto bound = m_app.ingest().bindParser(sourceId, binding, kInvalidDataset);
    if (!bound)
    {
        m_app.sources().remove(sourceId);
        return bound.error();
    }
    m_app.sources().addDataset(sourceId, bound.value());

    LiveSession session;
    session.sourceId = sourceId;
    session.url = url;
    session.parserId = parserId;
    session.parserOptions = parserOptions;

    const QString host = qurl.host();
    const bool listen = host.isEmpty() || host == QLatin1String("0.0.0.0");

    // 收字节的统一出口：一条 lambda，客户端/监听端共用。
    auto attach = [this, sourceId](QTcpSocket *sock) {
        QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock, sourceId] {
            auto it = m_live.find(sourceId);
            if (it == m_live.end())
                return;
            while (sock->bytesAvailable() > 0)
            {
                const QByteArray bytes = sock->readAll();
                if (bytes.isEmpty())
                    break;
                enqueueLive(sourceId, bytes);
            }
        });
    };

    if (listen)
    {
        auto server = std::make_unique<QTcpServer>();
        if (!server->listen(QHostAddress::Any, quint16(port)))
        {
            m_app.sources().remove(sourceId);
            return fail(err::Io, QStringLiteral("TCP 监听 %1 失败: %2").arg(port).arg(server->errorString()));
        }
        QTcpServer *rawServer = server.get();
        QObject::connect(rawServer, &QTcpServer::newConnection, rawServer, [this, rawServer, sourceId, attach] {
            auto it = m_live.find(sourceId);
            if (it == m_live.end())
                return;
            while (rawServer->hasPendingConnections())
            {
                QTcpSocket *conn = rawServer->nextPendingConnection();
                attach(conn);
            }
        });
        session.tcpServer = std::move(server);
    }
    else
    {
        auto sock = std::make_unique<QTcpSocket>();
        attach(sock.get());
        sock->connectToHost(host, quint16(port));
        session.tcp = std::move(sock);
    }

    session.since.start();
    m_live[sourceId] = std::move(session);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("source"), sourceId},
        {QStringLiteral("dataset"), bound.value()},
        {QStringLiteral("url"), url},
        {QStringLiteral("parser"), parserId},
        {QStringLiteral("mode"), listen ? QStringLiteral("listen") : QStringLiteral("client")},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::connectSerial(const Intent &intent)
{
#ifndef Q_OS_UNIX
    return fail(err::NotSupported, QStringLiteral("串口前端目前只在 Linux/Unix 上实现"));
#else
    // 形如 serial:///dev/ttyUSB0:115200（设备路径里可能带 ':'，按**最后一个**冒号切）
    QString rest = intent.params.value(QStringLiteral("url")).toString();
    rest.remove(QStringLiteral("serial://"));
    const int sep = rest.lastIndexOf(QLatin1Char(':'));
    const QString dev = sep > 0 ? rest.left(sep) : rest;
    const QString baudText = sep > 0 ? rest.mid(sep + 1) : QStringLiteral("115200");
    if (dev.isEmpty())
        return fail(err::Args, QStringLiteral("缺少串口设备路径（serial:///dev/ttyUSB0:115200）"));
    static const QHash<int, int> kBaud{{9600, B9600}, {19200, B19200}, {38400, B38400},
                                       {57600, B57600}, {115200, B115200}, {230400, B230400},
                                       {460800, B460800}, {921600, B921600}};
    const int baud = baudText.toInt();
    if (!kBaud.contains(baud))
        return fail(err::Args, QStringLiteral("不支持的波特率: %1").arg(baudText));

    const QString parserId = intent.params.value(QStringLiteral("parser")).toString();
    if (parserId.isEmpty())
        return fail(err::Args, QStringLiteral("请指定 parser（如 custom-protocol）"));

    QByteArray parserOptions;
    if (const QVariant tv = intent.params.value(QStringLiteral("template")); tv.isValid()
        && !tv.toString().isEmpty())
        parserOptions = QJsonDocument(QJsonObject{{QStringLiteral("template"), tv.toString()}})
                            .toJson(QJsonDocument::Compact);
    else if (const QString tp = intent.params.value(QStringLiteral("templatePath")).toString();
             !tp.isEmpty())
    {
        QFile f(tp);
        if (!f.open(QIODevice::ReadOnly))
            return fail(err::Path, QStringLiteral("无法读取模板: %1").arg(tp));
        parserOptions = QJsonDocument(QJsonObject{{QStringLiteral("template"),
                                                   QString::fromUtf8(f.readAll())}})
                            .toJson(QJsonDocument::Compact);
    }

    DataSourceInfo info;
    info.name = intent.params.value(QStringLiteral("name")).toString();
    if (info.name.isEmpty())
        info.name = dev;
    info.origin = QStringLiteral("serial://%1:%2").arg(dev).arg(baud);
    info.kind = SourceKind::Live;
    info.state = SourceState::Streaming;
    ParserBinding binding;
    binding.parserId = parserId;
    binding.active = true;
    binding.options = parserOptions;
    info.parsers << binding;

    // 先开设备再登记源：打不开就不该在树里留一个"假的实时源"。
    const int fd = ::open(dev.toUtf8().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return fail(err::Io, QStringLiteral("打开串口失败: %1").arg(dev));
    struct termios tio {};
    if (::tcgetattr(fd, &tio) == 0)
    {
        ::cfmakeraw(&tio);
        ::cfsetispeed(&tio, kBaud.value(baud));
        ::cfsetospeed(&tio, kBaud.value(baud));
        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;
        ::tcsetattr(fd, TCSANOW, &tio);
    }

    const int sourceId = m_app.sources().add(info);
    if (sourceId < 0)
    {
        ::close(fd);
        return fail(err::Io, QStringLiteral("数据源登记失败"));
    }
    const auto bound = m_app.ingest().bindParser(sourceId, binding, kInvalidDataset);
    if (!bound)
    {
        ::close(fd);
        m_app.sources().remove(sourceId);
        return bound.error();
    }
    m_app.sources().addDataset(sourceId, bound.value());

    LiveSession session;
    session.sourceId = sourceId;
    session.fd = fd;
    session.since.start();
    session.url = intent.params.value(QStringLiteral("url")).toString();
    session.parserId = parserId;
    session.parserOptions = parserOptions;
    auto notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    QSocketNotifier *raw = notifier.get();
    QObject::connect(raw, &QSocketNotifier::activated, raw, [this, raw, fd, sourceId] {
        auto it = m_live.find(sourceId);
        if (it == m_live.end())
            return;
        char buf[8192];
        while (true)
        {
            const ssize_t n = ::read(fd, buf, sizeof buf);
            if (n <= 0)
                break;
            it->second.bytes += n;
            it->second.packets += 1; // 口径：一次可读触发算一"批"（与 UDP 的 datagram 区分，见 stats 注释）
            if (it->second.paused)
                continue;
            m_app.ingest().feed(sourceId, reinterpret_cast<const uint8_t *>(buf),
                                qint64(n));
        }
    });
    session.notifier = std::move(notifier);
    m_live[sourceId] = std::move(session);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("source"), sourceId},
        {QStringLiteral("dataset"), bound.value()},
        {QStringLiteral("url"), info.origin},
        {QStringLiteral("parser"), parserId},
        {QStringLiteral("baud"), baud},
    };
    return r;
#endif
}

Result<IntentResult> DataCommandsHandler::disconnectLive(const Intent &intent)
{
    int sourceId = intent.params.value(QStringLiteral("id"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    auto it = m_live.find(sourceId);
    if (it == m_live.end())
        return fail(err::NoSource, QStringLiteral("该数据源不是活动的实时源: %1").arg(sourceId));

    // 先把环里剩下的字节喂完（泵是异步的，断开时可能还有积压），再 flush 尾半帧。
    if (!it->second.buf.isEmpty())
    {
        m_app.ingest().feed(sourceId,
                            reinterpret_cast<const uint8_t *>(it->second.buf.constData()),
                            it->second.buf.size());
        it->second.fedBytes += it->second.buf.size();
        it->second.buf.clear();
    }
    // 断源前把该源的录制收口（不关就会留下没写尾记录的文件）。
    for (auto rit = m_records.begin(); rit != m_records.end();)
    {
        if (rit->sourceId != sourceId)
        {
            ++rit;
            continue;
        }
        if (rit->recorder)
            rit->recorder->close();
        rit = m_records.erase(rit);
    }
    // 先 flush 尾半帧，再断 socket，最后落状态——顺序反了会丢最后一个包。
    m_app.ingest().finish(sourceId);
    if (it->second.socket)
        it->second.socket->close();
    if (it->second.tcp)
        it->second.tcp->close();
    if (it->second.tcpServer)
        it->second.tcpServer->close();
#ifdef Q_OS_UNIX
    if (it->second.fd >= 0)
        ::close(it->second.fd);
#endif
    const qint64 packets = it->second.packets;
    const qint64 bytes = it->second.bytes;
    m_live.erase(it);
    m_app.sources().setState(sourceId, SourceState::Closed);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("source"), sourceId},
        {QStringLiteral("packets"), QVariant::fromValue<qlonglong>(packets)},
        {QStringLiteral("bytes"), QVariant::fromValue<qlonglong>(bytes)},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::reconnectLive(const Intent &intent)
{
    int sourceId = intent.params.value(QStringLiteral("id"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    auto it = m_live.find(sourceId);
    if (it == m_live.end())
        return fail(err::NoSource, QStringLiteral("该数据源不是活动的实时源: %1").arg(sourceId));

    // 记住旧参数与旧计数；断开前端（不动源、不动数据集、不动解析器实例）。
    const QString url = it->second.url;
    const QString parserId = it->second.parserId;
    QByteArray options = it->second.parserOptions;
    const qint64 oldPackets = it->second.packets;
    // 重连前把旧连接积压的字节喂完再丢，避免串到新连接里。
    if (!it->second.buf.isEmpty())
    {
        m_app.ingest().feed(sourceId,
                            reinterpret_cast<const uint8_t *>(it->second.buf.constData()),
                            it->second.buf.size());
        it->second.fedBytes += it->second.buf.size();
        it->second.buf.clear();
    }
    if (it->second.socket)
        it->second.socket->close();
#ifdef Q_OS_UNIX
    if (it->second.fd >= 0)
        ::close(it->second.fd);
#endif
    it->second.socket.reset();
    it->second.notifier.reset();
    it->second.fd = -1;

    // 新前端：仅 UDP / 串口支持重连。
    const QUrl qurl(url);
    if (qurl.scheme() == QLatin1String("udp"))
    {
        const int port = qurl.port(0);
        if (port <= 0)
            return fail(err::Args, QStringLiteral("重连失败：url 端口丢失: %1").arg(url));
        auto sock = std::make_unique<QUdpSocket>();
        const QString host = qurl.host().isEmpty() ? QStringLiteral("0.0.0.0") : qurl.host();
        if (!sock->bind(QHostAddress(host), quint16(port)))
            return fail(err::Io, QStringLiteral("重连绑定失败: %1").arg(sock->errorString()));
        QUdpSocket *raw = sock.get();
        QObject::connect(raw, &QUdpSocket::readyRead, raw, [this, raw, sourceId] {
            while (raw->hasPendingDatagrams())
            {
                const QNetworkDatagram dg = raw->receiveDatagram();
                enqueueLive(sourceId, dg.data());
            }
        });
        it->second.socket = std::move(sock);
    }
#ifdef Q_OS_UNIX
    else if (qurl.scheme() == QLatin1String("serial"))
    {
        // 复用 connectSerial 的 termios 配置逻辑过重，这里按同一规则重开。
        QString rest = url;
        rest.remove(QStringLiteral("serial://"));
        const int sep = rest.lastIndexOf(QLatin1Char(':'));
        const QString dev = sep > 0 ? rest.left(sep) : rest;
        const int fd = ::open(dev.toUtf8().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0)
            return fail(err::Io, QStringLiteral("重连打开串口失败: %1").arg(dev));
        struct termios tio {};
        if (::tcgetattr(fd, &tio) == 0)
        {
            ::cfmakeraw(&tio);
            tio.c_cflag |= (CLOCAL | CREAD);
            tio.c_cc[VMIN] = 0;
            tio.c_cc[VTIME] = 0;
            ::tcsetattr(fd, TCSANOW, &tio);
        }
        auto notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
        QSocketNotifier *raw = notifier.get();
        QObject::connect(raw, &QSocketNotifier::activated, raw, [this, raw, fd, sourceId] {
            auto iter = m_live.find(sourceId);
            if (iter == m_live.end())
                return;
            char buf[8192];
            while (true)
            {
                const ssize_t n = ::read(fd, buf, sizeof buf);
                if (n <= 0)
                    break;
                enqueueLive(sourceId, QByteArray(buf, int(n)));
            }
        });
        it->second.fd = fd;
        it->second.notifier = std::move(notifier);
    }
#endif
    else
        return fail(err::NotSupported, QStringLiteral("不支持的 url 方案: %1").arg(qurl.scheme()));

    it->second.since.restart();
    it->second.url = url;
    it->second.parserId = parserId;
    it->second.parserOptions = options;
    m_app.sources().setState(sourceId, SourceState::Streaming);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("source"), sourceId},
        {QStringLiteral("url"), url},
        {QStringLiteral("packets_before"), QVariant::fromValue<qlonglong>(oldPackets)},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::sourceList(const Intent &intent)
{
    Q_UNUSED(intent);
    QVariantList rows;
    for (int id : m_app.sources().ids())
    {
        const DataSourceInfo *info = m_app.sources().get(id);
        if (!info)
            continue;
        QVariantMap m{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), info->name},
            {QStringLiteral("origin"), info->origin},
            {QStringLiteral("kind"), int(info->kind)},
            {QStringLiteral("state"), int(info->state)},
            {QStringLiteral("datasets"), info->datasetIds.size()},
            {QStringLiteral("parsers"), info->parsers.size()},
            {QStringLiteral("bytes_read"), QVariant::fromValue<qlonglong>(info->bytesRead)},
            {QStringLiteral("error"), info->error},
        };
        if (auto it = m_live.find(id); it != m_live.end())
        {
            const double secs = qMax(1e-6, double(it->second.since.elapsed()) / 1000.0);
            m.insert(QStringLiteral("live"), true);
            m.insert(QStringLiteral("hz"), double(it->second.packets) / secs);
            m.insert(QStringLiteral("kb_s"), double(it->second.bytes) / secs / 1024.0);
        }
        rows.append(m);
    }
    IntentResult r;
    r.payload = rows;
    return r;
}

void DataCommandsHandler::enqueueLive(int sourceId, const QByteArray &bytes)
{
    auto it = m_live.find(sourceId);
    if (it == m_live.end())
        return;
    LiveSession &s = it->second;
    s.bytes += bytes.size();
    s.packets += 1; // 口径：一次可读/一报文算一包
    if (s.paused)
    {
        // 暂停期不喂也不攒：直接丢，恢复后从新数据继续（明确取舍，不补帧）。
        s.droppedBytes += bytes.size();
        return;
    }
    s.buf.append(bytes);
    if (s.buf.size() > s.bufCap)
    {
        const qint64 over = s.buf.size() - s.bufCap;
        s.buf.remove(0, int(over));
        s.droppedBytes += over; // 背压：环满丢最旧
    }
}

void DataCommandsHandler::pumpLive()
{
    for (auto &kv : m_live)
    {
        LiveSession &s = kv.second;
        if (s.paused || s.buf.isEmpty())
            continue;
        const int take = int(std::min<qint64>(s.pumpBudget, s.buf.size()));
        // 录制与解析吃同一块字节（顺序：先录后解析，录的是"进来的原样"）。
        writeRecordBytes(kv.first, QByteArray(s.buf.constData(), take),
                         quint64(s.since.nsecsElapsed()));
        m_app.ingest().feed(kv.first, reinterpret_cast<const uint8_t *>(s.buf.constData()),
                            take);
        s.buf.remove(0, take);
        s.fedBytes += take;
    }
}

bool DataCommandsHandler::writeRecordBytes(int sourceId, const QByteArray &bytes, quint64 timeNs)
{
    bool any = false;
    for (RecordSession &rs : m_records)
    {
        if (rs.sourceId != sourceId || !rs.recorder || !rs.recorder->isOpen())
            continue;
        QString err;
        // raw 只保真字节；mcap 需要 topic + 时间戳（相对会话起点）。
        const bool ok = rs.formatId == QLatin1String("raw")
                            ? rs.recorder->writeBytes(bytes, &err)
                            : rs.recorder->writeMessage(rs.topic.isEmpty() ? QStringLiteral("bytes")
                                                                           : rs.topic,
                                                        bytes, timeNs, &err);
        if (ok)
        {
            rs.bytes += bytes.size();
            any = true;
        }
    }
    return any;
}

Result<IntentResult> DataCommandsHandler::recordStart(const Intent &intent)
{
    int sourceId = intent.params.value(QStringLiteral("source"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    const DataSourceInfo *info = m_app.sources().get(sourceId);
    if (!info)
        return fail(err::NoSource, QStringLiteral("数据源不存在: %1").arg(sourceId));

    QString formatId = intent.params.value(QStringLiteral("format")).toString();
    if (formatId.isEmpty())
        formatId = QStringLiteral("raw");
    QString backendId = intent.params.value(QStringLiteral("backend")).toString();
    if (backendId.isEmpty())
        backendId = QStringLiteral("file");
    QString endpoint = intent.params.value(QStringLiteral("endpoint")).toString();
    if (endpoint.isEmpty())
    {
        // 缺省落在当前目录：<源名>-<时间戳>.<格式后缀>。
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString base = info->name.isEmpty() ? QStringLiteral("live") : info->name;
        const QString suffix = formatId == QLatin1String("mcap") ? QStringLiteral("mcap")
                                                                 : QStringLiteral("raw");
        endpoint = QStringLiteral("%1-%2.%3").arg(base, stamp, suffix);
    }

    for (const RecordSession &rs : m_records)
        if (rs.sourceId == sourceId && rs.formatId == formatId && rs.backendId == backendId)
            return fail(err::State, QStringLiteral("该源已在用 %1/%2 录制").arg(formatId, backendId));

    // mcap 自包含：把"这段字节该用哪个解析器、什么模板"写进 Schema 记录。
    // 没有它，录下来的 mcap 回放时只是一袋字节（AC-07 的"读回行数"就过不了）。
    QByteArray schemaJson;
    if (formatId == QLatin1String("mcap"))
    {
        const auto it = m_live.find(sourceId);
        if (it != m_live.end())
        {
            const LiveSession &s = it->second;
            QJsonObject root;
            root.insert(QStringLiteral("parser"), s.parserId);
            const QJsonDocument od = QJsonDocument::fromJson(s.parserOptions);
            root.insert(QStringLiteral("options"), od.isObject() ? od.object() : QJsonObject());
            schemaJson = QJsonDocument(root).toJson(QJsonDocument::Compact);
        }
    }

    QString why;
    auto created = rec::Recorder::create(formatId, backendId, endpoint, schemaJson, &why);
    if (!created)
        return fail(err::Io, why.isEmpty() ? QStringLiteral("录制创建失败") : why);

    RecordSession rs;
    rs.sourceId = sourceId;
    rs.formatId = formatId;
    rs.backendId = backendId;
    rs.endpoint = endpoint;
    rs.topic = info->name.isEmpty() ? QStringLiteral("source") : info->name;
    rs.recorder = created.value();
    rs.since.start();
    m_records.push_back(rs);

    // 回写到源的录制配置（工作区保存/UI 展示都以它为准）。
    DataSourceInfo copy = *info;
    RecordingTarget target;
    target.formatId = formatId;
    target.backendId = backendId;
    target.endpoint = endpoint;
    copy.recordings.push_back(target);
    m_app.sources().update(sourceId, copy);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("source"), sourceId},
        {QStringLiteral("format"), formatId},
        {QStringLiteral("backend"), backendId},
        {QStringLiteral("endpoint"), endpoint},
    };
    return r;
}

Result<IntentResult> DataCommandsHandler::recordStop(const Intent &intent)
{
    int sourceId = intent.params.value(QStringLiteral("source"), -1).toInt();
    if (sourceId < 0)
        sourceId = m_app.activeSourceId();
    const QString formatId = intent.params.value(QStringLiteral("format")).toString();

    QVariantList stopped;
    for (auto it = m_records.begin(); it != m_records.end();)
    {
        if (it->sourceId != sourceId || (!formatId.isEmpty() && it->formatId != formatId))
        {
            ++it;
            continue;
        }
        QString err;
        if (it->recorder)
            it->recorder->close(&err);
        stopped.append(QVariantMap{
            {QStringLiteral("source"), it->sourceId},
            {QStringLiteral("format"), it->formatId},
            {QStringLiteral("backend"), it->backendId},
            {QStringLiteral("endpoint"), it->endpoint},
            {QStringLiteral("bytes"), QVariant::fromValue<qlonglong>(it->bytes)},
            {QStringLiteral("error"), err},
        });
        it = m_records.erase(it);
    }
    if (stopped.isEmpty())
        return fail(err::NoEnt, QStringLiteral("该源没有正在进行的录制"));

    // 清掉源上的录制配置（关了就不要再显示"录制中"）。
    if (const DataSourceInfo *info = m_app.sources().get(sourceId))
    {
        DataSourceInfo copy = *info;
        copy.recordings.clear();
        m_app.sources().update(sourceId, copy);
    }

    IntentResult r;
    r.payload = stopped;
    return r;
}

Result<IntentResult> DataCommandsHandler::recordList(const Intent &intent)
{
    const int only = intent.params.value(QStringLiteral("source"), -1).toInt();
    QVariantList rows;
    for (const RecordSession &rs : m_records)
    {
        if (only >= 0 && rs.sourceId != only)
            continue;
        rows.append(QVariantMap{
            {QStringLiteral("source"), rs.sourceId},
            {QStringLiteral("format"), rs.formatId},
            {QStringLiteral("backend"), rs.backendId},
            {QStringLiteral("endpoint"), rs.endpoint},
            {QStringLiteral("bytes"), QVariant::fromValue<qlonglong>(rs.bytes)},
            {QStringLiteral("open"), rs.recorder && rs.recorder->isOpen()},
            {QStringLiteral("uptime_s"), double(rs.since.elapsed()) / 1000.0},
        });
    }
    IntentResult r;
    r.payload = rows;
    return r;
}

Result<IntentResult> DataCommandsHandler::setRate(const Intent &intent)
{
    // 泵频率：收包与解析之间的节流阀。hz 越大越"跟手"，越小越省 CPU。
    const int hz = intent.params.value(QStringLiteral("hz"), -1).toInt();
    if (hz < 1 || hz > 200)
        return fail(err::Args, QStringLiteral("hz 取值范围 1..200，收到 %1").arg(hz));
    m_pumpHz = hz;
    if (m_pump)
        m_pump->setInterval(1000 / m_pumpHz);
    IntentResult r;
    r.payload = QVariantMap{{QStringLiteral("hz"), m_pumpHz},
                            {QStringLiteral("interval_ms"), 1000 / m_pumpHz}};
    return r;
}

Result<IntentResult> DataCommandsHandler::sourceConfig(const Intent &intent)
{
    // 单源环参数：bufferBytes（环上限）/ pumpBudget（每 tick 喂多少）。
    // 不带 source 时改全部会话；只读时传 key 为空。
    const int only = intent.params.value(QStringLiteral("source"), -1).toInt();
    const QString key = intent.params.value(QStringLiteral("key")).toString();
    const qint64 value = intent.params.value(QStringLiteral("value"), -1).toLongLong();
    if (!key.isEmpty() && value < 0)
        return fail(err::Args, QStringLiteral("缺少 value"));

    int touched = 0;
    for (auto &kv : m_live)
    {
        if (only >= 0 && kv.first != only)
            continue;
        LiveSession &s = kv.second;
        if (key == QLatin1String("bufferBytes") && value > 0)
        {
            s.bufCap = qMax<qint64>(64 * 1024, value);
            if (s.buf.size() > s.bufCap)
            {
                const qint64 over = s.buf.size() - s.bufCap;
                s.buf.remove(0, int(over));
                s.droppedBytes += over;
            }
        }
        else if (key == QLatin1String("pumpBudget") && value > 0)
        {
            s.pumpBudget = qMax<qint64>(1024, value);
        }
        ++touched;
    }
    if (only >= 0 && touched == 0)
        return fail(err::NoEnt, QStringLiteral("没有这个 live 会话: %1").arg(only));

    QVariantList rows;
    for (const auto &kv : m_live)
    {
        if (only >= 0 && kv.first != only)
            continue;
        rows.append(QVariantMap{
            {QStringLiteral("source"), kv.first},
            {QStringLiteral("url"), kv.second.url},
            {QStringLiteral("bufferBytes"), QVariant::fromValue<qlonglong>(kv.second.bufCap)},
            {QStringLiteral("pumpBudget"), QVariant::fromValue<qlonglong>(kv.second.pumpBudget)},
            {QStringLiteral("pending_bytes"), QVariant::fromValue<qlonglong>(kv.second.buf.size())},
        });
    }
    IntentResult r;
    r.payload = QVariantMap{{QStringLiteral("pump_hz"), m_pumpHz},
                            {QStringLiteral("sources"), rows}};
    return r;
}

Result<IntentResult> DataCommandsHandler::liveStats(const Intent &intent)
{
    // 速率口径：整套会话的均值，不做滑动窗——先把"能看见"补齐，
    // 滑动窗（1s/5s）属于 T1.3 的细化，等有真实流再定窗口。
    const int only = intent.params.value(QStringLiteral("id"), -1).toInt();
    QVariantList rows;
    for (const auto &kv : m_live)
    {
        if (only >= 0 && kv.first != only)
            continue;
        const LiveSession &s = kv.second;
        const double secs = qMax(1e-6, double(s.since.elapsed()) / 1000.0);
        QVariantMap m{
            {QStringLiteral("id"), kv.first},
            {QStringLiteral("packets"), QVariant::fromValue<qlonglong>(s.packets)},
            {QStringLiteral("bytes"), QVariant::fromValue<qlonglong>(s.bytes)},
            {QStringLiteral("uptime_s"), secs},
            {QStringLiteral("hz"), double(s.packets) / secs},
            {QStringLiteral("kb_s"), double(s.bytes) / secs / 1024.0},
            {QStringLiteral("paused"), s.paused},
            {QStringLiteral("pending_bytes"), QVariant::fromValue<qlonglong>(s.buf.size())},
            {QStringLiteral("dropped_bytes"), QVariant::fromValue<qlonglong>(s.droppedBytes)},
            {QStringLiteral("fed_bytes"), QVariant::fromValue<qlonglong>(s.fedBytes)},
            {QStringLiteral("backpressure"), s.buf.size() >= s.bufCap},
            {QStringLiteral("pump_hz"), m_pumpHz},
            {QStringLiteral("parse_errors"), [this, id = kv.first] {
                 int n = 0;
                 for (const auto &ch : m_app.ingest().channels(id))
                     if (!ch.error.isEmpty())
                         ++n;
                 return n;
             }()},
        };
        if (const DataSourceInfo *info = m_app.sources().get(kv.first))
        {
            m.insert(QStringLiteral("name"), info->name);
            m.insert(QStringLiteral("state"), int(info->state));
            m.insert(QStringLiteral("error"), info->error);
            m.insert(QStringLiteral("origin"), info->origin);
        }
        rows.append(m);
    }
    IntentResult r;
    r.payload = rows;
    return r;
}

Result<IntentResult> DataCommandsHandler::alignTime(const Intent &intent)
{
    // 对齐策略（T3.2）。语义统一为"只改出口偏移"：
    //   base   → 本源成为时间基准，offset 归零并把它设为活动源
    //   start  → 本源起点对齐到基准源起点
    //   end    → 本源终点对齐到基准源终点
    //   center → 两端中点对齐
    //   reset  → 偏移归零（原始时间从未被改写过，因此等价于逐点还原）
    const QString mode = intent.params.value(QStringLiteral("mode")).toString();
    static const QStringList kModes{QStringLiteral("base"), QStringLiteral("start"),
                                    QStringLiteral("end"), QStringLiteral("center"),
                                    QStringLiteral("reset")};
    if (!kModes.contains(mode))
        return fail(err::Args, QStringLiteral("mode 须为 base/start/end/center/reset 之一"));

    int srcId = intent.params.value(QStringLiteral("source"), -1).toInt();
    if (srcId < 0)
        srcId = m_app.activeSourceId();
    const DataSourceInfo *srcInfo = m_app.sources().get(srcId);
    if (!srcInfo || srcInfo->datasetIds.isEmpty())
        return fail(err::NoSource, QStringLiteral("该数据源没有可对齐的数据集"));

    // 源范围必须是"原始"范围：offset 是相对量，拿叠加后的范围去算会自我累积。
    auto rangeOf = [this](int sourceId) -> std::optional<std::pair<TimeUs, TimeUs>> {
        const DataSourceInfo *info = m_app.sources().get(sourceId);
        if (!info)
            return std::nullopt;
        std::optional<TimeUs> min;
        std::optional<TimeUs> max;
        for (int dsId : info->datasetIds)
        {
            Dataset *ds = m_app.data().getMutable(dsId);
            if (!ds)
                continue;
            const auto r = ds->rawTimeRangeUs();
            if (!r)
                continue;
            if (!min || r->first < *min)
                min = r->first;
            if (!max || r->second > *max)
                max = r->second;
        }
        if (!min || !max)
            return std::nullopt;
        return std::make_pair(*min, *max);
    };

    const auto srcRange = rangeOf(srcId);
    if (!srcRange)
        return fail(err::State, QStringLiteral("该数据源没有时间数据（表为空）"));

    TimeUs offset = 0;
    int baseId = srcId;
    if (mode == QLatin1String("base"))
    {
        m_app.setActiveSourceId(srcId);
    }
    else if (mode == QLatin1String("reset"))
    {
        // offset=0，不需要基准
    }
    else
    {
        baseId = intent.params.value(QStringLiteral("base"), -1).toInt();
        if (baseId < 0)
            baseId = m_app.activeSourceId();
        if (baseId == srcId)
            return fail(err::Args, QStringLiteral("对齐对象与基准是同一个数据源，请指定 base"));
        const auto baseRange = rangeOf(baseId);
        if (!baseRange)
            return fail(err::NoSource, QStringLiteral("基准数据源没有时间数据"));

        if (mode == QLatin1String("start"))
            offset = baseRange->first - srcRange->first;
        else if (mode == QLatin1String("end"))
            offset = baseRange->second - srcRange->second;
        else // center：中点对齐，整除不丢符号。
        {
            const TimeUs srcMid = srcRange->first + (srcRange->second - srcRange->first) / 2;
            const TimeUs baseMid = baseRange->first + (baseRange->second - baseRange->first) / 2;
            offset = baseMid - srcMid;
        }
    }

    // 偏移的唯一真值在宿主态 AppContext（画布订阅 sourceTimeOffsetChanged 重推 x）。
    // Dataset 只存原始时间戳，不存偏移——两处都存会双算，这是刻意收敛到一条路径。
    m_app.setSourceTimeOffset(srcId, offset / 1000000.0);

    IntentResult r;
    r.payload = QVariantMap{
        {QStringLiteral("source"), srcId},
        {QStringLiteral("base"), baseId},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("offset_us"), QVariant::fromValue<qlonglong>(offset)},
        {QStringLiteral("raw_min_us"), QVariant::fromValue<qlonglong>(srcRange->first)},
        {QStringLiteral("raw_max_us"), QVariant::fromValue<qlonglong>(srcRange->second)},
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
        return fail(err::Args,
                    QStringLiteral("data.derive 需要 dataset/table/column/filter 参数"));

    // const 取数：派生只读，不碰仓库里的 Dataset——原数据零改动。
    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    Result<QString> tableRes = resolveTable(*ds, table);
    if (!tableRes)
        return tableRes.error();

    const Table *t = ds->table(table);
    if (!t || !t->column(column))
        return fail(err::Chan, QStringLiteral("通道不存在: %1").arg(input));

    // params 允许调用方给 JSON 字符串或 QVariantMap（脚本/Job 场景）。
    QByteArray paramsJson;
    const QVariant pv = intent.params.value(QStringLiteral("params"));
    if (pv.type() == QVariant::String)
        paramsJson = pv.toString().toUtf8();
    else if (pv.isValid() && !pv.toMap().isEmpty())
        paramsJson = QJsonDocument(QJsonObject::fromVariantMap(pv.toMap())).toJson(QJsonDocument::Compact);

    // 派生重算上限（M4/T4.4）：intent 的 cap > settings 的 derive.maxPoints > 默认。
    //   0 = 不限（取表的全量行，最多 kMaxDerivedPoints 的硬保护）；
    //   以前 `cap <= 0` 被悄悄重置成上限，"0 表示不限"根本没法表达。
    constexpr int kMaxDerivedPoints = 200000; // 画布/脚本够用，防全量派生灌爆 UI 内存
    int cap = kMaxDerivedPoints;
    if (intent.params.contains(QStringLiteral("cap")))
        cap = intent.params.value(QStringLiteral("cap")).toInt();
    else if (m_app.settings().contains(QStringLiteral("derive.maxPoints")))
        cap = m_app.settings().value(QStringLiteral("derive.maxPoints")).toInt();
    if (cap < 0)
        cap = kMaxDerivedPoints;
    if (cap == 0)
    {
        const Table *dt = ds ? ds->table(table) : nullptr;
        cap = dt && dt->rows > 0 ? qMin(dt->rows, kMaxDerivedPoints) : kMaxDerivedPoints;
    }

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
            return fail(err::Table, QStringLiteral("表不存在: %1").arg(table));
        auto raw = ds->channel(table, column, cap); // 输入也按同一上限取，否则派生点数还是被 4096 卡住
        if (!raw)
            return fail(err::Chan, QStringLiteral("读取通道失败: %1").arg(input));
        const int take = qMin(raw->size(), cap);
        rawT.reserve(take);
        rawV.reserve(take);
        for (int i = 0; i < take; ++i)
        {
            rawT.append(raw->time.value(i));
            rawV.append(raw->value.value(i));
        }

        if (!m_app.interactions().hasTransformEvaluator())
            return fail(err::NoEval, QStringLiteral("变换求值器未注册（script 模块未装载）"));
        Result<QVector<double>> ev{QVector<double>()};
        m_app.interactions().evalTransform(req, CapDerived, rawT, rawV, ev);
        if (!ev)
            return fail(ev.error().code, ev.error().message);

        QVector<double> time = rawT;
        QVector<double> value = ev.value();
        const int n = value.size();
        if (n <= 0)
            return fail(err::Derive, QStringLiteral("变换结果为空"));
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
        return fail(err::Derive, err.isEmpty() ? QStringLiteral("派生失败") : err);

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
        return fail(err::Args,
                    QStringLiteral("data.filter 需要 dataset/table/column/filter 参数"));

    if (!m_app.interactions().hasFilterOps())
        return fail(err::NoFilterOps, QStringLiteral("过滤器能力未注册（script 模块未装载）"));

    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    auto channel = ds->channel(table, column);
    if (!channel)
        return fail(err::Chan, QStringLiteral("通道不存在: %1").arg(input));

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
        return fail(err::Filter, QStringLiteral("过滤器 %1 结果为空或长度不一致").arg(filterId));

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
        return fail(err::Args,
                    QStringLiteral("data.marker 需要 dataset/table/column/code 参数"));

    if (!m_app.interactions().hasMarkerOps())
        return fail(err::NoMarkerOps, QStringLiteral("Marker 规则能力未注册（script 模块未装载）"));

    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    auto channel = ds->channel(table, column);
    if (!channel)
        return fail(err::Chan, QStringLiteral("通道不存在: %1").arg(input));

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
    Result<int> dsIdRes = resolveDataset(intent);
    if (!dsIdRes)
        return dsIdRes.error();
    const int datasetId = dsIdRes.value();
    const Dataset *ds = m_app.data().get(datasetId);

    const QString column = intent.params.value(QStringLiteral("column")).toString();
    if (column.isEmpty())
        return fail(err::Args, QStringLiteral("data.series 需要 column 参数"));

    // 派生名穿透（独立派生字段）：column 命中派生注册表 → 直接返回缓存结果。
    // （table 不参与派生定位，故 table 兜底/校验放在穿透之后）
    // 数据集需匹配（防跨数据集撞名）；datasetId 缺省由第一个数据集兜底的
    // 场景下，若派生属于其他数据集则视为不命中（不穿帮）。
    {
        const auto *entry = m_app.derivedRegistry().get(column);
        if (entry && entry->datasetId == datasetId)
        {
            const int n = qMin(entry->t.size(), entry->v.size());
            if (n <= 0)
                return fail(err::Empty,
                            QStringLiteral("派生序列无样本: %1").arg(column));
            auto [ts, vs] = toVariantLists(entry->t, entry->v);
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

    Result<QString> tableRes = resolveTable(*ds, intent.params.value(QStringLiteral("table")).toString());
    if (!tableRes)
        return tableRes.error();
    const QString table = tableRes.value();

    const Table *t = ds->table(table);
    if (!t || !t->column(column))
        return fail(err::Chan,
                    QStringLiteral("通道不存在: %1.%2").arg(table, column));

    // cap 默认 4096（有界读取口径，与 plot.render 无 px 路径一致），可调但不超过
    // 内核不抽稀上限（对齐 kStateMaxPoints 的量级，防止一次意图拖走整列百万行）。
    int limit = intent.params.value(QStringLiteral("limit"), 4096).toInt();
    limit = qBound(1, limit, 200000);

    auto channel = ds->channel(table, column, limit);
    if (!channel)
        return fail(err::Chan,
                    QStringLiteral("读取通道失败: %1.%2").arg(table, column));
    const int n = channel->size();
    if (n <= 0)
        return fail(err::Empty, QStringLiteral("通道无样本"));

    // 通道长度可能大于 time/value 各自容量（流式半行）：按短边截断。
    auto [ts, vs] = toVariantLists(channel->time, channel->value);

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
            return fail(err::NoEval,
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
        return fail(err::Args,
                    QStringLiteral("data.state 需要 dataset/table/column 参数"));

    const Dataset *ds = m_app.data().get(dsId);
    if (!ds)
        return fail(err::NoEnt, QStringLiteral("数据集不存在: %1").arg(dsId));
    const QString input = table + QLatin1Char('.') + column;
    const Table *t = ds->table(table);
    const Column *col = t ? t->column(column) : nullptr;
    if (!t || !col)
        return fail(err::Chan, QStringLiteral("通道不存在: %1").arg(input));
    // 状态视图只接受离散值族：bool / 整型 / 仅含整数值的浮点（对齐 PJ4 的
    // PrimitiveType 离散家族 + whole_numbers_only 约束）。Text 通道的 string
    // 状态读取依赖 kernel string ABI（暂缺），此处明确报错而非静默空白。
    if (col->type == ValueType::Text)
        return fail(err::Type,
                    QStringLiteral("string 状态读取依赖内核 string ABI（暂未实现）: %1").arg(input));

    auto channel = ds->channel(table, column, kStateMaxPoints);
    if (!channel)
        return fail(err::Chan, QStringLiteral("读取通道失败: %1").arg(input));
    const int n = channel->size();
    if (n <= 0)
        return fail(err::Empty, QStringLiteral("通道无样本: %1").arg(input));

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
                return fail(err::State,
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
        return fail(err::State,
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
        return fail(err::Io, err);
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
        return fail(err::Args,
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
            return fail(err::NoSource,
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
            return fail(err::Args,
                        QStringLiteral("transform JSON 解析失败: %1").arg(perr.errorString()));
    }
    else if (tv.isValid())
    {
        req = QJsonObject::fromVariantMap(tv.toMap());
    }
    if (!req.contains(QStringLiteral("descriptorId")))
        return fail(err::Args,
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
        return fail(err::Io, err);

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
        return fail(err::Args,
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
            return fail(err::NoSource,
                        QStringLiteral("无法定位数据源（给 origin 或有效 dataset）"));
        origin = info->origin;
    }

    ChannelTransformConfig cfg = ChannelTransformConfig::load(m_app.settings());
    const bool removed = cfg.removeByChannel(origin, table, column);
    QString err;
    if (!cfg.save(m_app.settings(), &err))
        return fail(err::Io, err);

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

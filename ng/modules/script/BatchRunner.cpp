#include "BatchRunner.h"

#include "Dataset.h"
#include "DatasetRegistry.h"
#include "ErrorCodes.h"
#include "InteractionRegistry.h"
#include "LuaEngine.h"
#include "ScriptFacade.h"
#include "SourceRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantMap>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::script
{

namespace
{
// 与 CLI/gui_main 的 makeLoadIntent 同规则：按扩展名选格式知识包。
QString formatForPath(const QString &path)
{
    return path.endsWith(QStringLiteral(".ulg"), Qt::CaseInsensitive)
        ? QStringLiteral("ulog")
        : QStringLiteral("csv");
}

// Lua 字符串字面量安全包装（路径里可能有引号/反斜杠）。
QString luaStr(const QString &s)
{
    QString escaped = s;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    escaped.replace(QLatin1Char('"'), QLatin1String("\\\""));
    escaped.replace(QLatin1Char('\n'), QLatin1String("\\n"));
    return QStringLiteral("\"%1\"").arg(escaped);
}

// pattern 参数："*.csv *.ulg" / "*.csv;*.ulg" / 单个 "*.csv" 都接受。
QStringList splitPatterns(const QString &raw)
{
    QStringList out;
    const QRegularExpression sep(QStringLiteral("[\\s;]+"));
    for (const QString &piece : raw.split(sep, Qt::SkipEmptyParts))
    {
        const QString p = piece.trimmed();
        if (!p.isEmpty())
            out << p;
    }
    return out.isEmpty() ? QStringList{QStringLiteral("*.csv"), QStringLiteral("*.ulg")}
                         : out;
}
} // namespace

BatchHandler::BatchHandler(IIntentExecutor &executor, InteractionRegistry *inter,
                           dataset::DatasetRegistry *data, dataset::SourceRegistry *sources,
                           ViewDispatcher viewDispatcher, QObject *parent)
    : QObject(parent), m_executor(executor), m_inter(inter), m_data(data), m_sources(sources),
      m_viewDispatcher(std::move(viewDispatcher))
{
    setObjectName(QStringLiteral("batch-handler"));
}

Result<IntentResult> BatchHandler::execute(const Intent &intent)
{
    if (m_running.exchange(true))
        return fail(err::Args, QStringLiteral("批量运行已在进行中"));
    // 所有退出路径都要复位 running（含提前 return 的参数错误）。
    struct RunningGuard
    {
        std::atomic<bool> &flag;
        ~RunningGuard() { flag.store(false); }
    } guard{m_running};
    m_cancel.store(false);

    // ---- 参数 ----
    const QString dir = intent.params.value(QStringLiteral("dir")).toString();
    const QDir d(dir);
    if (dir.isEmpty() || !d.exists())
        return fail(err::NoEnt, QStringLiteral("目录不存在: %1").arg(dir));

    // 脚本：source 内联 或 path 文件（与 script.run 同语义）。
    QByteArray source;
    QString scriptLabel = QStringLiteral("(内联)");
    if (intent.params.contains(QStringLiteral("source")))
    {
        source = intent.params.value(QStringLiteral("source")).toByteArray();
    }
    else if (intent.params.contains(QStringLiteral("script")))
    {
        const QString path = intent.params.value(QStringLiteral("script")).toString();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return fail(err::Io, QStringLiteral("无法读取脚本: %1").arg(path));
        source = f.readAll();
        scriptLabel = QFileInfo(path).fileName();
    }
    else
    {
        return fail(err::Args, QStringLiteral("data.batch.run 需要 script 或 source"));
    }
    if (source.trimmed().isEmpty())
        return fail(err::Args, QStringLiteral("批量脚本为空"));

    // ---- 文件收集（默认 *.csv *.ulg；recursive 可选）----
    const QStringList patterns =
        splitPatterns(intent.params.value(QStringLiteral("pattern")).toString());
    const bool recursive = intent.params.value(QStringLiteral("recursive")).toBool();
    QFileInfoList files;
    QDirIterator it(dir, patterns, QDir::Files,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext())
        files << QFileInfo(it.next());
    std::sort(files.begin(), files.end(),
              [](const QFileInfo &a, const QFileInfo &b) { return a.filePath() < b.filePath(); });
    if (files.isEmpty())
        return fail(err::NoEnt,
                    QStringLiteral("目录下没有匹配 %1 的文件: %2")
                        .arg(patterns.join(QLatin1Char(' ')), dir));

    const bool keep = intent.params.value(QStringLiteral("keep")).toBool();
    const int total = files.size();
    int okCount = 0;
    int failCount = 0;
    bool cancelled = false;
    QVariantList rows;

    for (int i = 0; i < total; ++i)
    {
        const QFileInfo &fi = files.at(i);
        const QString filePath = fi.absoluteFilePath();

        QVariantMap row;
        row.insert(QStringLiteral("file"), filePath);
        row.insert(QStringLiteral("ok"), false);

        // ---- 1) 加载：按扩展名选格式，与 CLI/GUI 打开同一条 data.load 路径 ----
        Intent load;
        load.op = QStringLiteral("data.load");
        load.params.insert(QStringLiteral("path"), filePath);
        load.params.insert(QStringLiteral("format"), formatForPath(filePath));
        Result<IntentResult> lr = m_executor.execute(load);
        // 空解析判定：摄取管道对垃圾文件宽容（返回 0 表数据集），
        // 批量场景里"0 表"就是损坏/格式不符——判 load 失败，明细可追。
        DatasetId createdId = kInvalidDataset; // load 实际产生的数据集（无论成败，收尾移除）
        if (lr)
            createdId = lr.value().payload.toInt();
        if (lr && m_data)
        {
            const Dataset *probe = m_data->get(createdId);
            if (!probe || probe->tableCount() == 0)
            {
                lr = fail(err::Data,
                          QStringLiteral("解析结果为空（文件损坏或格式不符）"));
            }
        }
        if (!lr)
        {
            row.insert(QStringLiteral("stage"), QStringLiteral("load"));
            row.insert(QStringLiteral("error"), lr.error().message);
            ++failCount;
        }
        else
        {
            const DatasetId dsId = createdId;
            // ---- 2) 脚本：每文件独立 VM（干净全局态）；batch 表注入文件上下文 ----
            ScriptFacade facade(m_executor, nullptr, m_viewDispatcher);
            LuaEngine lua(facade);
            const QString prelude = QStringLiteral("batch = { file = %1, dataset = %2 }\n")
                                        .arg(luaStr(filePath))
                                        .arg(dsId);
            Result<void> pr = lua.runString(prelude.toUtf8(), QStringLiteral("=batch-prelude"));
            if (!pr)
            {
                row.insert(QStringLiteral("stage"), QStringLiteral("script"));
                row.insert(QStringLiteral("error"), pr.error().message);
                ++failCount;
            }
            else if (Result<QVariant> sr = lua.runStringResult(
                         source, QStringLiteral("@batch:%1").arg(fi.fileName()));
                     !sr)
            {
                row.insert(QStringLiteral("stage"), QStringLiteral("script"));
                row.insert(QStringLiteral("error"), sr.error().message);
                ++failCount;
            }
            else
            {
                row.insert(QStringLiteral("ok"), true);
                const QVariant metrics = sr.value();
                if (metrics.typeId() == QMetaType::QVariantMap && !metrics.toMap().isEmpty())
                    row.insert(QStringLiteral("metrics"), metrics.toMap());
                ++okCount;
            }
        }

        // ---- 3) 收尾：默认移除数据集（100 个文件不堆积内存；keep=true 保留）。
        // 覆盖所有产生数据集的路径（成功/脚本失败/空解析失败）。
        // 注意 id 空间：data.remove 收**源 id**；数据集 id → 源 id 经注册表反查。
        if (!keep && createdId != kInvalidDataset)
        {
            const int srcId = m_sources ? m_sources->sourceOfDataset(createdId) : -1;
            if (srcId >= 0)
            {
                Intent rm;
                rm.op = QStringLiteral("data.remove");
                rm.params.insert(QStringLiteral("id"), srcId);
                (void)m_executor.execute(rm);
            }
        }

        rows.append(row);

        // ---- 4) 进度上报（GUI 面板消费；headless 无处理器，静默）----
        if (m_inter)
        {
            QVariantMap prog;
            prog.insert(QStringLiteral("done"), i + 1);
            prog.insert(QStringLiteral("total"), total);
            prog.insert(QStringLiteral("file"), filePath);
            prog.insert(QStringLiteral("ok"), row.value(QStringLiteral("ok")).toBool());
            if (row.contains(QStringLiteral("stage")))
            {
                prog.insert(QStringLiteral("stage"), row.value(QStringLiteral("stage")));
                prog.insert(QStringLiteral("error"), row.value(QStringLiteral("error")));
            }
            if (row.contains(QStringLiteral("metrics")))
                prog.insert(QStringLiteral("metrics"), row.value(QStringLiteral("metrics")));
            Result<QVariant> out{QVariant()};
            (void)m_inter->dispatchViewAction(QStringLiteral("data.batch.progress"), prog, out);
        }

        // ---- 5) 事件循环分片：进度 UI 刷新、取消请求在此进入 ----
        if (QCoreApplication::instance())
            QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (m_cancel.load())
        {
            cancelled = true;
            break;
        }
    }

    QVariantMap payload;
    payload.insert(QStringLiteral("dir"), dir);
    payload.insert(QStringLiteral("script"), scriptLabel);
    payload.insert(QStringLiteral("total"), total);
    payload.insert(QStringLiteral("ok"), okCount);
    payload.insert(QStringLiteral("failed"), failCount);
    payload.insert(QStringLiteral("cancelled"), cancelled);
    payload.insert(QStringLiteral("rows"), rows);

    IntentResult out;
    out.payload = payload;
    return out;
}

} // namespace nextpilot::plotscope::script

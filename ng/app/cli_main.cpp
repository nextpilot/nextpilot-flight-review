// PlotScope NG — headless CLI。验证新架构：统一数据模型 + 模块装载 + 意图执行。
// GUI 点击、脚本调用、Job 回放三条路径共用同一个执行器（意图是公共货币）。
#include <type_traits>
#include "App.h"
#include "Intent.h"
#include "Dataset.h"
#include "mcp_bridge.h"
#include "diagnostics.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTextStream>

#include <cstdio>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::mcp;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

static void printUsage()
{
    fprintf(stdout,
            "用法:\n"
            "  plotscope-ng --list-modules      # 器官模块\n"
            "  plotscope-ng --list-packs        # 工具箱里的知识包（装载/卸载单位）\n"
            "  plotscope-ng --list-contributions [kind]  # 贡献（按 kind×领域，开放类型）\n"
            "  plotscope-ng --list-formats      # 工具箱里的格式驱动（list-contributions format 别名）\n"
            "  plotscope-ng --list-slots        # UI 插槽及工具箱填入的入口\n"
            "  plotscope-ng --kernel            # Rust kernel ABI/版本/自检\n"
            "  plotscope-ng --info <file.csv|file.ulg>   # 按扩展名选格式\n"
            "  plotscope-ng --run <job.json>\n"
            "  plotscope-ng --script <file.lua> [--record <out.json>]\n"
            "  plotscope-ng --batch <dir> <script.lua> [--pattern \"*.csv;*.ulg\"] [--json out.json]\n"
            "  plotscope-ng --mcp-bridge        # MCP server（stdio JSON-RPC，供外部代理驱动）\n"
            "  plotscope-ng --dump-diagnostics  # 应用诊断 JSON（模块/知识包/数据面/工具目录）\n");
}

static Intent makeLoadIntent(const QString &path)
{
    // 按扩展名选格式知识包；新格式只需在这里加一行，扩展自身不动宿主。
    const QString format = path.endsWith(QStringLiteral(".ulg"), Qt::CaseInsensitive)
        ? QStringLiteral("ulog")
        : QStringLiteral("csv");
    Intent intent;
    intent.op = QStringLiteral("data.load");
    intent.params[QStringLiteral("path")] = path;
    intent.params[QStringLiteral("format")] = format;
    return intent;
}

static Intent makeScriptIntent(const QString &path)
{
    Intent intent;
    intent.op = QStringLiteral("script.run");
    intent.params[QStringLiteral("path")] = path;
    return intent;
}

static bool writeJob(const QString &path, const Job &job)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    f.write(QJsonDocument(job.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PlotScope NG"));

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2)
    {
        printUsage();
        return 0;
    }

    // MCP stdio 桥：stdout 是协议通道，任何提示打印都必须让路。
    // 必须在 App 构造之前设置（模块 initialize 在构造时已接好事件）。
    if (args.at(1) == QStringLiteral("--mcp-bridge"))
        qputenv("PLOTSCONG_QUIET_STDOUT", "1");

    App application;
    const auto errors = application.startup(/*headless=*/true);
    for (auto it = errors.constBegin(); it != errors.constEnd(); ++it)
        fprintf(stderr, "[skip] %s: %s\n", qPrintable(it.key()), qPrintable(it.value()));

    const QString command = args.at(1);

    if (command == QStringLiteral("--kernel"))
    {
        fprintf(stdout, "Rust kernel: version=%s abi=%u (需要 %u) selftest=%d\n",
                ps_kernel_version(), ps_abi_version(), PS_ABI_VERSION, ps_abi_selftest());
        return ps_abi_version() == PS_ABI_VERSION && ps_abi_selftest() == 0 ? 0 : 1;
    }

    if (command == QStringLiteral("--dump-diagnostics"))
    {
        // 诊断模式抑制模块提示，保证 stdout 是纯 JSON。
        qputenv("PLOTSCONG_QUIET_STDOUT", "1");
        // startup 已在上方完成（模块/知识包已装载）；此处快照即终态。
        const QJsonDocument doc(diag::collect(application));
        fprintf(stdout, "%s\n", QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).toUtf8().constData());
        return 0;
    }

    if (command == QStringLiteral("--list-modules"))
    {
        for (const auto &info : application.host().availableModules())
        {
            const char *kind = info.kind == nextpilot::plotscope::host::ModuleKind::Builtin ? "builtin" : "extension";
            fprintf(stdout, "%-12s %-10s %s\n", qPrintable(info.id), kind,
                    qPrintable(info.provides.join(QLatin1Char(','))));
        }
        return 0;
    }

    if (command == QStringLiteral("--list-packs"))
    {
        // 知识包是装载/卸载的原子单位：每包聚合任意 kind 的贡献。
        const auto &toolbox = application.host().extensions();
        for (const auto &pack : toolbox.packs.values())
        {
            fprintf(stdout, "%-18s %-12s [%s]\n",
                    qPrintable(pack.id), qPrintable(pack.name),
                    qPrintable(QString::fromLatin1(nextpilot::plotscope::registry::domainKey(pack.domain))));
        }
        return 0;
    }

    if (command == QStringLiteral("--list-contributions"))
    {
        const auto &toolbox = application.host().extensions();
        const QString filter = args.size() >= 3 ? args.at(2) : QString();
        namespace reg = nextpilot::plotscope::registry;
        // 内容全是数据：各类型表逐一列出（类型即身份，无 kind 字符串索引）。
        const auto displayNameOf = [](const auto &c) -> const QString & {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, reg::DocEntry> || std::is_same_v<T, reg::ExampleEntry>)
                return c.title;
            else
                return c.label;
        };
        const auto listTable = [&](const char *kind, const char *cn, auto &table) {
            if (!filter.isEmpty() && filter != QLatin1String(kind))
                return;
            if (table.count() == 0)
                return;
            fprintf(stdout, "[%s / %s] %d 项\n", kind, cn, table.count());
            for (const auto &c : table.values())
                fprintf(stdout, "  %-26s %-18s %-10s <- %s\n",
                        qPrintable(c.id), qPrintable(displayNameOf(c)),
                        qPrintable(QString::fromLatin1(reg::domainKey(c.domain))),
                        qPrintable(c.provider));
        };
        listTable("ui", "界面", toolbox.uiActions);
        listTable("panel", "面板", toolbox.panels);
        listTable("script", "脚本", toolbox.scripts);
        listTable("data", "数据", toolbox.dataAssets);
        listTable("example", "示例", toolbox.examples);
        listTable("doc", "文档", toolbox.docs);
        if (filter.isEmpty() || filter == QLatin1String("parser"))
        {
            const auto list = toolbox.parsers();
            if (!list.isEmpty())
            {
                fprintf(stdout, "[parser / 解析器] %d 项\n", list.size());
                for (reg::IMessageParser *p : list)
                    fprintf(stdout, "  %-26s %-18s %-10s <- %s\n",
                            qPrintable(p->id()), qPrintable(p->displayName()),
                            qPrintable(QString::fromLatin1(reg::domainKey(p->domain()))),
                            qPrintable(p->provider()));
            }
        }
        return 0;
    }

    if (command == QStringLiteral("--list-slots"))
    {
        // 器官/骨架声明插槽，工具箱的 UI 贡献填内容。
        const auto &slotReg = application.host().uiSlots();
        for (const nextpilot::plotscope::registry::UiSlot &slot : slotReg.allSlots())
        {
            const auto actions = slotReg.actionsIn(slot.area);
            fprintf(stdout, "[%s] %s (%s) <- %d 项\n",
                    qPrintable(slot.area), qPrintable(slot.label),
                    qPrintable(slot.owner), actions.size());
            for (const nextpilot::plotscope::registry::BoundUiAction &b : actions)
                fprintf(stdout, "    %-24s %-14s op=%s (来自 %s)\n",
                        qPrintable(b.action.id), qPrintable(b.action.label),
                        qPrintable(b.action.intentOp.isEmpty() ? QStringLiteral("-") : b.action.intentOp),
                        qPrintable(b.provider));
        }
        return 0;
    }

    if (command == QStringLiteral("--list-formats"))
    {
        // 工具箱按业务领域分组展示（通用 / 飞控 / 汽车 / 机器人 / 卫导 / 视频）。
        const auto &toolbox = application.host().extensions();
        const nextpilot::plotscope::registry::Domain order[] = {
            nextpilot::plotscope::registry::Domain::General, nextpilot::plotscope::registry::Domain::Autopilot,
            nextpilot::plotscope::registry::Domain::Automotive, nextpilot::plotscope::registry::Domain::Robotics,
            nextpilot::plotscope::registry::Domain::Gnss, nextpilot::plotscope::registry::Domain::Video
        };
        for (auto domain : order)
        {
            const auto drivers = toolbox.driversByDomain(domain);
            if (drivers.isEmpty())
                continue;
            fprintf(stdout, "[%s]\n", qPrintable(nextpilot::plotscope::registry::domainName(domain)));
            for (nextpilot::plotscope::parser::IMessageParser *driver : drivers)
                fprintf(stdout, "  %-10s %-18s 后缀[%s]\n",
                        qPrintable(driver->id()),
                        qPrintable(driver->displayName()),
                        qPrintable(driver->suffixes().join(QLatin1Char(','))));
        }
        return 0;
    }

    if (command == QStringLiteral("--info") && args.size() >= 3)
    {
        Result<IntentResult> r = application.host().execute(makeLoadIntent(args.at(2)));
        if (!r)
        {
            fprintf(stderr, "错误[%s]: %s\n", qPrintable(r.error().code), qPrintable(r.error().message));
            return 1;
        }
        const DatasetId id = r.value().payload.toInt();
        const Dataset *ds = application.data().get(id);
        fprintf(stdout, "数据集 #%d: %s [%s]\n", id, qPrintable(ds->name()), qPrintable(ds->format()));
        fprintf(stdout, "表 (%d): %s\n", ds->tableCount(), qPrintable(ds->tableNames().join(QLatin1Char(','))));
        for (int ti = 0; ti < ds->tableCount(); ++ti)
        {
            const Table *t = ds->table(ti);
            QStringList cols;
            for (const Column &c : t->columns)
                cols << QStringLiteral("%1(%2)").arg(c.name, QLatin1String(valueTypeName(c.type)));
            fprintf(stdout, "  表 %s: %d 行, 列 [%s]\n", qPrintable(t->name), t->rowCount(),
                    qPrintable(cols.join(QLatin1Char(','))));
        }
        fprintf(stdout, "时间范围: %.3f .. %.3f 秒\n", ds->timeMin(), ds->timeMax());
        return 0;
    }

    if (command == QStringLiteral("--run") && args.size() >= 3)
    {
        QFile f(args.at(2));
        if (!f.open(QIODevice::ReadOnly))
        {
            fprintf(stderr, "无法读取 Job: %s\n", qPrintable(args.at(2)));
            return 2;
        }
        Result<void> parseError;
        Job job = Job::fromJson(QJsonDocument::fromJson(f.readAll()).object(), &parseError);
        if (!parseError)
        {
            fprintf(stderr, "Job 解析失败[%s]: %s\n", qPrintable(parseError.error().code),
                    qPrintable(parseError.error().message));
            return 2;
        }

        for (int i = 0; i < job.size(); ++i)
        {
            const Intent &intent = job.at(i);
            Result<IntentResult> r = application.host().execute(intent);
            if (!r)
            {
                fprintf(stderr, "意图 %d (%s) 失败[%s]: %s\n", i, qPrintable(intent.op),
                        qPrintable(r.error().code), qPrintable(r.error().message));
                return 1;
            }
            if (intent.op == QStringLiteral("plot.render"))
            {
                const QJsonDocument doc(QJsonObject::fromVariantMap(r.value().payload.toMap()));
                fprintf(stdout, "PlotSpec: %s\n", qPrintable(doc.toJson(QJsonDocument::Compact)));
            }
            else if (intent.op == QStringLiteral("data.load"))
            {
                fprintf(stdout, "loaded dataset #%d\n", r.value().payload.toInt());
            }
        }
        return 0;
    }

    if (command == QStringLiteral("--script") && args.size() >= 3)
    {
        // --record <out.json> 是可选修饰参数：把脚本产生的业务意图录制成 Job。
        QString recordPath;
        for (int i = 2; i + 1 < args.size(); ++i)
            if (args.at(i) == QStringLiteral("--record"))
                recordPath = args.at(i + 1);

        if (!recordPath.isEmpty())
        {
            application.recorder().clear();
            application.recorder().setRecording(true);
        }

        Result<IntentResult> r = application.host().execute(makeScriptIntent(args.at(2)));
        if (!r)
        {
            fprintf(stderr, "脚本执行失败[%s]: %s\n", qPrintable(r.error().code), qPrintable(r.error().message));
            return 1;
        }
        fprintf(stdout, "脚本执行完成\n");

        if (!recordPath.isEmpty())
        {
            application.recorder().setRecording(false);
            if (!writeJob(recordPath, application.recorder().toJob()))
            {
                fprintf(stderr, "无法写入录制 Job: %s\n", qPrintable(recordPath));
                return 2;
            }
            fprintf(stdout, "已录制 %d 条意图 -> %s\n", application.recorder().size(), qPrintable(recordPath));
        }
        return 0;
    }

    if (command == QStringLiteral("--batch") && args.size() >= 4)
    {
        // --batch <dir> <script.lua> [--pattern "*.csv;*.ulg"] [--json out.json]
        // M2.4 headless JSON 出口：汇总 payload 直接序列化打印/落盘。
        const QString dir = args.at(2);
        const QString script = args.at(3);
        QString pattern;
        QString jsonOut;
        for (int i = 4; i + 1 < args.size(); ++i)
        {
            if (args.at(i) == QStringLiteral("--pattern"))
                pattern = args.at(i + 1);
            if (args.at(i) == QStringLiteral("--json"))
                jsonOut = args.at(i + 1);
        }

        Intent bi;
        bi.op = QStringLiteral("data.batch.run");
        bi.params[QStringLiteral("dir")] = dir;
        bi.params[QStringLiteral("script")] = script;
        if (!pattern.isEmpty())
            bi.params[QStringLiteral("pattern")] = pattern;

        Result<IntentResult> r = application.host().execute(bi);
        if (!r)
        {
            fprintf(stderr, "批量执行失败[%s]: %s\n",
                    qPrintable(r.error().code), qPrintable(r.error().message));
            return 1;
        }
        const QJsonDocument doc(QJsonObject::fromVariantMap(r.value().payload.toMap()));
        if (!jsonOut.isEmpty())
        {
            QFile out(jsonOut);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                fprintf(stderr, "无法写入汇总 JSON: %s\n", qPrintable(jsonOut));
                return 2;
            }
            out.write(doc.toJson(QJsonDocument::Indented));
            fprintf(stdout, "汇总已写入 %s（%s 共 %d 个文件：%s）\n",
                    qPrintable(jsonOut), qPrintable(dir),
                    r.value().payload.toMap().value(QStringLiteral("total")).toInt(),
                    qPrintable(QStringLiteral("%1 成功 / %2 失败")
                                   .arg(r.value().payload.toMap()
                                            .value(QStringLiteral("ok")).toInt())
                                   .arg(r.value().payload.toMap()
                                            .value(QStringLiteral("failed")).toInt())));
        }
        else
        {
            fprintf(stdout, "%s\n",
                    qPrintable(QString::fromUtf8(doc.toJson(QJsonDocument::Indented))));
        }
        return 0;
    }

    if (command == QStringLiteral("--mcp-bridge"))
    {
        // MCP over stdio：newline-delimited JSON-RPC 2.0。
        // 每行一个请求 → 每请求一行响应（notification 不回）；stdin EOF 退出。
        // 协议语义在 mcp_bridge.cpp（纯函数，另有单测）；这里只做传输。
        fprintf(stderr, "[mcp-bridge] MCP server ready on stdio\n");
        QTextStream in(stdin);
        while (!in.atEnd())
        {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty())
                continue;
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
            QJsonObject resp;
            if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            {
                resp = errorResponse(QJsonValue::Null, -32700,
                                     QStringLiteral("JSON 解析失败: %1").arg(parseError.errorString()));
            }
            else
            {
                resp = handleRequest(doc.object(), application);
            }
            if (resp.isEmpty())
                continue; // notification：无响应
            fprintf(stdout, "%s\n", qPrintable(QString::fromUtf8(QJsonDocument(resp).toJson(QJsonDocument::Compact))));
            fflush(stdout); // 代理按行读，必须立即冲刷
        }
        return 0;
    }

    printUsage();
    return 0;
}

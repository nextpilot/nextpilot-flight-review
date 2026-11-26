// PlotScope — headless CLI。验证新架构：统一数据模型 + 模块装载 + 意图执行。
// GUI 点击、脚本调用、Job 回放三条路径共用同一个执行器（意图是公共货币）。
#include <type_traits>
#include "../core/App.h"
#include "Intent.h"
#include "Dataset.h"
#include "../mcp/mcp_bridge.h"
#include "../diag/diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::mcp;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;

namespace {
// qPrintable 走系统 ANSI，和源码里的 UTF-8 字面量混流会乱码，这里统一按 UTF-8 输出。
// 注意：不能 return s.toUtf8().constData()——临时 QByteArray 在表达式结束即析构，
// 同一 fprintf 里多次调用 u8() 时前一次的指针会被后一次的临时 buffer 覆盖，造成
// 字段错位（--contributions 一行有 4~5 个 u8 时实测复现）。用轮转的持久 buffer，
// 每行最多 5 个 u8 调用，8 槽足够且生命周期覆盖到 fprintf 返回。
const char *u8(const QString &s)
{
    static thread_local QByteArray u8Buffers[8];
    static thread_local int u8Cursor = 0;
    QByteArray &buf = u8Buffers[u8Cursor++ % 8];
    buf = s.toUtf8();
    return buf.constData();
}
}
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

static void printUsage()
{
    fprintf(stdout,
            "用法:\n"
            "  plotscope --list-modules      # 模块模块\n"
            "  plotscope --list-packs        # 工具箱里的扩展包（装载/卸载单位）\n"
            "  plotscope --list-contributions [kind]  # 贡献（按 kind×领域，开放类型）\n"
            "  plotscope --contributions      # A8b vscode contributes：owner/key/id/order 逐行\n"
            "  plotscope --list-parser      # 工具箱里的格式驱动（list-contributions format 别名）\n"
            "  plotscope --list-slots        # UI 插槽及工具箱填入的入口\n"
            "  plotscope --kernel            # Rust kernel ABI/版本/自检\n"
            "  plotscope --info <file.csv|file.ulg>   # 按扩展名选格式\n"
            "  plotscope --run <job.json>\n"
            "  plotscope --script <file.lua> [--record <out.json>]\n"
            "  plotscope --batch <dir> <script.lua> [--pattern \"*.csv;*.ulg\"] [--json out.json]\n"
            "  plotscope --mcp-bridge        # MCP server（stdio JSON-RPC，供外部代理驱动）\n"
            "  plotscope --dump-diagnostics  # 应用诊断 JSON（模块/扩展包/数据面/工具目录）\n"
            "\n"
            "全局选项:\n"
            "  --settings <path>   # 设置文件路径，缺省 <AppData>/plotscope/settings.json\n");
}

static Intent makeLoadIntent(const QString &path)
{
    // 按扩展名选格式扩展包；新格式只需在这里加一行，扩展自身不动宿主。
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

namespace {
// CLI 是短命进程：settings 只在内存里改，退出前不 flush 就丢，GUI 侧读不到 CLI 写的键。
struct SettingsFlushGuard
{
    explicit SettingsFlushGuard(App &application) : m_application(application) {}
    ~SettingsFlushGuard() { m_application.context().settings().flush(); }
    App &m_application;
};
} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
    // 控制台代码页默认 936，切到 UTF-8 才能正常显示中文
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("plotscope"));

    QStringList args = QCoreApplication::arguments();

    // --settings 与 GUI 同名同义，位置自由，先剥离后命令索引才是 --xxx 本身。
    QString settingsPath;
    for (int i = 1; i + 1 < args.size(); ++i)
    {
        if (args.at(i) != QStringLiteral("--settings"))
            continue;
        settingsPath = args.at(i + 1);
        args.removeAt(i + 1);
        args.removeAt(i);
        --i;
    }
    if (settingsPath.isEmpty())
    {
        // 与 gui_main.cpp 同款：AppDataLocation 已含 applicationName，缺目录先建。
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!dir.isEmpty())
        {
            QDir().mkpath(dir);
            settingsPath = dir + QStringLiteral("/settings.json");
        }
    }

    if (args.size() < 2)
    {
        printUsage();
        return 0;
    }

    // MCP stdio 桥：stdout 是协议通道，任何提示打印都必须让路。
    // 必须在 App 构造之前设置（模块 initialize 在构造时已接好事件）。
    if (args.at(1) == QStringLiteral("--mcp-bridge"))
        qputenv("PLOTSCOPE_QUIET_STDOUT", "1");

    App application(settingsPath);
    SettingsFlushGuard flushGuard(application);
    const auto errors = application.startup(/*headless=*/true);
    for (auto it = errors.constBegin(); it != errors.constEnd(); ++it)
        fprintf(stderr, "[skip] %s: %s\n", u8(it.key()), u8(it.value()));

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
        qputenv("PLOTSCOPE_QUIET_STDOUT", "1");
        // startup 已在上方完成（模块/扩展包已装载）；此处快照即终态。
        const QJsonDocument doc(diag::collect(application));
        fprintf(stdout, "%s\n", QString::fromUtf8(doc.toJson(QJsonDocument::Indented)).toUtf8().constData());
        return 0;
    }

    if (command == QStringLiteral("--contributions"))
    {
        // A8b-M1：只列 vscode contributes 契约表（owner/key/id/order），
        // 纯读扩展注册表的 contributes 贡献面，不链 GUI / 业务数据。
        const auto &extensions = application.context().toolbox();
        for (const auto &c : extensions.contributedCommands())
            fprintf(stdout, "%s commands %s order=%d\n",
                    u8(extensions.ownerOfCommand(c.command)), u8(c.command), c.order);
        for (const auto &e : extensions.paletteEntries())
            fprintf(stdout, "%s menus.commandPalette %s group=%s when=%s\n",
                    u8(e.owner), u8(e.item.command),
                    u8(e.item.group.isEmpty() ? QStringLiteral("-") : e.item.group),
                    u8(e.item.when.isEmpty() ? QStringLiteral("-") : e.item.when));
        // A8b-M2：其余 contributes 键同样是纯声明数据，按 owner/key/id 输出，
        // 供 headless 验收「扩展一个视图 → 各 place 可见」。不渲染、不解释业务。
        for (const auto &c : extensions.contributedViewContainers())
            fprintf(stdout, "%s viewsContainers %s area=%s order=%d\n",
                    u8(c.owner), u8(c.item.id), u8(c.item.area), c.item.order);
        for (const auto &v : extensions.contributedViews())
            fprintf(stdout, "%s views %s container=%s order=%d\n",
                    u8(v.owner), u8(v.item.id), u8(v.item.container), v.item.order);
        for (const auto &m : extensions.contributedMenuItems())
            fprintf(stdout, "%s menus.%s %s group=%s when=%s\n",
                    u8(m.owner), u8(m.item.place), u8(m.item.command),
                    u8(m.item.group.isEmpty() ? QStringLiteral("-") : m.item.group),
                    u8(m.item.when.isEmpty() ? QStringLiteral("-") : m.item.when));
        for (const auto &c : extensions.contributedColors())
            fprintf(stdout, "%s colors %s dark=%s light=%s\n",
                    u8(c.owner), u8(c.item.id), u8(c.item.dark), u8(c.item.light));
        for (const auto &s : extensions.contributedStatuses())
            fprintf(stdout, "%s statusbar %s side=%s priority=%d\n",
                    u8(s.owner), u8(s.item.id), u8(s.item.side), s.item.priority);
        // A8b-M3-a：customEditors 声明输出（owner / viewType / selIdx / 条件，
        // 与「owner/key/id/order」约定对齐——裁定 52442925 建议）。
        for (const auto &e : extensions.contributedCustomEditors())
        {
            for (int si = 0; si < e.item.selector.size(); ++si)
            {
                const auto &sel = e.item.selector.at(si);
                const QString cond = !sel.mime.isEmpty() ? QStringLiteral("mime=%1").arg(sel.mime)
                                                         : QStringLiteral("pattern=%1").arg(sel.filenamePattern);
                fprintf(stdout, "%s customEditors %s %d %s\n",
                        u8(e.owner), u8(e.item.viewType), si, u8(cond));
            }
        }
        for (const auto &d : extensions.contributionDiagnostics())
            fprintf(stdout, "diagnostic %s %s %s\n", u8(d.owner), u8(d.keyPath), u8(d.message));
        return 0;
    }

    if (command == QStringLiteral("--list-modules"))
    {
        for (const auto &info : application.host().availableModules())
        {
            fprintf(stdout, "%-16s %s\n", u8(info.id),
                    u8(info.provides.join(QLatin1Char(','))));
        }
        return 0;
    }

    if (command == QStringLiteral("--list-packs"))
    {
        // 扩展包是装载/卸载的原子单位：每包聚合任意 kind 的贡献。
        const auto &toolbox = application.host().toolbox();
        for (const auto &pack : toolbox.packs.values())
        {
            fprintf(stdout, "%-18s %-12s [%s]\n",
                    u8(pack.id), u8(pack.name),
                    u8(QString::fromLatin1(nextpilot::plotscope::registry::domainKey(pack.domain))));
        }
        return 0;
    }

    if (command == QStringLiteral("--list-contributions"))
    {
        const auto &toolbox = application.host().toolbox();
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
                        u8(c.id), u8(displayNameOf(c)),
                        u8(QString::fromLatin1(reg::domainKey(c.domain))),
                        u8(c.provider));
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
                fprintf(stdout, "[parser / 解析器] %d 项\n", static_cast<int>(list.size()));
                for (reg::IMessageParser *p : list)
                    fprintf(stdout, "  %-26s %-18s %-10s <- %s\n",
                            u8(p->id()), u8(p->displayName()),
                            u8(QString::fromLatin1(reg::domainKey(p->domain()))),
                            u8(p->provider()));
            }
        }
        return 0;
    }

    if (command == QStringLiteral("--list-slots"))
    {
        // 模块/骨架声明插槽，工具箱的 UI 贡献填内容。
        const auto &slotReg = application.host().uiSlots();
        for (const nextpilot::plotscope::registry::UiSlot &slot : slotReg.allSlots())
        {
            const auto actions = slotReg.actionsIn(slot.slotId);
            fprintf(stdout, "[%s] %s (%s) <- %d 项\n",
                    u8(slot.slotId), u8(slot.label),
                    u8(slot.owner), static_cast<int>(actions.size()));
            for (const nextpilot::plotscope::registry::BoundUiAction &b : actions)
                fprintf(stdout, "    %-24s %-14s op=%s (来自 %s)\n",
                        u8(b.action.id), u8(b.action.label),
                        u8(b.action.intentOp.isEmpty() ? QStringLiteral("-") : b.action.intentOp),
                        u8(b.provider));
        }
        return 0;
    }

    if (command == QStringLiteral("--list-parser"))
    {
        // 工具箱按业务领域分组展示（通用 / 飞控 / 汽车 / 机器人 / 卫导 / 视频）。
        const auto &toolbox = application.host().toolbox();
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
            fprintf(stdout, "[%s]\n", u8(nextpilot::plotscope::registry::domainName(domain)));
            for (nextpilot::plotscope::parser::IMessageParser *driver : drivers)
                fprintf(stdout, "  %-10s %-18s 后缀[%s]\n",
                        u8(driver->id()),
                        u8(driver->displayName()),
                        u8(driver->suffixes().join(QLatin1Char(','))));
        }
        return 0;
    }

    if (command == QStringLiteral("--info") && args.size() >= 3)
    {
        Result<IntentResult> r = application.host().execute(makeLoadIntent(args.at(2)));
        if (!r)
        {
            fprintf(stderr, "error[%s]: %s\n", u8(r.error().code), u8(r.error().message));
            return 1;
        }
        const DatasetId id = r.value().payload.toInt();
        const Dataset *ds = application.data().get(id);
        fprintf(stdout, "数据集 #%d: %s [%s]\n", id, u8(ds->name()), u8(ds->format()));
        fprintf(stdout, "表 (%d): %s\n", ds->tableCount(), u8(ds->tableNames().join(QLatin1Char(','))));
        for (int ti = 0; ti < ds->tableCount(); ++ti)
        {
            const Table *t = ds->table(ti);
            QStringList cols;
            for (const Column &c : t->columns)
                cols << QStringLiteral("%1(%2)").arg(c.name, QLatin1String(valueTypeName(c.type)));
            fprintf(stdout, "  表 %s: %d 行, 列 [%s]\n", u8(t->name), t->rowCount(),
                    u8(cols.join(QLatin1Char(','))));
        }
        fprintf(stdout, "时间范围: %.3f .. %.3f 秒\n", ds->timeMin(), ds->timeMax());
        return 0;
    }

    if (command == QStringLiteral("--run") && args.size() >= 3)
    {
        QFile f(args.at(2));
        if (!f.open(QIODevice::ReadOnly))
        {
            fprintf(stderr, "cannot read job file: %s\n", u8(args.at(2)));
            return 2;
        }
        Result<void> parseError;
        Job job = Job::fromJson(QJsonDocument::fromJson(f.readAll()).object(), &parseError);
        if (!parseError)
        {
            fprintf(stderr, "job parse failed[%s]: %s\n", u8(parseError.error().code),
                    u8(parseError.error().message));
            return 2;
        }

        for (int i = 0; i < job.size(); ++i)
        {
            const Intent &intent = job.at(i);
            Result<IntentResult> r = application.host().execute(intent);
            if (!r)
            {
                fprintf(stderr, "intent %d (%s) failed[%s]: %s\n", i, u8(intent.op),
                        u8(r.error().code), u8(r.error().message));
                return 1;
            }
            if (intent.op == QStringLiteral("plot.render"))
            {
                const QJsonDocument doc(QJsonObject::fromVariantMap(r.value().payload.toMap()));
                fprintf(stdout, "PlotSpec: %s\n", u8(doc.toJson(QJsonDocument::Compact)));
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
            fprintf(stderr, "script run failed[%s]: %s\n", u8(r.error().code), u8(r.error().message));
            return 1;
        }
        fprintf(stdout, "脚本执行完成\n");

        if (!recordPath.isEmpty())
        {
            application.recorder().setRecording(false);
            if (!writeJob(recordPath, application.recorder().toJob()))
            {
                fprintf(stderr, "cannot write recorded job: %s\n", u8(recordPath));
                return 2;
            }
            fprintf(stdout, "已录制 %d 条意图 -> %s\n", application.recorder().size(), u8(recordPath));
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
            fprintf(stderr, "batch run failed[%s]: %s\n",
                    u8(r.error().code), u8(r.error().message));
            return 1;
        }
        const QJsonDocument doc(QJsonObject::fromVariantMap(r.value().payload.toMap()));
        if (!jsonOut.isEmpty())
        {
            QFile out(jsonOut);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                fprintf(stderr, "cannot write summary json: %s\n", u8(jsonOut));
                return 2;
            }
            out.write(doc.toJson(QJsonDocument::Indented));
            fprintf(stdout, "汇总已写入 %s（%s 共 %d 个文件：%s）\n",
                    u8(jsonOut), u8(dir),
                    r.value().payload.toMap().value(QStringLiteral("total")).toInt(),
                    u8(QStringLiteral("%1 成功 / %2 失败")
                                   .arg(r.value().payload.toMap()
                                            .value(QStringLiteral("ok")).toInt())
                                   .arg(r.value().payload.toMap()
                                            .value(QStringLiteral("failed")).toInt())));
        }
        else
        {
            fprintf(stdout, "%s\n",
                    u8(QString::fromUtf8(doc.toJson(QJsonDocument::Indented))));
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
            fprintf(stdout, "%s\n", u8(QString::fromUtf8(QJsonDocument(resp).toJson(QJsonDocument::Compact))));
            fflush(stdout); // 代理按行读，必须立即冲刷
        }
        return 0;
    }

    printUsage();
    return 0;
}

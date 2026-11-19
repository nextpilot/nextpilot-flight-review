// PlotScope NG — headless CLI。验证新架构：统一数据模型 + 模块装载 + 意图执行。
// GUI 点击、脚本调用、Job 回放三条路径共用同一个执行器（意图是公共货币）。
#include "App.h"
#include "Intent.h"
#include "Dataset.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

#include <cstdio>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::core;

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
            "  plotscope-ng --script <file.lua> [--record <out.json>]\n");
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
        for (const QString &packId : toolbox.packIds())
        {
            nextpilot::plotscope::core::IToolPack *pack = toolbox.pack(packId);
            if (!pack)
                continue;
            int n = static_cast<int>(pack->contributions().size());
            fprintf(stdout, "%-18s %-12s [%s] %d 项贡献\n",
                    qPrintable(pack->id()), qPrintable(pack->displayName()),
                    qPrintable(QString::fromLatin1(nextpilot::plotscope::core::domainKey(pack->domain()))), n);
        }
        return 0;
    }

    if (command == QStringLiteral("--list-contributions"))
    {
        const auto &toolbox = application.host().extensions();
        const QString filter = args.size() >= 3 ? args.at(2) : QString();
        // 稳定顺序：按已知 kind 顺序，再按领域顺序。
        for (const QString &kind : nextpilot::plotscope::core::knownKinds())
        {
            if (!filter.isEmpty() && filter != kind)
                continue;
            const auto list = toolbox.contributions(kind);
            if (list.isEmpty())
                continue;
            fprintf(stdout, "[%s / %s] %d 项\n",
                    qPrintable(kind), qPrintable(nextpilot::plotscope::core::kindName(kind)), list.size());
            for (nextpilot::plotscope::core::IToolContribution *c : list)
                fprintf(stdout, "  %-26s %-18s %-10s <- %s\n",
                        qPrintable(c->id()), qPrintable(c->displayName()),
                        qPrintable(QString::fromLatin1(nextpilot::plotscope::core::domainKey(c->domain()))),
                        qPrintable(c->provider()));
        }
        return 0;
    }

    if (command == QStringLiteral("--list-slots"))
    {
        // 器官/骨架声明插槽，工具箱的 UI 贡献填内容。
        const auto &slotReg = application.host().uiSlots();
        for (const nextpilot::plotscope::core::UiSlot &slot : slotReg.allSlots())
        {
            const auto actions = slotReg.actionsIn(slot.area);
            fprintf(stdout, "[%s] %s (%s) <- %d 项\n",
                    qPrintable(slot.area), qPrintable(slot.label),
                    qPrintable(slot.owner), actions.size());
            for (const nextpilot::plotscope::core::BoundUiAction &b : actions)
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
        const nextpilot::plotscope::core::Domain order[] = {
            nextpilot::plotscope::core::Domain::General, nextpilot::plotscope::core::Domain::Autopilot,
            nextpilot::plotscope::core::Domain::Automotive, nextpilot::plotscope::core::Domain::Robotics,
            nextpilot::plotscope::core::Domain::Gnss, nextpilot::plotscope::core::Domain::Video
        };
        for (auto domain : order)
        {
            const auto drivers = toolbox.driversByDomain(domain);
            if (drivers.isEmpty())
                continue;
            fprintf(stdout, "[%s]\n", qPrintable(nextpilot::plotscope::core::domainName(domain)));
            for (nextpilot::plotscope::core::IMessageParser *driver : drivers)
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

    printUsage();
    return 0;
}

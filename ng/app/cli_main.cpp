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

using namespace ng::app;
using namespace ng::core;

static void printUsage()
{
    fprintf(stdout,
            "用法:\n"
            "  plotscope-ng --list-modules\n"
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
        for (const auto &info : application.modules().availableModules())
        {
            const char *kind = info.kind == ng::host::ModuleKind::Builtin ? "builtin" : "extension";
            fprintf(stdout, "%-12s %-10s %s\n", qPrintable(info.id), kind,
                    qPrintable(info.provides.join(QLatin1Char(','))));
        }
        return 0;
    }

    if (command == QStringLiteral("--info") && args.size() >= 3)
    {
        Result<IntentResult> r = application.modules().execute(makeLoadIntent(args.at(2)));
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
            Result<IntentResult> r = application.modules().execute(intent);
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

        Result<IntentResult> r = application.modules().execute(makeScriptIntent(args.at(2)));
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

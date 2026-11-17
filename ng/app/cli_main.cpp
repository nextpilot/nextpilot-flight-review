// PlotScope NG — headless CLI。验证新架构：统一数据模型 + 模块装载 + 意图执行。
#include "App.h"
#include "Intent.h"

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
            "  plotscope-ng --info <file.csv>\n"
            "  plotscope-ng --run <job.json>\n");
}

static Intent makeLoadIntent(const QString &path)
{
    Intent intent;
    intent.op = QStringLiteral("data.load");
    intent.params[QStringLiteral("path")] = path;
    intent.params[QStringLiteral("format")] = QStringLiteral("csv");
    return intent;
}

static Intent makeRenderIntent(DatasetId id)
{
    Intent intent;
    intent.op = QStringLiteral("plot.render");
    intent.params[QStringLiteral("dataset")] = id;
    return intent;
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

    if (command == QStringLiteral("--list-modules"))
    {
        for (const auto &info : application.modules().availableModules())
        {
            const char *kind = info.kind == ng::host::ModuleKind::Builtin ? "builtin" : "extension";
            fprintf(stdout, "%-12s %-10s %s\n", qPrintable(info.id), kind, qPrintable(info.provides.join(QLatin1Char(','))));
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
            // 若意图产出了 PlotSpec，打印它——这就是 headless 的可复现产物。
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

    printUsage();
    return 0;
}

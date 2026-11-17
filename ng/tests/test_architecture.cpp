#include "App.h"
#include "Intent.h"
#include "DataRepository.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>

#include <cassert>
#include <cstdio>
#include <QTemporaryDir>

using namespace ng::app;
using namespace ng::core;

static QString writeCsv(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("demo.csv"));
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&f);
    out << "time,roll,pitch\n"
        << "0,1.0,2.0\n"
        << "1,1.5,2.5\n"
        << "2,2.0,3.0\n";
    f.close();
    return path;
}

static Intent loadIntent(const QString &path)
{
    Intent i;
    i.op = QStringLiteral("data.load");
    i.params[QStringLiteral("path")] = path;
    i.params[QStringLiteral("format")] = QStringLiteral("csv");
    return i;
}

static Intent renderIntent(DatasetId id)
{
    Intent i;
    i.op = QStringLiteral("plot.render");
    i.params[QStringLiteral("dataset")] = id;
    return i;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString csv = writeCsv(dir);

    // 1) 装载成功后三个模块都激活
    App application;
    const auto errors = application.startup(true);
    assert(errors.isEmpty());
    assert(application.modules().activeIds().size() == 3);
    fprintf(stdout, "[ok] 三个模块按依赖顺序激活\n");

    // 2) 意图执行：CSV 扩展产出 Dataset
    Result<IntentResult> loaded = application.modules().execute(loadIntent(csv));
    assert(loaded);
    const DatasetId id = loaded.value().payload.toInt();
    assert(application.data().get(id) != nullptr);
    assert(application.data().get(id)->seriesCount() == 2);
    fprintf(stdout, "[ok] 意图 data.load 产出 Dataset，2 个通道\n");

    // 3) 意图执行：plot 模块编译出 PlotSpec
    Result<IntentResult> rendered = application.modules().execute(renderIntent(id));
    assert(rendered);
    const QVariantMap spec = rendered.value().payload.toMap();
    assert(spec.value(QStringLiteral("type")).toString() == QStringLiteral("timeseries"));
    assert(spec.value(QStringLiteral("channel")).toString() == QStringLiteral("roll"));
    fprintf(stdout, "[ok] 意图 plot.render 产出 PlotSpec\n");

    // 4) 卸载隔离：关掉绘图模块后，CSV 仍能加载，数据不消失
    assert(application.modules().deactivate(QStringLiteral("mod.plot")));
    assert(!application.modules().isActive(QStringLiteral("mod.plot")));
    Result<IntentResult> stillLoads = application.modules().execute(loadIntent(csv));
    assert(stillLoads);
    assert(application.data().get(id) != nullptr);
    fprintf(stdout, "[ok] 卸载 plot 模块后 CSV 仍可用，数据未丢失\n");

    // 5) 关掉 CSV 扩展后，data.load 不再可执行（能力随模块撤销）
    assert(application.modules().deactivate(QStringLiteral("ext.csv")));
    Result<IntentResult> noHandler = application.modules().execute(loadIntent(csv));
    assert(!noHandler);
    assert(noHandler.error().code == QStringLiteral("eunsupported"));
    fprintf(stdout, "[ok] 卸载 CSV 扩展后能力被精确撤销\n");

    // 6) Job 序列化往返
    Job job;
    job.append(loadIntent(csv));
    job.append(renderIntent(id));
    Result<void> parseErr;
    Job restored = Job::fromJson(job.toJson(), &parseErr);
    assert(parseErr);
    assert(restored.size() == 2);
    fprintf(stdout, "[ok] Job JSON 序列化往返\n");

    fprintf(stdout, "\n全部架构契约测试通过\n");
    return 0;
}

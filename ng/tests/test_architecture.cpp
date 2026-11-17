#include "App.h"
#include "Intent.h"
#include "DataRepository.h"
#include "Dataset.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <cassert>
#include <cstdio>

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

static Intent renderIntent(DatasetId id, const QString &channel)
{
    Intent i;
    i.op = QStringLiteral("plot.render");
    i.params[QStringLiteral("dataset")] = id;
    if (!channel.isEmpty())
        i.params[QStringLiteral("channel")] = channel;
    return i;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString csv = writeCsv(dir);

    // 1) 装载成功后四个模块都激活
    App application;
    const auto errors = application.startup(true);
    assert(errors.isEmpty());
    assert(application.modules().activeIds().size() == 4);
    fprintf(stdout, "[ok] 四个模块按依赖顺序激活（含脚本模块）\n");

    // 2) 意图执行：CSV 扩展产出 Dataset（一张 CSV = 一张表）
    Result<IntentResult> loaded = application.modules().execute(loadIntent(csv));
    assert(loaded);
    const DatasetId id = loaded.value().payload.toInt();
    const Dataset *ds = application.data().get(id);
    assert(ds != nullptr);

    // 2a) 多表模型：一张表，表内两列 typed 列，共享 3 行时间轴
    assert(ds->tableCount() == 1);
    const Table *t = ds->table(0);
    assert(t->rowCount() == 3);
    assert(t->columns.size() == 2);
    assert(t->column(QStringLiteral("roll")) != nullptr);
    assert(t->column(QStringLiteral("pitch")) != nullptr);
    assert(t->column(QStringLiteral("roll"))->type == ValueType::F64);
    // 共享时间轴：UTC 微秒，0/1/2 秒
    assert(t->timeUs.at(0) == 0);
    assert(t->timeUs.at(1) == 1000000);
    assert(t->timeUs.at(2) == 2000000);
    fprintf(stdout, "[ok] Dataset = 多张二维表；表内列共享 UTC 微秒时间轴（typed 列 + nulls）\n");

    // 2b) 通道投影：table.column -> (秒,值) f64 序列
    const auto ch = ds->channel(QStringLiteral("demo.roll"));
    assert(ch.has_value());
    assert(ch->size() == 3);
    assert(ch->time.first() == 0.0 && ch->time.last() == 2.0);
    assert(ch->value.at(1) == 1.5);
    fprintf(stdout, "[ok] 通道投影 table.column 产出绘图用 (t_sec, value) 序列\n");

    // 3) 意图执行：plot 模块按 table.column 编译出 PlotSpec
    Result<IntentResult> rendered = application.modules().execute(renderIntent(id, QStringLiteral("demo.pitch")));
    assert(rendered);
    const QVariantMap spec = rendered.value().payload.toMap();
    assert(spec.value(QStringLiteral("type")).toString() == QStringLiteral("timeseries"));
    assert(spec.value(QStringLiteral("column")).toString() == QStringLiteral("pitch"));
    assert(spec.value(QStringLiteral("valueType")).toString() == QStringLiteral("F64"));
    fprintf(stdout, "[ok] 意图 plot.render 产出 PlotSpec（含 table/column/valueType）\n");

    // 3b) 脚本自动化：Lua 通过门面 ps.* 发意图，与 GUI/CLI 走同一执行器。
    //     脚本里不认识 CSV/plot 模块，只认识 ps 门面。
    {
        const QString script =
            QStringLiteral("local id = ps.data.load('%1', 'csv', 100)\n"
                           "ps.log('script loaded dataset ' .. id)\n"
                           "local spec = ps.plot.render(id, 'demo.roll')\n"
                           "assert(spec.type == 'timeseries')\n"
                           "assert(spec.column == 'roll')\n").arg(csv);
        Intent runScript;
        runScript.op = QStringLiteral("script.run");
        runScript.params[QStringLiteral("source")] = script.toUtf8();

        application.recorder().clear();
        application.recorder().setRecording(true);
        Result<IntentResult> sr = application.modules().execute(runScript);
        application.recorder().setRecording(false);
        assert(sr);
        fprintf(stdout, "[ok] Lua 脚本通过 ps 门面执行成功（headless，无 QWidget）\n");

        // 3c) 录制：脚本调用产生的业务意图被录成 Job（不含 script.run 本身）
        assert(application.recorder().size() == 2);
        const Job recorded = application.recorder().toJob();
        assert(recorded.at(0).op == QStringLiteral("data.load"));
        assert(recorded.at(1).op == QStringLiteral("plot.render"));
        fprintf(stdout, "[ok] 脚本操作被录制成 2 条业务意图的可回放 Job\n");

        // 3d) 回放：移除脚本数据集后重放 Job，得到相同 PlotSpec —— 闭环成立。
        //     稳定 id=100 让回放与会话状态无关，不影响主流程的 id=0 数据集。
        application.data().remove(100);
        assert(application.data().get(100) == nullptr);
        assert(application.data().get(id) != nullptr); // 主数据集仍在
        int replayedId = -1;
        QVariantMap replayedSpec;
        for (int i = 0; i < recorded.size(); ++i)
        {
            Result<IntentResult> rr = application.modules().execute(recorded.at(i));
            assert(rr);
            if (recorded.at(i).op == QStringLiteral("data.load"))
                replayedId = rr.value().payload.toInt();
            else
                replayedSpec = rr.value().payload.toMap();
        }
        assert(replayedId >= 0);
        assert(replayedSpec.value(QStringLiteral("column")).toString() == QStringLiteral("roll"));
        assert(replayedSpec.value(QStringLiteral("points")).toInt() == 3);
        fprintf(stdout, "[ok] Job 回放出相同 PlotSpec —— 脚本录制/回放闭环成立\n");
    }

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
    job.append(renderIntent(id, QStringLiteral("demo.roll")));
    Result<void> parseErr;
    Job restored = Job::fromJson(job.toJson(), &parseErr);
    assert(parseErr);
    assert(restored.size() == 2);
    fprintf(stdout, "[ok] Job JSON 序列化往返\n");

    fprintf(stdout, "\n全部架构契约测试通过\n");
    return 0;
}

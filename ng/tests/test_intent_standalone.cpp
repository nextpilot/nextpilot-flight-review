#include "Intent.h"
#include "IIntentExecutor.h"
#include "JobRecorder.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <iostream>

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

// 最小执行器实现，验证契约头可独立实现，不依赖任何宿主。
class EchoExecutor : public nextpilot::plotscope::intent::IIntentExecutor
{
public:
    nextpilot::plotscope::foundation::Result<nextpilot::plotscope::intent::IntentResult>
    execute(const nextpilot::plotscope::intent::Intent &i) override
    {
        lastOp = i.op;
        return nextpilot::plotscope::intent::IntentResult{};
    }
    QString lastOp;
};
} // namespace

int main()
{
    using namespace nextpilot::plotscope::core;

    Intent a;
    a.op = QStringLiteral("data.load");
    a.params[QStringLiteral("format")] = QStringLiteral("csv");
    a.params[QStringLiteral("rows")] = 42;

    Intent b;
    b.op = QStringLiteral("plot.render");
    b.params[QStringLiteral("title")] = QStringLiteral("speed");

    Job job;
    job.append(a);
    job.append(b);
    check(job.size() == 2, "Job 追加两条意图");

    // 往返：toJson -> fromJson -> 再 toJson，两次 JSON 应一致。
    const QJsonObject json = job.toJson();
    Result<void> err;
    Job restored = Job::fromJson(json, &err);
    check(err.ok(), "fromJson 无错误");
    check(restored.size() == 2, "往返后条数不变");
    check(restored.at(0).op == a.op, "首条 op 一致");
    check(restored.at(0).params.value(QStringLiteral("rows")).toInt() == 42,
          "整数参数无损");
    check(restored.at(1).op == b.op, "末条 op 一致");
    check(QJsonDocument(restored.toJson()) == QJsonDocument(json), "二次序列化一致");

    // 缺 op 应报错。
    QJsonObject bad;
    QJsonArray arr;
    arr.append(QJsonObject{{QStringLiteral("params"), QJsonObject{}}});
    bad[QStringLiteral("intents")] = arr;
    Result<void> badErr;
    Job::fromJson(bad, &badErr);
    check(!badErr.ok(), "缺 op 报错");

    // 录制器。
    JobRecorder rec;
    rec.setRecording(true);
    rec.record(a);
    rec.record(b);
    check(rec.size() == 2, "录制两条");
    check(rec.toJob().size() == 2, "录制产出 Job 条数一致");
    rec.clear();
    check(rec.size() == 0, "清空");

    // 契约可执行。
    EchoExecutor exec;
    auto r = exec.execute(a);
    check(r.ok() && exec.lastOp == a.op, "执行器独立工作");

    if (failures)
    {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "intent standalone: all checks passed\n";
    return 0;
}

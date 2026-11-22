// M2.6 Python 子进程桥验收（headless）：
//   2.6.1 子进程桥 —— py.eval 可用、崩溃隔离（os._exit 不拖垮主进程）、超时强杀
//   2.6.2 数据往返 —— shm 旁路 100 万行 <200ms（硬验收线）
//   2.6.4 数据集↔numpy —— exec 注入 ds、写回落成 v2 数据集
// 走真实执行路径：App 装配 + script.py.* 意图（与脚本/GUI 同一条执行器）。
#include "App.h"
#include "Intent.h"
#include "Dataset.h"
#include "DatasetHub.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;

namespace
{
Result<IntentResult> runOp(App &application, const QString &op, const QVariantMap &params)
{
    Intent in;
    in.op = op;
    in.params = params;
    return application.host().execute(in);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    App application;
    const auto errors = application.startup(true);
    for (auto it = errors.constBegin(); it != errors.constEnd(); ++it)
        fprintf(stderr, "[skip] %s: %s\n", qPrintable(it.key()), qPrintable(it.value()));

    // ---- 0) status：解释器可用（沙箱有 python3；找不到则后续全部失败，给出明确诊断）----
    {
        const Result<IntentResult> r = runOp(application, QStringLiteral("script.py.status"), {});
        CHECK(r);
        const QVariantMap st = r.value().payload.toMap();
        CHECK(st.value(QStringLiteral("available")).toBool());
        fprintf(stderr, "[py] interpreter=%s\n",
                qPrintable(st.value(QStringLiteral("interpreter")).toString()));
    }

    // ---- 1) 2.6.1：eval 基本面（数值/字符串/结构体往返）----
    {
        const Result<IntentResult> r =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"), QStringLiteral("40 + 2")}});
        CHECK(r);
        CHECK(r.value().payload.toInt() == 42);
        const Result<IntentResult> r2 =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"),
                               QStringLiteral("'ab' * 3")}});
        CHECK(r2 && r2.value().payload.toString() == QLatin1String("ababab"));
        // numpy 预注入：np.sum
        const Result<IntentResult> r3 =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"),
                               QStringLiteral("int(np.arange(10).sum())")}});
        CHECK(r3 && r3.value().payload.toInt() == 45);
    }

    // ---- 2) 2.6.1：脚本阶段异常 → 错误返回、主进程无恙 ----
    {
        const Result<IntentResult> r =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"), QStringLiteral("1/0")}});
        CHECK(!r);
        fprintf(stderr, "[py] 异常隔离 ok: %s\n", qPrintable(r.error().message));
        const Result<IntentResult> after =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"), QStringLiteral("7 * 6")}});
        CHECK(after && after.value().payload.toInt() == 42);
    }

    // ---- 3) 2.6.1：崩溃隔离（os._exit(1) 杀子进程；主进程活、下一条请求自动重启）----
    {
        const Result<IntentResult> r =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"),
                               QStringLiteral("__import__('os')._exit(1)")}});
        CHECK(!r);
        fprintf(stderr, "[py] 崩溃隔离 ok: %s\n", qPrintable(r.error().message));
        const Result<IntentResult> after =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"), QStringLiteral("6 * 7")}});
        CHECK(after && after.value().payload.toInt() == 42);
    }

    // ---- 4) 2.6.1：超时强杀（sleep 卡住 → 定时杀进程 → 明确错误）----
    {
        const Result<IntentResult> r =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"),
                               QStringLiteral("__import__('time').sleep(5)")},
                              {QStringLiteral("timeout_ms"), 500}});
        CHECK(!r);
        fprintf(stderr, "[py] 超时强杀 ok: %s\n", qPrintable(r.error().message));
        const Result<IntentResult> after =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"), QStringLiteral("2**8")}});
        CHECK(after && after.value().payload.toInt() == 256);
    }

    // ---- 5) 2.6.2：shm 旁路基准——100 万行 f64 C++→Python <200ms ----
    {
        Dataset ds = Dataset::create(QStringLiteral("bench"), QStringLiteral("csv"));
        QVector<double> t1m(1000000);
        QVector<double> v1m(1000000);
        for (int i = 0; i < 1000000; ++i)
        {
            t1m[i] = i * 0.01;
            v1m[i] = i;
        }
        ds.addTable(QStringLiteral("log"), t1m);
        ds.addF64Column(QStringLiteral("log"), QStringLiteral("val"), QString(), v1m);
        ds.refreshSchema();
        const DatasetId id = application.data().add(ds);
        CHECK(id >= 0);

        // push：读列 + 写 shm + Python 注册
        QElapsedTimer timer;
        timer.start();
        const Result<IntentResult> r =
            runOp(application, QStringLiteral("script.py.push"),
                  QVariantMap{{QStringLiteral("dataset"), id},
                              {QStringLiteral("column"), QStringLiteral("val")},
                              {QStringLiteral("name"), QStringLiteral("big")}});
        const qint64 pushMs = timer.elapsed();
        CHECK(r);
        CHECK(r.value().payload.toMap().value(QStringLiteral("rows")).toInt() == 1000000);

        // 校验：Python 侧算 sum（证明数据真的过去且数值正确）
        const Result<IntentResult> sum =
            runOp(application, QStringLiteral("script.py.eval"),
                  QVariantMap{{QStringLiteral("code"),
                               QStringLiteral("float(np.asarray(big).sum())")}});
        CHECK(sum);
        // 1e6 * (1e6-1) / 2 = 499999500000
        CHECK(qFuzzyCompare(sum.value().payload.toDouble(), 499999500000.0));
        fprintf(stderr, "[py] 100 万行 push=%lld ms（硬验收线 <200ms）\n",
                static_cast<long long>(pushMs));
        CHECK(pushMs < 200);

        // pull：Python → 文件旁路 → C++（含读取，一并计时）
        timer.restart();
        const Result<IntentResult> pr =
            runOp(application, QStringLiteral("script.py.pull"),
                  QVariantMap{{QStringLiteral("name"), QStringLiteral("big")}});
        const qint64 pullMs = timer.elapsed();
        CHECK(pr);
        CHECK(pr.value().payload.toMap().value(QStringLiteral("rows")).toInt() == 1000000);
        fprintf(stderr, "[py] 100 万行 pull=%lld ms\n", static_cast<long long>(pullMs));
        CHECK(pullMs < 200);

        (void)runOp(application, QStringLiteral("data.remove"),
                    QVariantMap{{QStringLiteral("id"), id}});
    }

    // ---- 6) 2.6.4：exec 注入 ds + 写回落成 v2 数据集 ----
    {
        Dataset ds = Dataset::create(QStringLiteral("flight"), QStringLiteral("csv"));
        QVector<double> t(60);
        QVector<double> rpm(60);
        for (int i = 0; i < 60; ++i)
        {
            t[i] = i * 0.1;
            rpm[i] = 1400 + i;
        }
        ds.addTable(QStringLiteral("log"), t);
        ds.addF64Column(QStringLiteral("log"), QStringLiteral("engine_rpm[rpm]"), QStringLiteral("rpm"), rpm);
        ds.refreshSchema();
        const DatasetId id = application.data().add(ds);
        CHECK(id >= 0);
        const int before = application.data().count();

        // Python：读列 → rolling 均值（numpy 卷积）→ 写回
        const Result<IntentResult> r =
            runOp(application, QStringLiteral("script.py.exec"),
                  QVariantMap{
                      {QStringLiteral("dataset"), id},
                      {QStringLiteral("source"), QStringLiteral(
                           "v = ds['engine_rpm[rpm]']\n"
                           "k = np.ones(5) / 5\n"
                           "ds['rpm_smooth'] = np.convolve(v, k, mode='same')\n"
                           "round(float(v.mean()), 2)")}});
        CHECK(r);
        const QVariantMap out = r.value().payload.toMap();
        // mean(1400..1459) = 1429.5
        CHECK(qFuzzyCompare(out.value(QStringLiteral("value")).toDouble(), 1429.5));
        CHECK(out.value(QStringLiteral("rows")).toInt() == 60);
        CHECK(out.value(QStringLiteral("new")).toMap().contains(QStringLiteral("rpm_smooth")));

        // 写回 → v2 数据集（原数据集不动）
        const Result<IntentResult> wb =
            runOp(application, QStringLiteral("script.py.writeback"),
                  QVariantMap{{QStringLiteral("dataset"), id}});
        if (!wb)
            fprintf(stderr, "[py] writeback 错误: %s\n", qPrintable(wb.error().message));
        CHECK(wb);
        const int v2Id = wb.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
        CHECK(v2Id >= 0 && v2Id != id);
        CHECK(application.data().count() == before + 1);
        const Dataset *v2 = application.data().get(v2Id);
        CHECK(v2 && v2->name().contains(QStringLiteral("py v2")));
        QVector<double> smooth;
        CHECK(v2->readColumnRows(QStringLiteral("log"), QStringLiteral("rpm_smooth"), 0, 60,
                                 smooth) == 60);
        // 卷积 same 模式边界外补零：中心点应是 5 点均值；抽查 3 个点
        CHECK(qFuzzyCompare(smooth[10], (rpm[8] + rpm[9] + rpm[10] + rpm[11] + rpm[12]) / 5.0));
        CHECK(qFuzzyCompare(smooth[30], (rpm[28] + rpm[29] + rpm[30] + rpm[31] + rpm[32]) / 5.0));
        // 原数据集没有被塞进新列（不可变快照）
        const Dataset *orig = application.data().get(id);
        CHECK(orig->table(0)->column(QStringLiteral("rpm_smooth")) == nullptr);
        (void)runOp(application, QStringLiteral("data.remove"),
                    QVariantMap{{QStringLiteral("id"), id}});
        (void)runOp(application, QStringLiteral("data.remove"),
                    QVariantMap{{QStringLiteral("id"), v2Id}});
    }

    // ---- 7) exec 末条表达式 + stdout 捕获（REPL 语义 / 控制台回显）----
    {
        const Result<IntentResult> r =
            runOp(application, QStringLiteral("script.py.exec"),
                  QVariantMap{{QStringLiteral("source"),
                               QStringLiteral("print('hello from py')\n21 * 2")}});
        CHECK(r);
        const QVariantMap out = r.value().payload.toMap();
        CHECK(out.value(QStringLiteral("out")).toString().trimmed() ==
              QLatin1String("hello from py"));
        CHECK(out.value(QStringLiteral("value")).toInt() == 42);
    }

    fprintf(stderr, "ALL PASS\n");
    return 0;
}

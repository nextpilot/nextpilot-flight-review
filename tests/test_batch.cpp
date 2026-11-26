// M2.4 批量处理验收（headless）：
//   2.4.1 data.batch.run 意图 + 失败明细 —— 3 个文件 1 个损坏 → 2 成功 1 失败明细准确
//   2.4.2 进度反馈（可取消）—— progress 广播计数 + 分片边界取消生效
// 走真实执行路径：App 装配 + data.load/script 同一条执行器（与 GUI/CLI 同路径）。
#include "App.h"
#include "Intent.h"
#include "DatasetHub.h"
#include "InteractionHub.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
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
// 写一份合法 CSV（与 tests/fixture/demo_flight.csv 同构，少量行即可）。
bool writeCsv(const QString &path, int seed)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream ts(&f);
    ts << "time_ms,att.roll[deg],engine_rpm[rpm]\n";
    for (int i = 0; i < 50; ++i)
        ts << (i * 10) << ',' << (seed + i % 7) << ',' << (1400 + i + seed) << '\n';
    return true;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(bytes);
    return true;
}

QVariantMap runBatch(App &application, const QVariantMap &params, Result<IntentResult> *rOut = nullptr)
{
    Intent bi;
    bi.op = QStringLiteral("data.batch.run");
    bi.params = params;
    Result<IntentResult> r = application.host().execute(bi);
    if (rOut)
        *rOut = r;
    if (!r)
        return QVariantMap();
    return r.value().payload.toMap();
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    App application;
    const auto errors = application.startup(true);
    for (auto it = errors.constBegin(); it != errors.constEnd(); ++it)
        fprintf(stderr, "[skip] %s: %s\n", qPrintable(it.key()), qPrintable(it.value()));

    // 内联批量脚本：return 表即该文件的指标（走 runStringResult 捕获链）。
    const char *kMetricScript =
        "local m = {}\n"
        "m.file = batch.file\n"
        "m.ds = batch.dataset\n"
        "return m\n";

    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString d = dir.path();
    CHECK(writeCsv(d + QStringLiteral("/good1.csv"), 1));
    CHECK(writeCsv(d + QStringLiteral("/good2.csv"), 2));
    // 故意损坏：二进制垃圾，摄取管道必失败
    // 注意：这里必须用 QByteArrayLiteral 让长度编译期定死。原先写
    // QByteArray("...", 26) —— 字面量只有 24 字节，ASan 直接报 global-buffer-overflow
    // （sanitizer 腿首轮抓到，GCC -O3 普通构建下只是读两个越界字节、看不出问题）。
    CHECK(writeFile(d + QStringLiteral("/bad.csv"), QByteArrayLiteral("\x89PNG\r\n\x1a\nnot a csv at all")));

    // ---- 2.4.1：3 个文件 1 个损坏 → 2 成功 1 失败，失败明细准确 ----
    QVariantMap p;
    p.insert(QStringLiteral("dir"), d);
    p.insert(QStringLiteral("source"), QLatin1String(kMetricScript));
    const QVariantMap summary = runBatch(application, p);
    CHECK(!summary.isEmpty());
    CHECK(summary.value(QStringLiteral("total")).toInt() == 3);
    CHECK(summary.value(QStringLiteral("ok")).toInt() == 2);
    CHECK(summary.value(QStringLiteral("failed")).toInt() == 1);
    CHECK(!summary.value(QStringLiteral("cancelled")).toBool());

    const QVariantList rows = summary.value(QStringLiteral("rows")).toList();
    CHECK(rows.size() == 3);
    QVariantMap badRow;
    for (const QVariant &rv : rows)
    {
        const QVariantMap row = rv.toMap();
        if (!row.value(QStringLiteral("ok")).toBool())
        {
            badRow = row;
            break;
        }
    }
    CHECK(!badRow.isEmpty());
    CHECK(badRow.value(QStringLiteral("file")).toString().endsWith(QStringLiteral("bad.csv")));
    CHECK(badRow.value(QStringLiteral("stage")).toString() == QStringLiteral("load"));
    CHECK(!badRow.value(QStringLiteral("error")).toString().isEmpty());
    // 成功行带脚本 return 的指标（file/ds 注入 batch 全局 → runStringResult 捕获）
    for (const QVariant &rv : rows)
    {
        const QVariantMap row = rv.toMap();
        if (!row.value(QStringLiteral("ok")).toBool())
            continue;
        const QVariantMap metrics = row.value(QStringLiteral("metrics")).toMap();
        CHECK(metrics.value(QStringLiteral("file")).toString()
              == row.value(QStringLiteral("file")).toString());
        CHECK(metrics.value(QStringLiteral("ds")).toInt() > 0);
    }
    // 默认每文件加载→运行→移除：不堆积数据集（100 个文件不膨胀内存）。
    // 注：data.remove 的底层回收是延迟的（deleteLater），先冲事件循环再断言。
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    CHECK(application.data().count() == 0);
    fprintf(stdout, "[ok] 2.4.1 data.batch.run：3 文件 1 损坏 → 2 成功 1 失败（stage=load 明细准确）\n");

    // ---- 脚本阶段失败：stage=script 明细 ----
    QVariantMap p2;
    p2.insert(QStringLiteral("dir"), d);
    p2.insert(QStringLiteral("pattern"), QStringLiteral("good1.csv"));
    p2.insert(QStringLiteral("source"), QByteArrayLiteral("error('boom')\n"));
    const QVariantMap s2 = runBatch(application, p2);
    CHECK(!s2.isEmpty());
    CHECK(s2.value(QStringLiteral("failed")).toInt() == 1);
    const QVariantMap scriptFailRow = s2.value(QStringLiteral("rows")).toList().at(0).toMap();
    CHECK(scriptFailRow.value(QStringLiteral("stage")).toString() == QStringLiteral("script"));
    CHECK(scriptFailRow.value(QStringLiteral("error")).toString().contains(QStringLiteral("boom")));
    fprintf(stdout, "[ok] 2.4.1 脚本阶段失败：stage=script，错误信息带 boom\n");

    // ---- 2.4.2：进度广播计数 == total ----
    int progressCount = 0;
    application.context().interactions().registerViewAction(
        QStringLiteral("data.batch.progress"),
        [&progressCount](const QVariantMap &) -> Result<QVariant> {
            ++progressCount;
            return Result<QVariant>(QVariant(true));
        });
    QVariantMap p3;
    p3.insert(QStringLiteral("dir"), d);
    p3.insert(QStringLiteral("source"), QLatin1String(kMetricScript));
    (void)runBatch(application, p3);
    CHECK(progressCount == 3);
    fprintf(stdout, "[ok] 2.4.2 进度广播：progress 触发次数 == 文件数\n");

    // ---- view action 注销：命中 true、注销后 dispatch false、重复注销安全 ----
    CHECK(application.context().interactions().unregisterViewAction(QStringLiteral("data.batch.progress")));
    Result<QVariant> afterRemove{QVariant()};
    CHECK(!application.context().interactions().dispatchViewAction(QStringLiteral("data.batch.progress"), QVariantMap(),
                                                                   afterRemove));
    CHECK(!application.context().interactions().unregisterViewAction(QStringLiteral("data.batch.progress")));
    CHECK(progressCount == 3);
    fprintf(stdout, "[ok] 注销后 dispatch 返回 false，重复注销安全\n");

    // ---- 2.4.2：取消 —— 分片边界生效（5 文件每文件 ~40ms，0ms 定时器插取消）----
    CHECK(writeCsv(d + QStringLiteral("/c1.csv"), 10));
    CHECK(writeCsv(d + QStringLiteral("/c2.csv"), 20));
    CHECK(writeCsv(d + QStringLiteral("/c3.csv"), 30));
    CHECK(writeCsv(d + QStringLiteral("/c4.csv"), 40));
    CHECK(writeCsv(d + QStringLiteral("/c5.csv"), 50));
    const char *kBusyScript =
        "local t0 = os.clock()\n"
        "while os.clock() - t0 < 0.04 do end\n"
        "return {}\n";
    // 批量在 execute 内每文件后 processEvents → 0ms 定时器在首个分片边界触发取消。
    QTimer::singleShot(0, &app, [&application]() {
        Result<QVariant> out{QVariant()};
        application.context().interactions().dispatchViewAction(
            QStringLiteral("data.batch.cancel"), QVariantMap(), out);
    });
    QVariantMap p4;
    p4.insert(QStringLiteral("dir"), d);
    p4.insert(QStringLiteral("pattern"), QStringLiteral("c?.csv"));
    p4.insert(QStringLiteral("source"), QLatin1String(kBusyScript));
    const QVariantMap s4 = runBatch(application, p4);
    CHECK(!s4.isEmpty());
    CHECK(s4.value(QStringLiteral("cancelled")).toBool());
    const int doneOk = s4.value(QStringLiteral("ok")).toInt();
    CHECK(doneOk >= 1 && doneOk < 5);
    fprintf(stdout, "[ok] 2.4.2 可取消：%d/5 后停止，cancelled=true\n", doneOk);

    // ---- keep=true 保留数据集：load 产物可被继续消费（对比默认移除路径）----
    QVariantMap p5;
    p5.insert(QStringLiteral("dir"), d);
    p5.insert(QStringLiteral("pattern"), QStringLiteral("good1.csv"));
    p5.insert(QStringLiteral("source"), QLatin1String(kMetricScript));
    p5.insert(QStringLiteral("keep"), true);
    const QVariantMap s5 = runBatch(application, p5);
    CHECK(s5.value(QStringLiteral("ok")).toInt() == 1);
    CHECK(application.data().count() == 1);
    fprintf(stdout, "[ok] keep=true 保留数据集（load 产物可继续消费）\n");

    fprintf(stdout, "ALL PASS\n");
    return 0;
}

// 画布导出 / 视图状态 / 图例开关 契约测试（fastplot 库能力接进 ng 后的视图薄包装）。
//
// 验证：PlotCanvas 把库的 exportPng/exportCsv/saveState/restoreState/legend
// 正确暴露出来；导出文件真实落盘且非空；视图状态可 round-trip。

#include "App.h"
#include "MainWindow.h"
#include "PlotCanvas.h"
#include "ShellBinder.h"

#include "Dataset.h"

#include <QApplication>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ng::app;
using namespace ng::core;
using namespace ng::gui;
using namespace ng::host;

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

#define OK(msg) fprintf(stdout, "[ok] %s\n", msg)

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    App application;
    MainWindow window(application.context());
    window.build(application.modules());
    window.resize(1280, 800);

    ShellBinder *binder = window.binder();
    CHECK(binder != nullptr);
    auto *canvas = qobject_cast<PlotCanvas *>(binder->centralContent());
    CHECK(canvas != nullptr);

    // 喂一批 LOD 三元组，让画布上确实有曲线可导出。
    QVector<LodTriple> lod;
    lod.reserve(120);
    for (int i = 0; i < 120; ++i)
    {
        const double x = i * 0.01;
        const double y = std::sin(i * 0.2);
        lod.push_back(LodTriple{x, y - 0.05, y + 0.05});
    }
    canvas->setChannel(0, QStringLiteral("trace"), QStringLiteral("sig"),
                       QStringLiteral("trace.sig"));
    canvas->setLodData(lod, 0.0, 1.19);

    QTemporaryDir dir;
    CHECK(dir.isValid());

    // --- 1) 导出 PNG ---
    const QString pngPath = dir.filePath(QStringLiteral("canvas.png"));
    CHECK(canvas->exportPng(pngPath));
    QFile png(pngPath);
    CHECK(png.exists() && png.size() > 1000);
    OK(QStringLiteral("导出 PNG：%1 字节").arg(png.size()).toUtf8().constData());

    // --- 2) 导出 CSV（LOD 三段：x,min,max）---
    const QString csvPath = dir.filePath(QStringLiteral("canvas.csv"));
    CHECK(canvas->exportCsv(csvPath));
    QFile csv(csvPath);
    CHECK(csv.exists() && csv.size() > 0);
    csv.open(QIODevice::ReadOnly);
    const QByteArray csvBody = csv.readAll();
    CHECK(csvBody.contains("x,min,max"));
    OK(QStringLiteral("导出 CSV：%1 字节，含 x,min,max 表头").arg(csv.size()).toUtf8().constData());

    // --- 3) 图例开关 ---
    canvas->setLegendVisible(true);
    CHECK(canvas->legendVisible());
    canvas->setLegendVisible(false);
    CHECK(!canvas->legendVisible());

    // --- 4) 视图状态 round-trip：保存 -> 改图例 -> 复原 ---
    const QJsonObject state = canvas->saveViewState();
    CHECK(!state.isEmpty());
    canvas->setLegendVisible(true);
    CHECK(canvas->restoreViewState(state));
    CHECK(!canvas->legendVisible()); // 复原回第 3 步保存时的 false
    OK("视图状态：save/restore 可 round-trip，图例归属复原");

    // --- 5) 视图动作登记路径：经 AppContext 派发（与 ribbon 点击同一条路）---
    {
        QVariant nullV;
        Result<QVariant> pngResult(nullV);
        const QString png2 = dir.filePath(QStringLiteral("via_action.png"));
        CHECK(application.context().dispatchViewAction(
            QStringLiteral("plot.export.png"), {{QStringLiteral("path"), png2}}, pngResult));
        CHECK(pngResult);
        CHECK(QFile::exists(png2));

        Result<QVariant> csvResult(nullV);
        const QString csv2 = dir.filePath(QStringLiteral("via_action.csv"));
        CHECK(application.context().dispatchViewAction(
            QStringLiteral("plot.export.csv"), {{QStringLiteral("path"), csv2}}, csvResult));
        CHECK(csvResult);
        CHECK(QFile::exists(csv2));

        Result<QVariant> saveResult(nullV);
        CHECK(application.context().dispatchViewAction(
            QStringLiteral("plot.state.save"), {}, saveResult));
        const QJsonObject saved = saveResult.value().toJsonObject();
        CHECK(!saved.isEmpty());

        canvas->setLegendVisible(true);
        Result<QVariant> restoreResult(nullV);
        CHECK(application.context().dispatchViewAction(
            QStringLiteral("plot.state.restore"),
            {{QStringLiteral("state"), saved}}, restoreResult));
        CHECK(restoreResult);

        Result<QVariant> toggleResult(nullV);
        CHECK(application.context().dispatchViewAction(
            QStringLiteral("plot.legend.toggle"), {}, toggleResult));
        CHECK(toggleResult);

        // 未注册的动作：派发返回 false，交回意图层。
        Result<QVariant> miss(nullV);
        CHECK(!application.context().dispatchViewAction(
            QStringLiteral("plot.not-a-real-action"), {}, miss));
        OK("视图动作：导出/状态/图例经 AppContext 派发闭环，未知动作回落意图层");
    }

    fprintf(stdout, "\n画布导出/状态/图例测试全部通过\n");
    return 0;
}

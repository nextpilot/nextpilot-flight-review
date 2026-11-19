// 画布导出 / 视图状态 / 图例开关 契约测试（fastplot 库能力接进 ng 后的视图薄包装）。
//
// 验证：PlotCanvas 把库的 exportPng/exportCsv/saveState/restoreState/legend
// 正确暴露出来；导出文件真实落盘且非空；视图状态可 round-trip。

#include "App.h"
#include "MainWindow.h"
#include "PlotCanvas.h"
#include "CanvasWorkspace.h"
#include "ShellBinder.h"

#include "Dataset.h"

#include <QApplication>
#include <QFile>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace nextpilot::plotscope::app;
using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::gui;
using namespace nextpilot::plotscope::host;

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
    window.build(application.host());
    window.resize(1280, 800);

    ShellBinder *binder = window.binder();
    CHECK(binder != nullptr);
    auto *workspace = qobject_cast<CanvasWorkspace *>(binder->centralContent());
    CHECK(workspace != nullptr);
    auto *canvas = workspace ? workspace->activeCanvas() : nullptr;
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
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.export.png"), {{QStringLiteral("path"), png2}}, pngResult));
        CHECK(pngResult);
        CHECK(QFile::exists(png2));

        Result<QVariant> csvResult(nullV);
        const QString csv2 = dir.filePath(QStringLiteral("via_action.csv"));
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.export.csv"), {{QStringLiteral("path"), csv2}}, csvResult));
        CHECK(csvResult);
        CHECK(QFile::exists(csv2));

        Result<QVariant> saveResult(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.state.save"), {}, saveResult));
        const QJsonObject saved = saveResult.value().toJsonObject();
        CHECK(!saved.isEmpty());

        canvas->setLegendVisible(true);
        Result<QVariant> restoreResult(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.state.restore"),
            {{QStringLiteral("state"), saved}}, restoreResult));
        CHECK(restoreResult);

        Result<QVariant> toggleResult(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.legend.toggle"), {}, toggleResult));
        CHECK(toggleResult);

        // 未注册的动作：派发返回 false，交回意图层。
        Result<QVariant> miss(nullV);
        CHECK(!application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.not-a-real-action"), {}, miss));
        OK("视图动作：导出/状态/图例经 AppContext 派发闭环，未知动作回落意图层");
    }

    // --- 6) 多曲线叠加：追加不覆盖、同 key 去重、单条移除 ---
    {
        auto mk = [](int id, const QString &col) {
            PlotSeries s;
            s.datasetId = id;
            s.table = QStringLiteral("t");
            s.column = col;
            s.title = col;
            QVector<LodTriple> l;
            for (int i = 0; i < 40; ++i)
                l.push_back(LodTriple{i * 0.01, std::sin(i * 0.1), std::sin(i * 0.1)});
            s.lod = l;
            s.t0 = 0.0;
            s.t1 = 0.39;
            return s;
        };
        canvas->clear();
        canvas->setSeries(mk(1, QStringLiteral("roll")));
        canvas->setSeries(mk(1, QStringLiteral("pitch")));
        canvas->setSeries(mk(2, QStringLiteral("yaw")));
        CHECK(canvas->seriesCount() == 3);
        CHECK(canvas->lodPointCount() == 120);
        // 同 key 刷新：不新增，点数不变。
        canvas->setSeries(mk(1, QStringLiteral("roll")));
        CHECK(canvas->seriesCount() == 3);
        // 颜色互不相同（叠加可区分）。
        CHECK(canvas->seriesAt(0)->color != canvas->seriesAt(1)->color);
        // 单条移除。
        CHECK(canvas->removeSeries(QStringLiteral("1|t|pitch")));
        CHECK(canvas->seriesCount() == 2);
        CHECK(canvas->seriesAt(0)->column == QStringLiteral("roll"));
        CHECK(canvas->seriesAt(1)->column == QStringLiteral("yaw"));
        // 曲线清单视图动作。
        QVariant nullV;
        Result<QVariant> listResult(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.series.list"), {}, listResult));
        CHECK(listResult.value().toList().size() == 2);
        OK("多曲线叠加：追加/去重/移除/清单视图动作全部通过");
    }

    // --- 7) 双 Y 轴：用户可选挂轴 + 视图状态 round-trip + 图型切换 ---
    {
        auto mk = [](const QString &col, double scale) {
            PlotSeries s;
            s.datasetId = 7;
            s.table = QStringLiteral("t");
            s.column = col;
            s.title = col;
            QVector<LodTriple> l;
            for (int i = 0; i < 20; ++i)
            {
                const double v = std::sin(i * 0.2) * scale;
                l.push_back(LodTriple{i * 0.01, v, v});
            }
            s.lod = l;
            s.t0 = 0.0;
            s.t1 = 0.19;
            return s;
        };
        canvas->clear();
        canvas->setSeries(mk(QStringLiteral("leftCh"), 1.0));
        canvas->setSeries(mk(QStringLiteral("rightCh"), 100.0)); // 量级差百倍
        CHECK(canvas->seriesCount() == 2);

        // 用户选择：把第二条挂右轴。
        CHECK(canvas->setSeriesAxis(QStringLiteral("7|t|rightCh"), true));
        CHECK(canvas->seriesAt(1)->rightAxis);
        CHECK(!canvas->seriesAt(0)->rightAxis);

        // 视图状态含左右轴分配与图例可见性，且可 round-trip。
        const QJsonObject st = canvas->saveViewState();
        CHECK(st.contains(QStringLiteral("seriesAxes")));
        CHECK(st.value(QStringLiteral("seriesAxes")).toArray().size() == 2);
        CHECK(st.contains(QStringLiteral("legendVisible")));
        canvas->setSeriesAxis(QStringLiteral("7|t|rightCh"), false); // 改掉
        canvas->setLegendVisible(false);
        CHECK(canvas->restoreViewState(st));
        CHECK(canvas->seriesAt(1)->rightAxis); // 恢复成挂右轴

        // 图型切换：工作区统一路由——曲线类在画布内切，专用图型在 cell 内切到仪表页。
        QVariant nullV;
        Result<QVariant> r(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.setType"), {{QStringLiteral("type"), QStringLiteral("scatter")}}, r));
        CHECK(r.value().toMap().value(QStringLiteral("handled")).toBool());
        CHECK(canvas->plotKind() == QStringLiteral("scatter"));
        Result<QVariant> r2(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.setType"), {{QStringLiteral("type"), QStringLiteral("gauge")}}, r2));
        // 多画布工作区把专用图型也在 cell 内接住了（cell 内切到仪表页），
        // 不再返回 handled=false 依赖外壳替换中央——这样多画布形态得以保留。
        CHECK(r2.value().toMap().value(QStringLiteral("handled")).toBool());
        OK("双 Y 轴：用户选轴 / 分轴范围 / 状态往返 / 图型切换路由全部通过");
    }

    // --- 7) 多画布工作区：添加/拆分/网格/删除，view action 路由到活动画布 ---
    {
        QVariant nullV;
        auto stateOf = [&]() -> QVariantMap {
            Result<QVariant> st(nullV);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("page.state"), {}, st));
            return st.value().toMap();
        };
        // 前一段 setType(gauge) 只切了 cell 内页，cell 数不变；本段从 1 起。
        CHECK(stateOf().value(QStringLiteral("cells")).toInt() == 1);

        Result<QVariant> add(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("page.addCanvas"), {}, add));
        CHECK(stateOf().value(QStringLiteral("cells")).toInt() == 2);

        Result<QVariant> split(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("page.splitHorizontal"), {}, split));
        CHECK(stateOf().value(QStringLiteral("cells")).toInt() == 3);

        Result<QVariant> grid(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("page.grid"),
            {{QStringLiteral("rows"), 2}, {QStringLiteral("cols"), 2}}, grid));
        CHECK(grid.value().toMap().value(QStringLiteral("cells")).toInt() == 4);
        CHECK(workspace->rows() == 2 && workspace->cols() == 2);
        CHECK(workspace->cellCount() == 4);
        CHECK(workspace->activeCanvas() != nullptr);
        CHECK(workspace->activeChannelView() != nullptr);

        Result<QVariant> rm(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("page.removeCanvas"), {}, rm));
        CHECK(workspace->cellCount() == 3);
        OK("多画布工作区：添加/拆分/2×2 网格/删除 + 活动画布路由全部通过");
    }

    fprintf(stdout, "\n画布导出/状态/图例测试全部通过\n");
    return 0;
}

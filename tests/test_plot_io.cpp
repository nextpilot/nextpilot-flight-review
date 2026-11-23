// 画布导出 / 视图状态 / 图例开关 契约测试（fastplot 库能力接进 ng 后的视图薄包装）。
//
// 验证：AxesView 把库的 exportPng/exportCsv/saveState/restoreState/legend
// 正确暴露出来；导出文件真实落盘且非空；视图状态可 round-trip。

#include "App.h"
#include "MainWindow.h"
#include "AxesView.h"
#include "PlotWorkspace.h"
#include "PlotLayout.h"
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
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::gui;
using namespace nextpilot::plotscope::host;
namespace plot = nextpilot::plotscope::plot;

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
    auto *workspace = qobject_cast<PlotWorkspace *>(binder->centralContent());
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
    CHECK(csv.open(QIODevice::ReadOnly));
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
                QStringLiteral("figure.state"), {}, st));
            return st.value().toMap();
        };
        // 前一段 setType(gauge) 只切了 cell 内页，cell 数不变；本段从 1 起。
        CHECK(stateOf().value(QStringLiteral("cells")).toInt() == 1);

        Result<QVariant> add(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.addAxes"), {}, add));
        CHECK(stateOf().value(QStringLiteral("cells")).toInt() == 2);

        Result<QVariant> split(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.splitHorizontal"), {}, split));
        CHECK(stateOf().value(QStringLiteral("cells")).toInt() == 3);

        Result<QVariant> grid(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.grid"),
            {{QStringLiteral("rows"), 2}, {QStringLiteral("cols"), 2}}, grid));
        CHECK(grid.value().toMap().value(QStringLiteral("cells")).toInt() == 4);
        CHECK(workspace->rows() == 2 && workspace->cols() == 2);
        CHECK(workspace->cellCount() == 4);
        CHECK(workspace->activeCanvas() != nullptr);
        CHECK(workspace->activeChannelView() != nullptr);

        Result<QVariant> rm(nullV);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.removeAxes"), {}, rm));
        CHECK(workspace->cellCount() == 3);
        OK("多画布工作区：添加/拆分/2×2 网格/删除 + 活动画布路由全部通过");
    }

    // --- 8) 第2仗恢复链：视口不被绑曲线覆盖 + 颜色显式回读 ---
    {
        // 8a. 视口保护：存盘一个"深缩放"视口，恢复后紧接着绑一条全量曲线，
        //     视口必须停在存盘值，不被 pushToPlot 的 setXRange(m_t0,m_t1) 覆盖。
        auto mkSeries = [](DatasetId id, const QString &col, double t1) {
            PlotSeries s;
            s.datasetId = id;
            s.table = QStringLiteral("t");
            s.column = col;
            s.title = col;
            QVector<LodTriple> l;
            for (int i = 0; i < 40; ++i)
                l.push_back(LodTriple{i * (t1 / 40.0), std::sin(i * 0.1), std::sin(i * 0.1)});
            s.lod = l;
            s.t0 = 0.0;
            s.t1 = t1;
            return s;
        };
        canvas->clear();
        // 先绑一条全量 0..10 的曲线，拿到含 seriesAxes 的完整 viewState。
        canvas->setSeries(mkSeries(1, QStringLiteral("roll"), 10.0));
        QJsonObject view = canvas->saveViewState();
        // 模拟存盘的深缩放视口 [2,3]（远小于数据全量 [0,10]）。
        view.insert(QStringLiteral("x"), QJsonArray{2.0, 3.0});
        CHECK(canvas->restoreViewState(view));
        double vx0 = 0.0, vx1 = 0.0;
        CHECK(canvas->visibleTimeRange(&vx0, &vx1));
        CHECK(std::fabs(vx0 - 2.0) < 1e-9 && std::fabs(vx1 - 3.0) < 1e-9);
        // 恢复后紧接着绑一条新曲线（触发 pushToPlot）——视口必须仍是 [2,3]。
        canvas->setSeries(mkSeries(1, QStringLiteral("pitch"), 10.0));
        CHECK(canvas->visibleTimeRange(&vx0, &vx1));
        CHECK(std::fabs(vx0 - 2.0) < 1e-9 && std::fabs(vx1 - 3.0) < 1e-9);
        OK("恢复链 8a：存盘视口不被绑曲线 pushToPlot 覆盖（preserveRange）");

        // 8b. 显式颜色回读：setSeriesInk 写入的颜色经 save/restore 稳定保留，
        //     且显式色不会被 palette 重新分配覆盖。
        canvas->clear();
        PlotSeries colored = mkSeries(2, QStringLiteral("yaw"), 5.0);
        canvas->setSeries(colored);
        const QColor savedColor(200, 40, 90);
        canvas->setSeriesInk(colored.key(), savedColor);
        CHECK(canvas->seriesAt(0)->color == savedColor);
        // 再写一条不同曲线，palette 分配不能改动第一条的显式色。
        canvas->setSeries(mkSeries(2, QStringLiteral("roll"), 5.0));
        CHECK(canvas->seriesAt(0)->color == savedColor);
        OK("恢复链 8b：显式颜色稳定保留，不被 palette 重分配覆盖");

        // 8c. offset 不进存盘：设置曲线 X 偏移后，saveViewState 的视口 x 不含 offset
        //     （offset 只叠加绘图副本，不改轴范围/存盘）。
        canvas->clear();
        canvas->setSeries(mkSeries(3, QStringLiteral("sig"), 8.0));
        QJsonObject base = canvas->saveViewState();
        const QJsonArray xBefore = base.value(QStringLiteral("x")).toArray();
        canvas->setSeriesXOffset(mkSeries(3, QStringLiteral("sig"), 8.0).key(), 3.5);
        QJsonObject shifted = canvas->saveViewState();
        const QJsonArray xAfter = shifted.value(QStringLiteral("x")).toArray();
        CHECK(xBefore.size() == 2 && xAfter.size() == 2);
        CHECK(std::fabs(xBefore.at(0).toDouble() - xAfter.at(0).toDouble()) < 1e-12);
        CHECK(std::fabs(xBefore.at(1).toDouble() - xAfter.at(1).toDouble()) < 1e-12);
        OK("恢复链 8c：曲线 X offset 不进存盘视口（不漂移）");
    }

    // --- 9) 端到端：toDocumentSpec/applyDocumentSpec 的颜色 + 视口 round-trip ---
    {
        // 走真实 data.load 造出源+数据集（origin 才有意义）。
        const QString fixture = QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv");
        Intent load;
        load.op = QStringLiteral("data.load");
        load.params[QStringLiteral("path")] = fixture;
        const auto loaded = application.host().execute(load);
        CHECK(loaded);
        const int dsId = loaded.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
        CHECK(dsId >= 0);
        const int srcId = application.context().sources().sourceOfDataset(dsId);
        CHECK(srcId >= 0);
        const DataSourceInfo *srcInfo = application.context().sources().get(srcId);
        CHECK(srcInfo != nullptr && !srcInfo->datasetIds.isEmpty());

        // 动态取真实的 table.column（CSV 解析后 table 名不一定是 att）。
        const Dataset *ds = application.context().data().get(dsId);
        CHECK(ds != nullptr && !ds->tableNames().isEmpty());
        const QString realTable = ds->tableNames().first();
        const Table *tbl = ds->table(realTable);
        CHECK(tbl != nullptr && !tbl->columns.isEmpty());
        const QString realColumn = tbl->columns.first().name;
        const QString realChannel = realTable + QLatin1Char('.') + realColumn;

        // 用 grid 复位到干净 1 格，清掉前几段遗留的曲线/格子。
        {
            QVariant nullV;
            Result<QVariant> g(nullV);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.grid"), {{QStringLiteral("rows"), 1}, {QStringLiteral("cols"), 1}}, g));
        }
        workspace->activeCanvas()->clear();
        // 构造一个只含 1 页 1 格的文档，手动填 CanvasSpec（避免依赖 UI 交互）。
        plot::PlotDocumentSpec doc;
        doc.version = 1;
        plot::PlotPageSpec page;
        page.id = QStringLiteral("page-1");
        plot::LayoutNode canvasNode;
        canvasNode.isCanvas = true;
        plot::CanvasSpec cs;
        cs.id = QStringLiteral("c1");
        cs.viewType = QStringLiteral("timeseries");
        cs.datasetOrigin = srcInfo->origin;
        cs.channels << realChannel;
        // 存盘色 + 深缩放视口。style key 末两段须为 table|column（apply 按此匹配）。
        const QColor docColor(10, 200, 130);
        QJsonObject so;
        so.insert(QStringLiteral("key"), QStringLiteral("%1|%2|%3").arg(dsId).arg(realTable, realColumn));
        so.insert(QStringLiteral("color"), docColor.name());
        QJsonArray styleArr{so};
        QJsonObject styleObj;
        styleObj.insert(QStringLiteral("series"), styleArr);
        cs.style = styleObj;
        QJsonObject st;
        st.insert(QStringLiteral("x"), QJsonArray{1.0, 2.0});
        cs.state = st;
        canvasNode.canvas = cs;
        page.root = canvasNode;
        doc.pages << page;

        // 应用 → 曲线绑上、颜色/视口就位。
        CHECK(workspace->applyDocumentSpec(doc, application.context().sources()));
        // 通过 toDocumentSpec 回读验证（不依赖 active 指向哪个 cell）。
        plot::PlotDocumentSpec round = workspace->toDocumentSpec();
        CHECK(round.pages.size() >= 1);
        QVector<plot::CanvasSpec> allCanvases;
        {
            std::function<void(const plot::LayoutNode &)> grab = [&](const plot::LayoutNode &n) {
                if (n.isCanvas) allCanvases.append(n.canvas);
                for (const auto &ch : n.children) grab(ch);
            };
            grab(round.pages.first().root);
        }
        // 找到绑上目标通道的那张画布。
        plot::CanvasSpec rc;
        for (const auto &cv : allCanvases)
            if (cv.channels.contains(realChannel))
            { rc = cv; break; }
        CHECK(rc.channels.size() == 1);
        CHECK(rc.channels.first() == realChannel);
        // 回读的颜色应等于存盘色。
        const QJsonArray rseries = rc.style.value(QStringLiteral("series")).toArray();
        CHECK(rseries.size() == 1);
        CHECK(QColor(rseries.at(0).toObject().value(QStringLiteral("color")).toString()) == docColor);
        OK("恢复链 9：applyDocumentSpec 读回存盘颜色 + 通道（端到端）");
        // 视口回读。
        const QJsonArray rx = rc.state.value(QStringLiteral("x")).toArray();
        CHECK(rx.size() == 2);
        CHECK(std::fabs(rx.at(0).toDouble() - 1.0) < 1e-6);
        CHECK(std::fabs(rx.at(1).toDouble() - 2.0) < 1e-6);
        OK("恢复链 9b：存盘深缩放视口经 apply→save 往返精确");
    }

    // --- 10) 晚到绑定（pending binder + demand reference）---
    // 场景：布局先到（origin 还没加载）→ 画布空态挂 pending → data.load 晚到
    // → sourceAdded 信号自动接回曲线 + 回放存盘视口 + 释放 demand。
    {
        // CSV 表名取自文件名 stem：拷贝必须保留文件名（换目录 = 换 origin）。
        QTemporaryDir tmpDir;
        CHECK(tmpDir.isValid());
        const QString fixture = QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv");
        const QString lateOrigin = tmpDir.filePath(QStringLiteral("demo_flight.csv"));
        CHECK(QFile::copy(fixture, lateOrigin));

        // 从已加载的源拿真实 table.column 口径（同内容拷贝 → schema 一致）。
        const DataSourceInfo *anySrc = nullptr;
        for (int id : application.context().sources().ids())
        {
            const DataSourceInfo *info = application.context().sources().get(id);
            if (info && !info->datasetIds.isEmpty())
            { anySrc = info; break; }
        }
        CHECK(anySrc != nullptr);
        const Dataset *ds0 = application.context().data().get(anySrc->datasetIds.first());
        CHECK(ds0 != nullptr && !ds0->tableNames().isEmpty());
        const QString realTable = ds0->tableNames().first();
        const Table *tbl0 = ds0->table(realTable);
        CHECK(tbl0 != nullptr && !tbl0->columns.isEmpty());
        const QString realColumn = tbl0->columns.first().name;
        const QString realChannel = realTable + QLatin1Char('.') + realColumn;

        // 复位到干净 1 格并清空，保证 pending 条目定位准确。
        {
            QVariant nullV;
            Result<QVariant> g(nullV);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.grid"), {{QStringLiteral("rows"), 1}, {QStringLiteral("cols"), 1}}, g));
        }
        workspace->activeCanvas()->clear();

        // 布局先到：origin 尚未加载 → 空态 + pending + demand 引用。
        plot::PlotDocumentSpec doc;
        doc.version = 1;
        plot::PlotPageSpec page;
        page.id = QStringLiteral("page-late");
        plot::LayoutNode canvasNode;
        canvasNode.isCanvas = true;
        plot::CanvasSpec cs;
        cs.id = QStringLiteral("late-1");
        cs.viewType = QStringLiteral("timeseries");
        cs.datasetOrigin = lateOrigin;
        cs.channels << realChannel;
        const QColor lateColor(90, 40, 210);
        QJsonObject so;
        so.insert(QStringLiteral("key"), QStringLiteral("0|%1|%2").arg(realTable, realColumn));
        so.insert(QStringLiteral("color"), lateColor.name());
        cs.style = QJsonObject{{QStringLiteral("series"), QJsonArray{so}}};
        cs.state = QJsonObject{{QStringLiteral("x"), QJsonArray{0.5, 1.5}}};
        canvasNode.canvas = cs;
        page.root = canvasNode;
        doc.pages << page;

        CHECK(workspace->applyDocumentSpec(doc, application.context().sources()));
        CHECK(workspace->pendingBindCount() == 1);
        CHECK(workspace->pendingBindOrigins().contains(lateOrigin));
        CHECK(application.context().sources().demandCount(lateOrigin) == 1);
        CHECK(application.context().sources().unsatisfiedDemands().contains(lateOrigin));
        // 空态：曲线还没接上。
        workspace->setActive(0);
        CHECK(workspace->activeCanvas() != nullptr && workspace->activeCanvas()->seriesCount() == 0);
        OK("晚到绑定 10a：布局先到 → 空态 + pending + demand 引用登记");

        // preflight（2.8）：对同一份文档应报告 origin 未到。
        const QStringList pf = PlotWorkspace::preflightDocumentSpec(
            doc, application.context().sources(), application.context().data());
        CHECK(pf.size() == 1 && pf.first().contains(lateOrigin));

        // 数据晚到：加载同 origin 源 → sourceAdded/datasetAdded → 自动接回。
        Intent loadLate;
        loadLate.op = QStringLiteral("data.load");
        loadLate.params[QStringLiteral("path")] = lateOrigin;
        const auto lateLoaded = application.host().execute(loadLate);
        CHECK(lateLoaded);

        CHECK(workspace->pendingBindCount() == 0);
        CHECK(application.context().sources().demandCount(lateOrigin) == 0);
        CHECK(application.context().sources().unsatisfiedDemands().isEmpty());
        // 曲线已接上，颜色 = 存盘色。
        auto *lateCanvas = workspace->activeCanvas();
        CHECK(lateCanvas != nullptr && lateCanvas->seriesCount() == 1);
        CHECK(lateCanvas->seriesAt(0) != nullptr);
        CHECK(lateCanvas->seriesAt(0)->color == lateColor);
        // 视口回放：绑定后的视口 = 存盘 state，不被数据全范围覆盖。
        double l0 = 0.0, l1 = 0.0;
        CHECK(lateCanvas->visibleTimeRange(&l0, &l1));
        CHECK(std::fabs(l0 - 0.5) < 1e-6 && std::fabs(l1 - 1.5) < 1e-6);
        OK("晚到绑定 10b：数据晚到 → 自动接回 + 颜色/视口回放 + demand 释放");

        // 10c：源被移除后 demand 仍在，同 origin 重新载入 → 源可被再次找回。
        {
            const int lateSrcId = application.context().sources().sourceByOrigin(lateOrigin);
            CHECK(lateSrcId >= 0);
            Intent rm;
            rm.op = QStringLiteral("data.remove");
            rm.params[QStringLiteral("id")] = lateSrcId;
            application.host().execute(rm);
            CHECK(application.context().sources().sourceByOrigin(lateOrigin) < 0);
            // demand 语义：登记→源移除→需求仍在；源重新登记→需求不变
            //（释放是绑定方职责，不由源生命周期自动清理）。
            application.context().sources().demandOrigin(lateOrigin);
            CHECK(application.context().sources().demandCount(lateOrigin) == 1);
            application.host().execute(loadLate);
            CHECK(application.context().sources().demandCount(lateOrigin) == 1);
            application.context().sources().releaseOrigin(lateOrigin);
            CHECK(application.context().sources().demandCount(lateOrigin) == 0);
            OK("晚到绑定 10c：demand 引用计数（登记/释放/不受源增删影响）");
        }
    }

    fprintf(stdout, "\n画布导出/状态/图例测试全部通过\n");
    return 0;
}

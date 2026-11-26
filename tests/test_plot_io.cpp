// 画布导出 / 视图状态 / 图例开关 契约测试（fastplot 库能力接进 ng 后的视图薄包装）。
//
// 验证：AxesView 把库的 exportPng/exportCsv/saveState/restoreState/legend
// 正确暴露出来；导出文件真实落盘且非空；视图状态可 round-trip。

#include "App.h"
#include "MainWindow.h"
#include "AxesView.h"
#include "PlotWorkspace.h"
#include "PlotDocument.h"
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
#include <memory>

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

    // --- 11) 数据集替换重绑（rebindCurveKeys 对标）---
    // 场景：曲线绑在 datasetId 上，源被 remove 后重载（换 origin 拷贝）会拿到
    // 新 datasetId——曲线按 table.column 自动接回，显式色/配方保留，不删曲线。
    {
        // 专用独占通道（psReb.val 全仓唯一）：测试 9/10 遗留的 demo_flight
        // 数据集不携带它，保证"无处可绑"与"唯一可绑"的断言确定性。
        QTemporaryDir tmpA;
        CHECK(tmpA.isValid());
        const QString originA = tmpA.filePath(QStringLiteral("psReb.csv"));
        {
            QFile f(originA);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("time_ms,psReb.val\n0,1.0\n4,1.5\n8,2.0\n");
            f.close();
        }
        Intent loadA;
        loadA.op = QStringLiteral("data.load");
        loadA.params[QStringLiteral("path")] = originA;
        const auto la = application.host().execute(loadA);
        CHECK(la);
        const int srcA = application.context().sources().sourceByOrigin(originA);
        CHECK(srcA >= 0);
        const int dsA = application.context().sources().get(srcA)->datasetIds.first();
        CHECK(dsA >= 0);

        // 真实 table.column（schema 动态取，不写死表头）。
        const Dataset *ds = application.context().data().get(dsA);
        CHECK(ds != nullptr && !ds->tableNames().isEmpty());
        const QString tbl = ds->tableNames().first();
        const QString col = ds->table(tbl)->columns.first().name;
        const QString channel = tbl + QLatin1Char('.') + col;

        // 复位干净 1 格并清空，再经文档恢复绑一条显式色曲线。
        {
            QVariant nullV;
            Result<QVariant> g(nullV);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.grid"), {{QStringLiteral("rows"), 1}, {QStringLiteral("cols"), 1}}, g));
        }
        workspace->activeCanvas()->clear();

        plot::PlotDocumentSpec doc;
        doc.version = 1;
        plot::PlotPageSpec page;
        plot::LayoutNode node;
        node.isCanvas = true;
        plot::CanvasSpec cs;
        cs.id = QStringLiteral("rebind-1");
        cs.viewType = QStringLiteral("timeseries");
        cs.datasetOrigin = originA;
        cs.channels << channel;
        const QColor rbColor(15, 130, 220);
        QJsonObject so;
        so.insert(QStringLiteral("key"), QStringLiteral("%1|%2|%3").arg(dsA).arg(tbl, col));
        so.insert(QStringLiteral("color"), rbColor.name());
        cs.style = QJsonObject{{QStringLiteral("series"), QJsonArray{so}}};
        node.canvas = cs;
        page.root = node;
        doc.pages << page;
        CHECK(workspace->applyDocumentSpec(doc, application.context().sources()));
        workspace->setActive(0);
        auto *rbCanvas = workspace->activeCanvas();
        CHECK(rbCanvas && rbCanvas->seriesCount() == 1);
        CHECK(rbCanvas->seriesAt(0)->datasetId == dsA);
        CHECK(rbCanvas->seriesAt(0)->color == rbColor);

        // 源被移除：曲线保留（kept 语义，不随数据集消失），重绑无处可去返回 0。
        Intent rmA;
        rmA.op = QStringLiteral("data.remove");
        rmA.params[QStringLiteral("id")] = srcA;
        application.host().execute(rmA);
        CHECK(application.context().sources().sourceOfDataset(dsA) < 0);
        CHECK(rbCanvas->seriesCount() == 1);
        CHECK(workspace->rebindStaleCurves() == 0);

        // 同内容拷贝换 origin 重载 → 新 datasetId；信号触发自动重绑。
        QTemporaryDir tmpB;
        CHECK(tmpB.isValid());
        const QString originB = tmpB.filePath(QStringLiteral("psReb.csv"));
        {
            QFile f(originB);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("time_ms,psReb.val\n0,1.0\n4,1.5\n8,2.0\n");
            f.close();
        }
        Intent loadB;
        loadB.op = QStringLiteral("data.load");
        loadB.params[QStringLiteral("path")] = originB;
        const auto lb = application.host().execute(loadB);
        CHECK(lb);
        const int srcB = application.context().sources().sourceByOrigin(originB);
        CHECK(srcB >= 0);
        const int dsB = application.context().sources().get(srcB)->datasetIds.first();
        CHECK(dsB >= 0 && dsB != dsA);
        // 自动重绑（datasetAdded 信号）已接回；再调一次幂等返回 0。
        CHECK(rbCanvas->seriesCount() == 1);
        CHECK(rbCanvas->seriesAt(0)->datasetId == dsB);
        CHECK(rbCanvas->seriesAt(0)->color == rbColor);
        CHECK(workspace->rebindStaleCurves() == 0);
        // 数据真的重新取到：时间范围有限且非零。
        double r0 = 0.0, r1 = 0.0;
        CHECK(rbCanvas->visibleTimeRange(&r0, &r1));
        CHECK(std::isfinite(r0) && std::isfinite(r1));
        OK("重绑 11：dataset 替换 → 曲线按 table.column 接回 + 显式色保留 + 幂等");
    }

    // --- 12) planDatasetChoice：同源多数据集按通道可满足性选最佳拟合 ---
    {
        // 造两个通道面不同的数据集：同名 stem（表名相同才有共享通道），
        // A 少一列，B 多一列 extra.val。
        QTemporaryDir tmp12;
        CHECK(tmp12.isValid());
        const QString dirA = tmp12.filePath(QStringLiteral("a"));
        const QString dirB = tmp12.filePath(QStringLiteral("b"));
        CHECK(QDir().mkpath(dirA) && QDir().mkpath(dirB));
        const QString pA = QDir(dirA).filePath(QStringLiteral("psChX.csv"));
        const QString pB = QDir(dirB).filePath(QStringLiteral("psChX.csv"));
        {
            QFile fa(pA), fb(pB);
            CHECK(fa.open(QIODevice::WriteOnly | QIODevice::Text));
            fa.write("time_ms,att.roll[deg]\n0,1.0\n4,1.5\n8,2.0\n");
            fa.close();
            CHECK(fb.open(QIODevice::WriteOnly | QIODevice::Text));
            fb.write("time_ms,att.roll[deg],extra.val\n0,1.0,10\n4,1.5,11\n8,2.0,12\n");
            fb.close();
        }
        Intent lA;
        lA.op = QStringLiteral("data.load");
        lA.params[QStringLiteral("path")] = pA;
        const auto a = application.host().execute(lA);
        CHECK(a);
        const int srcA12 = application.context().sources().sourceByOrigin(pA);
        CHECK(srcA12 >= 0);
        const int dsA = application.context().sources().get(srcA12)->datasetIds.first();
        Intent lB;
        lB.op = QStringLiteral("data.load");
        lB.params[QStringLiteral("path")] = pB;
        const auto b = application.host().execute(lB);
        CHECK(b);
        const int srcB12 = application.context().sources().sourceByOrigin(pB);
        CHECK(srcB12 >= 0);
        const int dsB = application.context().sources().get(srcB12)->datasetIds.first();
        CHECK(dsA >= 0 && dsB >= 0 && dsA != dsB);

        const Dataset *dA = application.context().data().get(dsA);
        const Dataset *dB = application.context().data().get(dsB);
        CHECK(dA && dB && !dA->tableNames().isEmpty() && !dB->tableNames().isEmpty());
        const QString tblA = dA->tableNames().first();
        const QString tblB = dB->tableNames().first();
        CHECK(tblA == tblB); // 同 stem → 表名相同，通道才有重叠
        // 共享通道：A 有、B 也有；独占通道：只有 B 有。
        QString chCommon, chOnlyB;
        for (const auto &cd : dA->table(tblA)->columns)
            if (dB->table(tblB)->column(cd.name))
            { chCommon = tblA + QLatin1Char('.') + cd.name; break; }
        for (const auto &cd : dB->table(tblB)->columns)
            if (!dA->table(tblA)->column(cd.name))
            { chOnlyB = tblB + QLatin1Char('.') + cd.name; break; }
        CHECK(!chCommon.isEmpty() && !chOnlyB.isEmpty());

        // 合成源：两个数据集挂在同一 origin 下。
        DataSourceInfo synth;
        synth.id = -1;
        synth.origin = QStringLiteral("synthetic-choice");

        // B 携带全部通道（2/2）、A 只有 1/2 → 无论登记序，最佳拟合都是 B。
        synth.datasetIds = {dsA, dsB};
        CHECK(workspace->chooseDatasetFor(synth, {chCommon, chOnlyB}) == dsB);
        synth.datasetIds = {dsB, dsA};
        CHECK(workspace->chooseDatasetFor(synth, {chCommon, chOnlyB}) == dsB);

        // 单通道拟合并列（1/1）→ 取登记序第一个（PJ4 "take first and says so"）。
        synth.datasetIds = {dsA, dsB};
        CHECK(workspace->chooseDatasetFor(synth, {chCommon}) == dsA);
        synth.datasetIds = {dsB, dsA};
        CHECK(workspace->chooseDatasetFor(synth, {chCommon}) == dsB);

        // 单数据集源零开销直通。
        synth.datasetIds = {dsA};
        CHECK(workspace->chooseDatasetFor(synth, {chCommon, chOnlyB}) == dsA);
        OK("多数据集 12：planDatasetChoice 最佳拟合优先 / 并列取首 / 单集直通");
    }

    // --- 13) visibility 单一出口：五路径收敛 + 双侧一致 + 图例四角持久化（第3仗 3a）---
    {
        canvas->clear();
        QVector<LodTriple> lod13;
        lod13.reserve(100);
        for (int i = 0; i < 100; ++i)
        {
            const double x = i * 0.01;
            const double y = std::sin(i * 0.3);
            lod13.push_back(LodTriple{x, y - 0.02, y + 0.02});
        }
        canvas->setChannel(0, QStringLiteral("trace"), QStringLiteral("sig"),
                           QStringLiteral("trace.sig"));
        canvas->setLodData(lod13, 0.0, 0.99);
        // 稳定键 = datasetId|table|column（datasetId=0 → "0|trace|sig"）
        const QString key13 = QStringLiteral("0|trace|sig");
        auto *fp13 = canvas->plotForTest();
        CHECK(fp13 != nullptr);
        // 探针必须放堆上、由 lambda 按值捕获。连接挂在 canvas 上、寿命远长于本块，
        // 按引用捕获局部量会在块结束后变成悬垂栈引用：后续任何 setSeriesVisible 触发
        // 这个信号都会读已销毁的栈槽（ASan: stack-use-after-scope；clang -O3 下必崩）。
        struct VisibilityProbe
        {
            int emits = 0;
            QString key;
            bool val = false;
        };
        auto probe13 = std::make_shared<VisibilityProbe>();
        QObject::connect(canvas, &AxesView::seriesVisibilityChanged, [probe13](const QString &k, bool v) {
            ++probe13->emits;
            probe13->key = k;
            probe13->val = v;
        });

        // 一致性：store（单一事实源）与 fastplot Line 必须同值。
        auto consistent13 = [&]() {
            const PlotSeries *s = canvas->seriesByKey(key13);
            CHECK(s != nullptr);
            CHECK(s->visible == fp13->legendItemVisible(0));
            return s->visible;
        };

        // 路径①（右键菜单 Interaction:424 / 属性面板 PropertyProviders:198 /
        //         曲线动作 SeriesActions:114 → setSeriesVisible）：
        probe13->emits = 0;
        CHECK(canvas->setSeriesVisible(key13, false));
        CHECK(consistent13() == false);
        CHECK(probe13->emits == 1); // 恰好一次广播
        CHECK(probe13->key == key13 && probe13->val == false);

        // 同值重复置：无广播、返回 true
        CHECK(canvas->setSeriesVisible(key13, false));
        CHECK(probe13->emits == 1);

        // 路径②（图例行点击 / 拖拽 Interaction:244 → Axes::toggleLegendItem）：
        // Line 先被 Axes 切换，回声信号收敛进 setSeriesVisible 同步 store。
        probe13->emits = 0;
        fp13->toggleLegendItem(0);
        CHECK(consistent13() == true);
        CHECK(probe13->emits == 1 && probe13->val == true);

        // 路径③（出口内部同步用的 set 版 Axes::setLegendItemVisible）：
        probe13->emits = 0;
        fp13->setLegendItemVisible(0, false);
        CHECK(consistent13() == false);
        CHECK(probe13->emits == 1);

        // 路径④（toggleSeriesVisible：仍经 toggleLegendItem 收敛，不漂移）
        probe13->emits = 0;
        CHECK(canvas->toggleSeriesVisible(key13));
        CHECK(consistent13() == true);
        CHECK(probe13->emits == 1);

        // 五路径无漂移兜底：无论从哪条进，store/Line/广播三方恒一致。
        for (int round = 0; round < 2; ++round)
        {
            const bool expect = (round % 2 == 0) ? false : true;
            CHECK(canvas->setSeriesVisible(key13, expect));
            CHECK(consistent13() == expect);
        }
        OK("visibility 单一出口：菜单/属性/动作/图例点击/set 版 五路径三方一致");

        // 图例四角：AxesView 转发 + saveViewState/restoreViewState round-trip
        //（经 Axes::saveState 的 legendCorner 键走既有通道，不自建第二通道）。
        CHECK(canvas->legendCorner() == 0); // 默认右上
        canvas->setLegendCorner(2);        // 左下
        CHECK(canvas->legendCorner() == 2);
        const QJsonObject st13 = canvas->saveViewState();
        CHECK(st13.contains(QStringLiteral("legendCorner")));
        canvas->setLegendCorner(0);
        CHECK(canvas->restoreViewState(st13));
        CHECK(canvas->legendCorner() == 2);
        // 越界值忽略
        canvas->setLegendCorner(7);
        CHECK(canvas->legendCorner() == 2);
        OK("图例四角：saveViewState/restoreViewState round-trip，越界忽略");
        canvas->clear();
    }

    // --- 14) 全工作区快照 undo：连续手势=1 步 + 结构变更可回退（第3仗 3b）---
    {
        auto *hist = workspace->history();
        CHECK(hist != nullptr);
        hist->flushPending(); // 结算遗留脏标记：首个结算只立基线，不产生步数
        const int baselineSteps = hist->stepCount();

        // 真实加载数据集（undo 恢复链经 origin 回绑，虚拟 datasetId 无源可绑）。
        const QString fixture14 = QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv");
        int src14 = application.context().sources().sourceByOrigin(fixture14);
        if (src14 < 0)
        {
            Intent load14;
            load14.op = QStringLiteral("data.load");
            load14.params[QStringLiteral("path")] = fixture14;
            CHECK(application.host().execute(load14));
            src14 = application.context().sources().sourceByOrigin(fixture14);
        }
        CHECK(src14 >= 0);
        const int ds14 = application.context().sources().get(src14)->datasetIds.first();
        const Dataset *d14 = application.context().data().get(ds14);
        CHECK(d14 != nullptr && !d14->tableNames().isEmpty());
        const QString tbl14 = d14->tableNames().first();
        const auto *t14 = d14->table(tbl14);
        CHECK(t14 != nullptr && !t14->columns.isEmpty());
        const QString col14 = t14->columns.first().name;

        auto *c14 = workspace->activeCanvas();
        CHECK(c14 != nullptr);
        c14->clear();
        c14->setChannel(ds14, tbl14, col14, QStringLiteral("undo.demo"));
        QVector<LodTriple> lod14;
        lod14.reserve(100);
        for (int i = 0; i < 100; ++i)
        {
            const double x = i * 0.01;
            const double y = std::sin(i * 0.3);
            lod14.push_back(LodTriple{x, y - 0.02, y + 0.02});
        }
        c14->setLodData(lod14, 0.0, 0.99);
        const QString key14 = QStringLiteral("%1|%2|%3").arg(ds14).arg(tbl14, col14);
        CHECK(c14->seriesByKey(key14) != nullptr);
        // 结算曲线添加（本身也是一步；首个基线建立时可能被吸收为 0 步）
        hist->flushPending();
        const int stepsAfterAdd = hist->stepCount();
        CHECK(stepsAfterAdd >= baselineSteps);

        // burst：连续三次显隐切换（连续手势）→ 一次 undo 应整体回到"可见"基线
        CHECK(c14->setSeriesVisible(key14, false));
        CHECK(c14->setSeriesVisible(key14, true));
        CHECK(c14->setSeriesVisible(key14, false));
        CHECK(hist->canUndo());
        CHECK(workspace->undoHistory());
        auto *c14b = workspace->activeCanvas(); // applyDocumentSpec 后重取
        const PlotSeries *s14 = c14b ? c14b->seriesByKey(key14) : nullptr;
        CHECK(s14 != nullptr);
        CHECK(s14->visible == true); // 回到 burst 前（经 style.series[].visible 回放）
        CHECK(hist->stepCount() == stepsAfterAdd); // 连续手势 = 1 步

        // 结构变更可回退：拆分（经 view action，与 ribbon 同路径）→ undo 回到拆分前
        const QJsonObject beforeSplit = workspace->toDocumentSpec().toJson();
        {
            QVariant nullV14;
            Result<QVariant> splitRes(nullV14);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.splitHorizontal"), {}, splitRes));
            CHECK(splitRes.value().toBool());
        }
        CHECK(workspace->toDocumentSpec().toJson() != beforeSplit);
        CHECK(workspace->undoHistory());
        CHECK(workspace->toDocumentSpec().toJson() == beforeSplit);
        OK("全局 undo 14：连续显隐=1 步回放 + 拆分回退 + 步数断言");
        workspace->activeCanvas()->clear();
    }

    // --- 15) XY 手势建图 + 参考线 view action（第3仗 3c）---
    {
        // XY：demo 数据集两列（roll 作 X、pitch 作 Y）。复用测试 14 已加载的源。
        const QString fixture15 = QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv");
        int src15 = application.context().sources().sourceByOrigin(fixture15);
        CHECK(src15 >= 0);
        const int ds15 = application.context().sources().get(src15)->datasetIds.first();
        const Dataset *d15 = application.context().data().get(ds15);
        CHECK(d15 != nullptr && !d15->tableNames().isEmpty());
        const QString tbl15 = d15->tableNames().first();
        const auto *t15 = d15->table(tbl15);
        CHECK(t15 != nullptr && t15->columns.size() >= 3);
        const QString colX = QStringLiteral("att.roll[deg]");
        const QString colY = QStringLiteral("att.pitch[deg]");
        CHECK(t15->column(colX) != nullptr && t15->column(colY) != nullptr);

        auto *c15 = workspace->activeCanvas();
        CHECK(c15 != nullptr);
        c15->clear();
        // X 源：画布上先放一条曲线（首曲线作 X 通道）
        c15->setChannel(ds15, tbl15, colX, QStringLiteral("xy.src"));
        QVector<LodTriple> lod15;
        for (int i = 0; i < 100; ++i)
            lod15.push_back(LodTriple{i * 0.01, 0.0, 0.0});
        c15->setLodData(lod15, 0.0, 0.99);
        const int cellsBefore = workspace->cellCount();
        CHECK(workspace->createXyCanvasFor(ds15, tbl15, colY));
        CHECK(workspace->cellCount() == cellsBefore + 1); // 新格
        auto *xy = workspace->activeCanvas();
        CHECK(xy != nullptr && xy != c15);
        CHECK(xy->plotKind() == QStringLiteral("xy"));
        CHECK(xy->seriesCount() == 1);
        // 落点通道身份保留（重绑/样式通道沿用）：key = ds|tbl|colY
        CHECK(xy->seriesByKey(QStringLiteral("%1|%2|%3").arg(ds15).arg(tbl15, colY)) != nullptr);
        // 撤销一步 = 回到 XY 建图前（全局 undo 覆盖建图手势）
        CHECK(workspace->undoHistory());
        CHECK(workspace->cellCount() == cellsBefore);
        OK("XY 手势建图 15：Ctrl 拖通道 → 独立 XY 格（值-值曲线）+ 全局 undo 可回退");

        // 参考线：view action 设定/清除 + 读数 Δ + 随视图状态持久化
        auto *c15b = workspace->activeCanvas();
        CHECK(c15b != nullptr);
        QVariant nullV15;
        Result<QVariant> refRes(nullV15);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.reference.set"),
            {{QStringLiteral("t"), 0.35}}, refRes));
        CHECK(c15b->hasReference());
        const QJsonObject st15 = c15b->saveViewState();
        CHECK(st15.contains(QStringLiteral("referenceT")));
        c15b->clearReference();
        CHECK(!c15b->hasReference());
        CHECK(c15b->restoreViewState(st15));
        CHECK(c15b->hasReference());
        CHECK(std::abs(c15b->plotForTest()->referenceTime() - 0.35) < 1e-12);
        Result<QVariant> refClear(nullV15);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.reference.clear"), {}, refClear));
        CHECK(!c15b->hasReference());
        OK("参考线 15：view action 设定/清除 + saveViewState round-trip（相对轴值）");
        workspace->activeCanvas()->clear();
    }

    // --- 16) 4.2 双向通道全量 round-trip：toDocumentSpec → apply → toDocumentSpec 全等 ---
    // M4 之前这里零覆盖。画布 id 是运行期分配（apply 重建画布必换 id），
    // 比较前按位置归一化 id；其余字段（页/格/通道/style/state）要求逐位全等。
    {
        std::function<QJsonObject(const QJsonObject &, int &)> normNode =
            [&](const QJsonObject &node, int &counter) -> QJsonObject {
            QJsonObject out = node;
            if (out.value(QStringLiteral("isCanvas")).toBool())
            {
                QJsonObject cv = out.value(QStringLiteral("canvas")).toObject();
                cv.insert(QStringLiteral("id"), QStringLiteral("C%1").arg(counter++));
                out.insert(QStringLiteral("canvas"), cv);
            }
            else
            {
                QJsonArray ch;
                for (const QJsonValue &v : out.value(QStringLiteral("children")).toArray())
                    ch.append(normNode(v.toObject(), counter));
                out.insert(QStringLiteral("children"), ch);
            }
            return out;
        };
        auto normDoc = [&](const QJsonObject &doc) {
            QJsonObject out = doc;
            QJsonArray pages;
            for (const QJsonValue &pv : out.value(QStringLiteral("pages")).toArray())
            {
                QJsonObject page = pv.toObject();
                int counter = 0;
                page.insert(QStringLiteral("root"),
                            normNode(page.value(QStringLiteral("root")).toObject(), counter));
                pages.append(page);
            }
            out.insert(QStringLiteral("pages"), pages);
            return out;
        };

        // 干净 1 页 1 格 + 富状态：显式色 + 隐藏 + 图例四角 + 深缩放视口。
        QVariant nullV16;
        Result<QVariant> g16(nullV16);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.grid"),
            {{QStringLiteral("rows"), 1}, {QStringLiteral("cols"), 1}}, g16));
        auto *c16 = workspace->activeCanvas();
        CHECK(c16 != nullptr);
        c16->clear();
        QVector<LodTriple> lod16;
        lod16.reserve(200);
        for (int i = 0; i < 200; ++i)
        {
            const double x = i * 0.01;
            const double y = std::sin(i * 0.21);
            lod16.push_back(LodTriple{x, y - 0.03, y + 0.03});
        }
        c16->setChannel(0, QStringLiteral("trace"), QStringLiteral("sig"),
                        QStringLiteral("trace.sig"));
        c16->setLodData(lod16, 0.0, 1.99);
        const QString key16 = QStringLiteral("0|trace|sig");
        const QColor color16(60, 180, 75);
        CHECK(c16->setSeriesInk(key16, color16));
        CHECK(c16->setSeriesVisible(key16, false)); // 显隐随布局存取（PR 3b 补齐）
        // 注意顺序：restoreViewState 是全量状态恢复，缺键的 legendCorner 会回落默认
        // 右上——先恢复视口，再设四角（真实存档 state 是 saveViewState 全量，无此问题）。
        QJsonObject vp16;
        vp16.insert(QStringLiteral("x"), QJsonArray{0.4, 0.9}); // 深缩放（相对秒视口）
        CHECK(c16->restoreViewState(vp16));
        c16->setLegendCorner(2);                    // 左下（PR 3a）
        c16->setLegendVisible(false);               // 图例整体显隐（单一状态通道）
        c16->setReferenceTime(0.6);                 // 参考线（相对轴值，随视图状态持久化）

        // round-trip ×2：第一次 apply 落运行时，第二次验证幂等（不再丢字段）。
        const QJsonObject doc1 = workspace->toDocumentSpec().toJson();
        CHECK(workspace->applyDocumentSpec(plot::PlotDocumentSpec::fromJson(doc1),
                                           application.context().sources()));
        const QJsonObject doc2 = workspace->toDocumentSpec().toJson();
        CHECK(workspace->applyDocumentSpec(plot::PlotDocumentSpec::fromJson(doc2),
                                           application.context().sources()));
        const QJsonObject doc3 = workspace->toDocumentSpec().toJson();

        CHECK(normDoc(doc1) == normDoc(doc2)); // 富状态全量无损往返
        CHECK(normDoc(doc2) == normDoc(doc3)); // apply 幂等

        // 运行时逐项复核：不看 JSON 字符串，验证恢复出来的对象真的就位。
        auto *c16b = workspace->activeCanvas();
        CHECK(c16b != nullptr);
        const PlotSeries *s16 = c16b->seriesByKey(key16);
        CHECK(s16 != nullptr);
        CHECK(s16->color == color16);
        CHECK(s16->visible == false);
        CHECK(c16b->legendCorner() == 2);
        CHECK(c16b->legendVisible() == false);
        CHECK(c16b->hasReference()); // 参考线经全文档 round-trip 仍在（AxesView 无值读口，存档侧含 0.6）
        double vx0 = 0.0, vx1 = 0.0;
        CHECK(c16b->visibleTimeRange(&vx0, &vx1));
        CHECK(std::fabs(vx0 - 0.4) < 1e-9 && std::fabs(vx1 - 0.9) < 1e-9);
        OK("双向通道 16：toDocumentSpec→applyDocumentSpec 全量 round-trip（色/显隐/四角/视口）+ 幂等");
        workspace->activeCanvas()->clear();
    }

    fprintf(stdout, "\n画布导出/状态/图例测试全部通过\n");
    return 0;
}

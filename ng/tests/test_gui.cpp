// GUI 契约测试：验证 ribbon / docking / 画布 / ps 数据呈现这条链路。
//
// 与 headless 契约测试（test_architecture.cpp）分开：GUI 依赖 Qt Widgets，
// 必须在有 QApplication 的环境下跑。CI 里用 offscreen 平台插件。
//
// 检查项：
//   1) ribbon 按插槽数据渲染：页签数、分组数、动作数；
//   2) 器官在 GUI 环境下能申请到部件（requestPanel -> 骨架工厂）；
//   3) 停靠区就位（dock.left 有面板、标题正确）；
//   4) ps 数据呈现：plot.channel 意图返回**像素级有界**数据，
//      且点数 <= px（与数据行数无关）——这是百万级数据能画出来的关键；
//   5) 数据集管理面板能列出数据集/表/列；
//   6) 模块自注册：GUI 装载后器官全部激活。

#include "App.h"

#include "DataManagerPanel.h"
#include "DockManager.h"
#include "GuiContext.h"
#include "MainWindow.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "RibbonBar.h"

#include "Dataset.h"
#include "Intent.h"
#include "ModuleHost.h"
#include "UiSlotRegistry.h"

#include <QApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

using namespace ng::app;
using namespace ng::core;
using namespace ng::gui;
using namespace ng::host;

// Release 下 assert 被 NDEBUG 消除，用它做真实校验（失败即退出码非零）。
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            std::exit(1);                                                                \
        }                                                                                \
    } while (0)

#define OK(msg) fprintf(stdout, "[ok] %s\n", msg)

// 造一个 5000 行的内存数据集（三列），验证绘制路径的有界性。
static int buildDataset(DataRepository &repo)
{
    Dataset ds = Dataset::create(QStringLiteral("gui-test"), QStringLiteral("mem"));
    QVector<double> ts, a, b;
    ts.reserve(5000);
    a.reserve(5000);
    b.reserve(5000);
    for (int i = 0; i < 5000; ++i)
    {
        ts.append(i * 0.001);
        a.append(i * 0.5);
        b.append(1000.0 - i * 0.25);
    }
    // addTable 返回表索引（0 是合法值，不能用真假判断成功与否）
    if (ds.addTable(QStringLiteral("trace"), ts) < 0)
        return -1;
    if (!ds.addF64Column(QStringLiteral("trace"), QStringLiteral("sig"), QString(), a))
        return -1;
    if (!ds.addF64Column(QStringLiteral("trace"), QStringLiteral("alt"), QString(), b))
        return -1;
    ds.refreshSchema();
    return repo.add(ds);
}

int main(int argc, char *argv[])
{
    // offscreen：无显示环境也能跑。
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    App application;
    MainWindow window(application.data());
    window.build(application.modules());
    // 先给窗口一个确定尺寸：画布按自身像素宽请求数据，尺寸确定后断言才可复现。
    window.resize(1280, 800);

    // --- 1) 器官在 GUI 环境下全部激活（静态自注册生效）---
    const auto active = application.modules().activeIds();
    CHECK(active.contains(QStringLiteral("mod.data")));
    CHECK(active.contains(QStringLiteral("mod.plot")));
    CHECK(active.contains(QStringLiteral("mod.script")));
    OK("器官自注册装载：data / plot / script 全部激活");

    // --- 2) ribbon 按插槽数据渲染 ---
    RibbonBar *ribbon = window.ribbon();
    CHECK(ribbon != nullptr);
    CHECK(ribbon->pageCount() >= 1);
    CHECK(ribbon->pageAreas().contains(QStringLiteral("ribbon.page.home")));
    // 骨架声明了两个 ribbon 分组：数据 / 视图
    CHECK(ribbon->groupCount(QStringLiteral("ribbon.page.home")) >= 2);
    // 绘图模块在 onAttach 里往"视图"分组放了"重绘"动作
    CHECK(ribbon->actionCount(QStringLiteral("ribbon.group.home.view")) >= 1);
    OK("ribbon 按插槽渲染：页签/分组/动作数量符合声明");

    // --- 3) 器官 requestPanel 拿到部件，骨架只提供工厂 ---
    CHECK(!PanelFactoryRegistry::types().isEmpty());
    CHECK(PanelFactoryRegistry::types().contains(QStringLiteral("dataset-tree")));
    OK("部件工厂表就位：器官只描述类型，骨架决定实现");

    // --- 4) 停靠区就位 ---
    DockManager *docks = window.docks();
    CHECK(docks->dockCount() >= 1);
    CHECK(docks->widget(QStringLiteral("dock.left")) != nullptr);
    CHECK(!docks->title(QStringLiteral("dock.left")).isEmpty());
    OK("停靠区就位：左栏有数据集面板且标题正确");

    // --- 5) 数据集管理面板能列出数据集 -> 表 -> 列 ---
    DataManagerPanel *panel = window.dataPanel();
    CHECK(panel != nullptr);
    const int dsId = buildDataset(application.data());
    CHECK(dsId >= 0);
    panel->refresh();
    CHECK(panel->datasetNodeCount() == 1);
    CHECK(panel->tableNodeCount(0) == 1); // 一张表 trace
    OK("数据集管理面板：数据集/表层级正确");

    // --- 6) ps 数据呈现：plot.channel 返回像素级有界数据 ---
    // 关键断言：5000 行数据，请求 px=200，应返回 <= 200 个三元组——
    // 证明绘图路径与行数解耦（百万行同理）。
    const int px = 200;
    Intent intent;
    intent.op = QStringLiteral("plot.channel");
    intent.params[QStringLiteral("dataset")] = dsId;
    intent.params[QStringLiteral("table")] = QStringLiteral("trace");
    intent.params[QStringLiteral("column")] = QStringLiteral("sig");
    intent.params[QStringLiteral("px")] = px;

    const auto result = application.modules().execute(intent);
    CHECK(result);
    const QVariantMap payload = result.value().payload.toMap();
    const QVariantList xs = payload.value(QStringLiteral("x")).toList();
    const QVariantList mins = payload.value(QStringLiteral("min")).toList();
    const QVariantList maxs = payload.value(QStringLiteral("max")).toList();
    CHECK(!xs.isEmpty());
    CHECK(xs.size() == mins.size());
    CHECK(xs.size() == maxs.size());
    CHECK(xs.size() <= px); // 有界：与 5000 行无关
    // 抽稀后的 min/max 应覆盖原始范围（sig = i*0.5，0..2499.5）
    double gmax = -1e300, gmin = 1e300;
    for (int i = 0; i < mins.size(); ++i)
    {
        gmin = qMin(gmin, mins.at(i).toDouble());
        gmax = qMax(gmax, maxs.at(i).toDouble());
    }
    CHECK(gmax > 2400.0);
    CHECK(gmin < 100.0);
    OK(QStringLiteral("ps 数据呈现：5000 行抽稀至 %1 个像素三元组（<= px），值域正确")
           .arg(xs.size())
           .toUtf8()
           .constData());

    // --- 7) 画布消费 LOD 并渲染 ---
    // 画布按**自身像素宽度**请求数据（像素级渲染的应有行为），
    // 因此点数与画布宽度挂钩，但与数据行数无关。
    PlotCanvas *canvas = window.canvas();
    CHECK(canvas != nullptr);
    window.showChannel(dsId, QStringLiteral("trace"), QStringLiteral("sig"));
    CHECK(canvas->lodPointCount() > 0);
    const int canvasPx = qMax(64, canvas->width() - 80);
    CHECK(canvas->lodPointCount() <= canvasPx);
    CHECK(!canvas->channelTitle().isEmpty());
    OK(QStringLiteral("画布消费像素级数据并渲染（%1 点，按画布像素宽有界，与行数无关）")
           .arg(canvas->lodPointCount())
           .toUtf8()
           .constData());

    // --- 8) 渲染触发走意图层（与 CLI 同路径）---
    // plot.channel 是公开意图，CLI 与脚本同样可用。
    Intent bad;
    bad.op = QStringLiteral("plot.channel");
    bad.params[QStringLiteral("dataset")] = 9999;
    bad.params[QStringLiteral("table")] = QStringLiteral("trace");
    bad.params[QStringLiteral("column")] = QStringLiteral("sig");
    const auto badResult = application.modules().execute(bad);
    CHECK(!badResult);
    CHECK(badResult.error().code == QStringLiteral("enoent"));
    OK("意图层错误语义一致：不存在的数据集返回 enoent");

    fprintf(stdout, "\nGUI 契约测试全部通过\n");
    return 0;
}

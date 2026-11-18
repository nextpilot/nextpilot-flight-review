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
#include "GuiContext.h"
#include "MainWindow.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "ShellBinder.h"

#include "AppContext.h"

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
    MainWindow window(application.context());
    window.build(application.modules());
    // 先给窗口一个确定尺寸：画布按自身像素宽请求数据，尺寸确定后断言才可复现。
    window.resize(1280, 800);

    // --- 1) 器官在 GUI 环境下全部激活（静态自注册生效）---
    const auto active = application.modules().activeIds();
    CHECK(active.contains(QStringLiteral("mod.data")));
    CHECK(active.contains(QStringLiteral("mod.plot")));
    CHECK(active.contains(QStringLiteral("mod.script")));
    OK("器官自注册装载：data / plot / script 全部激活");

    // --- 2) ribbon 按插槽数据渲染（SARibbon，结构层由第三方负责）---
    ShellBinder *binder = window.binder();
    CHECK(binder != nullptr);
    // 绑定器创建了 SARibbonBar：窗口顶部区就是它
    CHECK(window.menuWidget() != nullptr);
    // 插槽声明了页签/分组；动作由器官在 onAttach 里登记
    const auto &slotReg = application.modules().uiSlots();
    CHECK(slotReg.slotsOfKind(SlotKind::RibbonPage).size() >= 1);
    CHECK(slotReg.slotsOfKind(SlotKind::RibbonGroup).size() >= 2);
    // 绘图模块在 onAttach 里往"视图"分组放了"重绘"动作
    CHECK(slotReg.actionsIn(QStringLiteral("ribbon.group.home.view")).size() >= 1);
    OK("ribbon 按插槽数据绑定：页签/分组/动作数量符合声明");

    // --- 3) 视图层自注册生效：部件工厂表由 views/ 各 TU 自行登记 ---
    //
    // 这里守一条**架构级不变量**：视图自注册依赖 OBJECT 库。
    // 若 ng_gui 被改成 STATIC 库，未被引用的 TU 会被链接器丢弃、
    // 静态初始化不执行，工厂表会**静默变空**——功能消失且无任何报错。
    // 所以这里显式断言两个视图都已登记。
    CHECK(!PanelFactoryRegistry::types().isEmpty());
    CHECK(PanelFactoryRegistry::types().contains(QStringLiteral("dataset-tree")));
    CHECK(PanelFactoryRegistry::types().contains(QStringLiteral("plot-canvas")));
    OK("视图层自注册生效：dataset-tree / plot-canvas 均已登记（OBJECT 库保证 TU 不被丢弃）");

    // --- 4) 停靠区就位（Qt-ADS）---
    CHECK(binder->panelCount() >= 1);
    CHECK(binder->panelWidget(QStringLiteral("dock.left")) != nullptr);
    CHECK(!binder->panelTitle(QStringLiteral("dock.left")).isEmpty());
    OK("停靠区就位：左栏有数据集面板且标题正确");

    // --- 5) 数据集管理面板能列出数据集 -> 表 -> 列 ---
    // 面板由器官 requestPanel 申请、由视图层自注册的工厂创建——
    // MainWindow 不认识它，这里从插槽区取回。
    const int dsId = buildDataset(application.data());
    CHECK(dsId >= 0);
    auto *panel = qobject_cast<DataManagerPanel *>(
        binder->panelWidget(QStringLiteral("dock.left")));
    CHECK(panel != nullptr);
    panel->refresh();
    CHECK(panel->datasetNodeCount() == 1);
    CHECK(panel->tableNodeCount(0) == 1); // 一张表 trace
    OK("数据集管理面板：数据集/表层级正确（面板归视图层，外壳不认识它）");

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
    auto *canvas = qobject_cast<PlotCanvas *>(binder->centralContent());
    CHECK(canvas != nullptr);
    // 渲染触发方式 = 设活动通道（与数据集树双击同一条路径）：
    // 数据集树 -> AppContext 共享状态 -> 绘图视图订阅 -> 取数渲染。
    application.context().setActiveChannel(dsId, QStringLiteral("trace"),
                                           QStringLiteral("sig"));
    CHECK(canvas->lodPointCount() > 0);
    // LOD 桶数必须 <= 画布绘图区像素宽：
    // 桶数与画布宽不匹配就会出现"梳齿"假象（每桶跨多个采样周期，
    // min/max 被撑到值域两端，看起来像方波）。
    // 因此断言用的是画布自己给出的绘图区宽度。
    const int canvasPx = qMax(64, canvas->plotPixelWidth());
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

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
#include "ThemeManager.h"
#include "ExtensionRegistry.h"
#include "IPanelContribution.h"
#include "PanelFactoryRegistry.h"
#include "PlotCanvas.h"
#include "ShellBinder.h"

#include "AppContext.h"
#include "ChannelMime.h"

#include "Dataset.h"
#include "Intent.h"
#include "ModuleHost.h"
#include "UiSlotRegistry.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSet>
#include <QStatusBar>
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
    // 数据模块在 onAttach 里往"数据"分组放了 reload/export/clearCache 三个动作
    const auto dataActs = slotReg.actionsIn(QStringLiteral("ribbon.group.home.data"));
    CHECK(dataActs.size() >= 3);
    bool hasReload = false, hasExport = false;
    for (const auto &entry : dataActs)
    {
        if (entry.action.intentOp == QStringLiteral("data.reload")) hasReload = true;
        if (entry.action.intentOp == QStringLiteral("data.export")) hasExport = true;
    }
    CHECK(hasReload && hasExport); // 动作绑的是真实意图，不是空按钮
    OK("ribbon 按插槽数据绑定：页签/分组/动作数量符合声明");

    // --- 2a-2) 图库是纯数据声明（headless 可读），plot 类型 / parse 类型都在 ---
    {
        // 图型库：12 种图型，按"常用/曲线/地图3D/仪表"分组，缩略图 visual 声明齐全。
        const auto plotActs = slotReg.actionsIn(QStringLiteral("ribbon.group.home.plottype"));
        CHECK(plotActs.size() == 12);
        QSet<QString> groups;
        for (const auto &entry : plotActs)
        {
            CHECK(!entry.action.galleryGroup.isEmpty()); // 都是 gallery 项
            CHECK(!entry.action.visual.isEmpty());       // 都有缩略图种类
            groups.insert(entry.action.galleryGroup);
        }
        CHECK(groups.contains(QStringLiteral("常用")));
        CHECK(groups.contains(QStringLiteral("曲线")));
        CHECK(groups.contains(QStringLiteral("仪表")));
        // 来源库：至少 csv/ulog/custom-protocol 三种格式驱动进了 gallery。
        const auto srcActs = slotReg.actionsIn(QStringLiteral("ribbon.group.home.sources"));
        CHECK(srcActs.size() >= 3);
        bool hasCsv = false, hasCustom = false;
        for (const auto &entry : srcActs)
        {
            CHECK(!entry.action.galleryGroup.isEmpty());
            if (entry.action.id.contains(QStringLiteral("csv"))) hasCsv = true;
            if (entry.action.id.contains(QStringLiteral("custom-protocol"))) hasCustom = true;
        }
        CHECK(hasCsv && hasCustom);
        OK("图库纯数据声明：12 图型分 4 组 + 格式驱动进来源库，headless 可读");
    }

    // --- 2a-3) 快捷键是动作属性（纯数据），骨架动作声明了默认值 ---
    {
        const auto dataActs2 = slotReg.actionsIn(QStringLiteral("ribbon.group.home.data"));
        bool hasOpenShortcut = false;
        for (const auto &entry : dataActs2)
            if (entry.action.id == QStringLiteral("app.action.openData")
                && entry.action.shortcut == QStringLiteral("Ctrl+O"))
                hasOpenShortcut = true;
        CHECK(hasOpenShortcut); // 打开数据 = Ctrl+O（抄老 file.open）
        OK("快捷键作为动作属性：打开数据声明 Ctrl+O");
    }

    // --- 2b) 点击链路端到端：按钮发的是 intentOp，不是把 id 当 op ---
    // 回归点：UiAction 有 id（data.action.reload）与 intentOp（data.reload）两个字段。
    // 曾把 id 当 op 发出，导致除 openData（被外壳特判）外所有按钮点击都报
    // "没有模块能处理意图"。这里真触发 QAction，走
    //   ShellBinder -> dispatcher -> ModuleHost -> handler 全链路，
    // 用状态栏消息断言 op 确实被 handler 认领（业务错误允许，"无人认领"不允许）。
    {
        QStatusBar *status = window.statusBar();
        auto findAction = [&window](const QString &id) -> QAction * {
            const auto all = window.findChildren<QAction *>();
            for (QAction *a : all)
                if (a->data().toString() == id)
                    return a;
            return nullptr;
        };
        const QStringList ids = {QStringLiteral("data.action.reload"),
                                 QStringLiteral("data.action.export"),
                                 QStringLiteral("data.action.clearCache")};
        for (const QString &id : ids)
        {
            QAction *action = findAction(id);
            CHECK(action != nullptr); // 按钮确实渲染出来了
            const int before = binder->actionTriggerCount(id);
            status->clearMessage();
            action->trigger(); // 真实点击
            CHECK(binder->actionTriggerCount(id) == before + 1); // 点击到达绑定器
            // 关键断言：op 必须被 handler 认领。业务错误（如无活动源、未指定导出
            // 路径）是允许的，但绝不能停在"没有模块能处理意图"——那是把 id 当 op 的症状。
            CHECK(!status->currentMessage().contains(QStringLiteral("没有模块能处理意图")));
        }
        OK("点击链路端到端：三个 data 按钮都按 intentOp 找到 handler（不是把 id 当 op）");
    }

    // --- 3) 视图层自注册生效：部件工厂表由 views/ 各 TU 自行登记 ---
    //
    // 这里守一条**架构级不变量**：视图自注册依赖 OBJECT 库。
    // 若 ng_gui 被改成 STATIC 库，未被引用的 TU 会被链接器丢弃、
    // 静态初始化不执行，工厂表会**静默变空**——功能消失且无任何报错。
    // 所以这里显式断言两个视图都已登记。
    CHECK(!ng::core::PanelFactoryRegistry::types().isEmpty());
    CHECK(ng::core::PanelFactoryRegistry::types().contains(QStringLiteral("dataset-tree")));
    CHECK(ng::core::PanelFactoryRegistry::types().contains(QStringLiteral("plot-canvas")));
    OK("视图层自注册生效：dataset-tree / plot-canvas 均已登记（OBJECT 库保证 TU 不被丢弃）");

    // --- 3b) 领域面板：形态随领域变时的解法 ---
    //
    // 有些面板的形态**本身就是领域知识**（CAN 报文浏览器取决于 DBC 定义的信号）。
    // 通用视图层不该认识它，器官也给不出形态 —— 于是形态归**知识包**：
    //   · 描述（IPanelContribution，纯数据，headless 可见）；
    //   · 实现（知识包的呈现层，注册进**同一张**工厂表）。
    //
    // 这里锁住三件事：
    //   1) 知识包声明的面板清单能被查到（headless 也可查）；
    //   2) 该面板的实现确实注册进了工厂表（描述与实现用同一个 id 关联）；
    //   3) 按领域过滤有效。
    {
        // 1) 描述：纯数据，headless 也查得到。
        const auto &toolbox = application.context().toolbox();
        int panelSpecCount = 0;
        for (ng::core::IToolContribution *c :
             toolbox.contributions(ng::core::ContributionKind::Panel))
        {
            auto *pc = dynamic_cast<ng::core::IPanelContribution *>(c);
            CHECK(pc != nullptr);
            if (pc)
                panelSpecCount += pc->panels().size();
        }
        CHECK(panelSpecCount >= 1); // 汽车包的 CAN 报文面板

        // 2) 实现：领域面板与通用面板共用同一张工厂表。
        CHECK(ng::core::PanelFactoryRegistry::types().contains(QStringLiteral("car.can-browser")));

        // 3) 领域过滤：汽车领域的清单里有它，通用领域不该混入。
        int automotive = 0;
        for (ng::core::IToolContribution *c :
             toolbox.contributions(ng::core::ContributionKind::Panel,
                                  ng::core::FormatDomain::Automotive))
        {
            auto *pc = dynamic_cast<ng::core::IPanelContribution *>(c);
            if (pc)
                automotive += pc->panels().size();
        }
        CHECK(automotive >= 1);
        OK("领域面板：知识包声明描述（headless 可见）+ 呈现层注册实现，共用同一张工厂表");
    }

    // --- 3c) 模块侧视角：器官不认识面板实现，也能查询清单并请求到面板 ---
    //
    // 这才是用户问题的落点：modules 需要某个 panel，但 panel 的形态随业务变、
    // gui/views 并不知道。器官只认**领域**，先问 availableDomainPanels(domain)，
    // 再按 id 请求；面板从哪来、长什么样，全由知识包决定。
    {
        auto *ui = window.context(); // 模块 onAttach 拿到的就是这个 UiContext
        CHECK(ui != nullptr);

        // 器官按领域查清单（不写死 id）。
        const auto specs = ui->availableDomainPanels(FormatDomain::Automotive);
        const PanelSpec *canSpec = nullptr;
        for (const PanelSpec &s : specs)
            if (s.id == QStringLiteral("car.can-browser"))
                canSpec = &s;
        CHECK(canSpec != nullptr);
        CHECK(canSpec->label == QStringLiteral("CAN 报文"));
        CHECK(canSpec->area == QStringLiteral("dock.right"));

        // 器官按清单里的 id 请求面板——它不知道这是领域面板还是通用面板。
        QWidget *canPanel = ui->requestDomainPanel(canSpec->area, canSpec->id);
        CHECK(canPanel != nullptr);
        // 标题取面板自己的描述，而非通用插槽的"属性"：形态由领域决定。
        CHECK(binder->panelTitle(QStringLiteral("dock.right")) == QStringLiteral("CAN 报文"));
        OK("模块侧闭环：按领域查清单 -> 按 id 请求面板 -> 标题/形态由知识包决定");
    }

    // --- 4) 停靠区就位（Qt-ADS）---
    CHECK(binder->panelCount() >= 1);
    CHECK(binder->panelWidget(QStringLiteral("dock.left")) != nullptr);
    CHECK(!binder->panelTitle(QStringLiteral("dock.left")).isEmpty());
    OK("停靠区就位：左栏有数据集面板且标题正确");

    // --- 5) 数据源面板：源列表 + 详情树（源 → 数据集 → 表 → 列）---
    //
    // 走**真实加载路径**（data.load 意图）——因为"数据集归属哪个源"只有走
    // 摄取管道才会产生：直接往仓库塞数据集是测不到源列表的。
    // 面板由器官 requestPanel 申请、由视图层自注册的工厂创建——
    // MainWindow 不认识它，这里从插槽区取回。
    auto *panel = qobject_cast<DataManagerPanel *>(
        binder->panelWidget(QStringLiteral("dock.left")));
    CHECK(panel != nullptr);

    const QString fixture =
        QStringLiteral(NG_TEST_FIXTURE_DIR "/demo_flight.csv");
    Intent load;
    load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = fixture;
    const auto loaded = application.modules().execute(load);
    CHECK(loaded);
    const int loadedDsId =
        loaded.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
    CHECK(loadedDsId >= 0);

    panel->refresh();
    // 源卡片：出现 1 个源，状态文案可见（不是空白按钮）。
    CHECK(panel->sourceNodeCount() == 1);
    CHECK(!panel->sourceStateText(0).isEmpty());
    // 字段视图（默认 chip）：数据集 -> 表 -> 列层级。
    CHECK(panel->shownSourceId() >= 0);
    CHECK(panel->activeChip() == 0);
    CHECK(panel->datasetNodeCount() == 1);
    CHECK(panel->tableNodeCount(0) >= 1); // 至少一张表
    OK("数据源面板：源卡片 + 字段视图（数据集 → 表 → 列）层级正确");

    // 字段/参数/消息 三 chip：对齐老 dock 与截图的领域信息分层。
    // 字段 chip 统计真实列数（demo_flight.csv 有多列），文案不是占位 0。
    CHECK(panel->chipText(0).startsWith(QStringLiteral("字段")));
    CHECK(panel->chipText(1).startsWith(QStringLiteral("参数")));
    CHECK(panel->chipText(2).startsWith(QStringLiteral("消息")));
    OK("数据源面板：字段/参数/消息 三 chip 就位，文案来自真实元数据");

    // --- 5b) 活动源 id 与数据集 id 是两套空间（曾混用导致重载失效）---
    //
    // data.reload 要按**源** id 找源；若视图把数据集 id 写进活动源，
    // handler 就永远找不到源。这里断言：反查能回到真正拥有该数据集的源。
    {
        const int sourceId = application.context().sources().sourceOfDataset(loadedDsId);
        CHECK(sourceId >= 0);
        const DataSourceInfo *info = application.context().sources().get(sourceId);
        CHECK(info != nullptr);
        CHECK(info->datasetIds.contains(loadedDsId));
    }
    OK("源与数据集 id 分离：sourceOfDataset 反查正确（重载/导出的前提）");

    // --- 5c) data.reload 走视图语义后真能找到源 ---
    //
    // 回归点：视图曾把**数据集 id** 写进活动源，而 handler 按**源 id** 查源，
    // 于是"重载"永远报"没有可重载的数据源"。这里显式给源 id 重载，
    // 并验证数据集 id 稳定（重载后绘图视图不因 id 漂移失联）。
    {
        const int sourceId = application.context().sources().sourceOfDataset(loadedDsId);
        CHECK(sourceId >= 0);
        Intent rel;
        rel.op = QStringLiteral("data.reload");
        rel.params[QStringLiteral("id")] = sourceId;
        const auto r = application.modules().execute(rel);
        CHECK(r);
        // 重载后数据集 id 不变（稳定 id 机制）。
        const int newDsId =
            r.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
        CHECK(newDsId == loadedDsId);
        // 源列表仍然只有一个源（重载是替换不是新增）。
        panel->refresh();
        CHECK(panel->sourceNodeCount() == 1);
        OK("data.reload：按源 id 重载成功，数据集 id 稳定（绘图不失效）");
    }

    // --- 5d) data.derive：Rust 实时派生 → 结果给 UI，原数据集零改动 ---
    //
    // 回归点：派生曾走 ps_dataset_apply_filter **写回**数据集（污染原数据、
    // 视图 schema 漂移）。现改为 ps_preview_filter 只读预览：payload 带 time/value，
    // 并发 derivedSeriesReady 给画布；数据集的表/列/行数前后必须一致。
    {
        const Dataset *before = application.data().get(loadedDsId);
        CHECK(before != nullptr && before->tableCount() >= 1);
        const Table *tbl = before->table(0);
        CHECK(tbl != nullptr && !tbl->columns.isEmpty());
        const int rowsBefore = tbl->rows;
        const int colsBefore = tbl->columns.size();
        const QString tableName = tbl->name;
        const QString columnName = tbl->columns.first().name;

        // 订阅派生结果事件。
        int derivedSignalCount = 0;
        QString derivedName;
        QObject::connect(&application.context(), &AppContext::derivedSeriesReady,
                         &application, [&](const QString &name, const QVector<double> &time,
                                           const QVector<double> &value) {
                             ++derivedSignalCount;
                             derivedName = name;
                             CHECK(time.size() == value.size());
                             CHECK(!time.isEmpty());
                         });

        Intent der;
        der.op = QStringLiteral("data.derive");
        der.params[QStringLiteral("dataset")] = loadedDsId;
        der.params[QStringLiteral("table")] = tableName;
        der.params[QStringLiteral("column")] = columnName;
        der.params[QStringLiteral("filter")] = QStringLiteral("absolute");
        const auto dr = application.modules().execute(der);
        CHECK(dr);
        const QVariantMap pm = dr.value().payload.toMap();
        const int count = pm.value(QStringLiteral("count")).toInt();
        CHECK(count > 0);
        const QVector<double> dtime = pm.value(QStringLiteral("time")).value<QVector<double>>();
        const QVector<double> dvalue = pm.value(QStringLiteral("value")).value<QVector<double>>();
        CHECK(dtime.size() == count);
        CHECK(dvalue.size() == count);
        CHECK(derivedSignalCount == 1);
        CHECK(!derivedName.isEmpty());

        // 关键断言：派生后数据集结构完全没变（没有新列、行数一致）。
        const Dataset *after = application.data().get(loadedDsId);
        CHECK(after != nullptr);
        CHECK(after->tableCount() == before->tableCount());
        const Table *tblAfter = after->table(tableName);
        CHECK(tblAfter != nullptr);
        CHECK(tblAfter->rows == rowsBefore);
        CHECK(tblAfter->columns.size() == colsBefore);
        OK("data.derive：Rust 实时派生结果给 UI（payload+信号），原数据集零改动");
    }

    // 绘图路径用**内存构造**的数据集：它只关心 LOD 有界性，
    // 与源列表无关；表/列名固定才好断言数值范围。
    const int dsId = buildDataset(application.data());
    CHECK(dsId >= 0);

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

    // --- 7.5) 拖拽画图：数据集树的列拖到画布 → 与双击同一路径重新渲染 ---
    {
        // 先切到 sig 之外的通道，确保拖拽确实改变了活动通道（不是早退的假通过）。
        application.context().setActiveChannel(dsId, QStringLiteral("trace"),
                                               QStringLiteral("sig"));
        CHECK(application.context().activeColumn() == QStringLiteral("sig"));

        // 构造通道 MIME（与 DataManagerPanel 发起端完全同一编码）。
        QMimeData mime;
        mime.setData(QString::fromLatin1(channel_mime::type()),
                     channel_mime::encode(dsId, QStringLiteral("trace"),
                                          QStringLiteral("alt"))
                         .toUtf8());
        // 投递拖拽负载：画布解码 → channelDropped → setActiveChannel → 视图重绘。
        // 直接调 dropChannel（dropEvent 的核心逻辑），避开 Qt 在 headless 下
        // 对 sendEvent(QDropEvent) 的分发限制——真实拖拽由 QDrag 系统投递同一函数。
        CHECK(canvas->dropChannel(&mime));                     // 合法 MIME 被接受
        CHECK(application.context().activeColumn() == QStringLiteral("alt")); // 活动通道被切换
        CHECK(canvas->lodPointCount() > 0);                    // 新通道已渲染
        OK("拖拽画图：列节点 MIME → 活动通道切换 + 重新渲染（与双击同路径）");

        // 非法 MIME（非通道格式）应被拒绝，且不影响当前通道。
        QMimeData garbage;
        garbage.setText(QStringLiteral("not a channel"));
        CHECK(!canvas->dropChannel(&garbage));
        CHECK(application.context().activeColumn() == QStringLiteral("alt")); // 未被改动
        OK("拖拽画图：非法 MIME 被拒，画布状态不受影响");
    }

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

    // --- 9) 主题管理：双主题 token + 意图切换 + 全局 QSS + ribbon 联动 ---
    // 反例锚点（根 AGENTS §12）：若颜色散落成十六进制字面量，切主题必残留旧色。
    // 这里验证颜色全部来自 token 表，且切换走与按钮/脚本相同的 theme.* 意图。
    {
        using Theme = ng::gui::ThemeManager;
        Theme *tm = window.themeManager();
        CHECK(tm != nullptr);
        CHECK(tm->current() == Theme::Theme::Light); // 缺省浅色

        // 同一 token 两套主题必须给出不同颜色（色表真的分了深浅）。
        CHECK(Theme::color(Theme::Theme::Light, QStringLiteral("text")) !=
              Theme::color(Theme::Theme::Dark, QStringLiteral("text")));
        CHECK(Theme::color(Theme::Theme::Light, QStringLiteral("window")) !=
              Theme::color(Theme::Theme::Dark, QStringLiteral("window")));
        // §12.1 契约 token 齐全；未知 token 给洋红告警色。
        CHECK(Theme::color(Theme::Theme::Light, QStringLiteral("textDim")).isValid());
        CHECK(Theme::color(Theme::Theme::Light, QStringLiteral("textMuted")).isValid());
        CHECK(Theme::color(Theme::Theme::Light, QStringLiteral("no_such_token")) ==
              QColor(QStringLiteral("#FF00FF")));
        // 数据语义色：不同系列index、不同主题都可读且不越界。
        CHECK(Theme::seriesColor(0, Theme::Theme::Dark) !=
              Theme::seriesColor(1, Theme::Theme::Dark));
        CHECK(Theme::seriesColor(0, Theme::Theme::Light).isValid());

        // mapStyleSheet：内联片段的 $(token) 必须被替换成实色，不留占位符。
        const QString mapped = tm->mapStyleSheet(QStringLiteral("color: $(text);"));
        CHECK(!mapped.contains(QStringLiteral("$(")));
        CHECK(mapped.contains(QStringLiteral("#")));

        // themeChanged 信号确实发出（ribbon 联动挂在它上面）。
        int changedCount = 0;
        QObject::connect(tm, &Theme::themeChanged, &window,
                         [&changedCount](Theme::Theme) { ++changedCount; });

        // 走意图切换：与按钮点击、脚本调用同一条路径。
        Intent dark;
        dark.op = QStringLiteral("theme.set");
        dark.params[QStringLiteral("theme")] = QStringLiteral("dark");
        const auto r = application.modules().execute(dark);
        CHECK(r); // 系统级 handler 认领（headless 才会报无处理器）
        CHECK(r.value().payload.toString() == QStringLiteral("dark"));
        CHECK(tm->current() == Theme::Theme::Dark);
        CHECK(changedCount == 1);
        // 全局 QSS 真的应用到 QApplication。
        CHECK(!qApp->styleSheet().isEmpty());
        CHECK(qApp->styleSheet().contains(Theme::color(
                  Theme::Theme::Dark, QStringLiteral("text")).name()));

        // theme.toggle 意图：切回浅色。
        Intent toggle;
        toggle.op = QStringLiteral("theme.toggle");
        const auto r2 = application.modules().execute(toggle);
        CHECK(r2);
        CHECK(tm->current() == Theme::Theme::Light);
        CHECK(changedCount == 2);
        OK("主题管理：双主题 token 分色 + theme.set/toggle 意图切换 + 全局 QSS + themeChanged 联动");
    }

    fprintf(stdout, "\nGUI 契约测试全部通过\n");
    return 0;
}

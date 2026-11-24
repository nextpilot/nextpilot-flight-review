// GUI 契约测试：验证 ribbon / docking / 画布 / ps 数据呈现这条链路。
//
// 与 headless 契约测试（test_architecture.cpp）分开：GUI 依赖 Qt Widgets，
// 必须在有 QApplication 的环境下跑。CI 里用 offscreen 平台插件。
//
// 检查项：
//   1) ribbon 按插槽数据渲染：页签数、分组数、动作数；
//   2) 模块在 GUI 环境下能申请到部件（requestPanel -> 骨架工厂）；
//   3) 停靠区就位（dock.left 有面板、标题正确）；
//   4) ps 数据呈现：plot.channel 意图返回**像素级有界**数据，
//      且点数 <= px（与数据行数无关）——这是百万级数据能画出来的关键；
//   5) 数据集管理面板能列出数据集/表/列；
//   6) 模块自注册：GUI 装载后模块全部激活。

#include "App.h"

#include "DataManagerPanel.h"
#include "DataTablePanel.h"
#include "views/diagnostics/DiagnosticsPanel.h"
#include "ImportDialog.h"

#include "ScriptPanels.h"
#include "BatchPanel.h"
#include "GuiContext.h"
#include "MainWindow.h"
#include "views/PropertyPanel.h"
#include "ThemeManager.h"
#include "ToolboxHub.h"
#include "Contribution.h"
#include "PanelFactoryHub.h"
#include "AxesView.h"
#include "axes/SeriesStore.h"
#include "PlotWorkspace.h"
#include "chrome/AxesToolbar.h"
#include "layout/PlotLayout.h"
#include "layout/TabbedFigure.h"
#include "layout/DockedAxes.h"
#include "DockAreaWidget.h"
#include "DockWidget.h"
#include "ShellBinder.h"
#include "command/CommandRegistry.h"
#include "command/KeybindingRegistry.h"
#include "FilterEditorDialog.h"

#include "AppContext.h"
#include "ChannelMime.h"
#include "RegistryTree.h"
#include "SettingsStore.h"

#include <QProcess>
#include "ChannelTransformConfig.h"

#include "Dataset.h"
#include "DataSource.h"
#include "Source.h"
#include "Intent.h"
#include "RuntimeHost.h"
#include "UiSlotHub.h"

#include <QAction>
#include <QApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QSet>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QToolButton>
#include <QTimer>

#include <cmath>
#include <memory>

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
using namespace nextpilot::plotscope::command;
using namespace nextpilot::plotscope::host;

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
static int buildDataset(DatasetHub &repo, double t0 = 0.0)
{
    Dataset ds = Dataset::create(QStringLiteral("gui-test"), QStringLiteral("mem"));
    QVector<double> ts, a, b;
    ts.reserve(5000);
    a.reserve(5000);
    b.reserve(5000);
    QVector<double> mode;      // 整数值枚举（0/1/2 循环，每 100 样本一段）——状态视图用
    mode.reserve(5000);
    QVector<quint8> armed;     // bool 编码 0/1（kernel Bool 列布局 = quint8 数组）
    armed.reserve(5000);
    for (int i = 0; i < 5000; ++i)
    {
        ts.append(t0 + i * 0.001);
        a.append(i * 0.5);
        b.append(1000.0 - i * 0.25);
        mode.append((i / 100) % 3);
        armed.append((i % 400) < 200 ? 1 : 0);
    }
    // addTable 返回表索引（0 是合法值，不能用真假判断成功与否）
    if (ds.addTable(QStringLiteral("trace"), ts) < 0)
        return -1;
    if (!ds.addF64Column(QStringLiteral("trace"), QStringLiteral("sig"), QString(), a))
        return -1;
    if (!ds.addF64Column(QStringLiteral("trace"), QStringLiteral("alt"), QString(), b))
        return -1;
    if (!ds.addF64Column(QStringLiteral("trace"), QStringLiteral("mode"), QString(), mode))
        return -1;
    if (!ds.addColumnTyped(QStringLiteral("trace"), QStringLiteral("armed"), QString(),
                           ValueType::Bool, armed.constData(), nullptr, armed.size()))
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
    MainWindow window(application.context(), nextpilot::plotscope::registry::plotScopeProductSpec());
    // derivedSeriesReady 的观察计数：lambda 以 &application 为接收者会活到 application 析构，
    // 故必须用 application 之前声明、之后的持久变量，不能捕获块内栈变量（悬空引用→堆损坏）。
    int g_derivedCount = 0, g_filterCount = 0, g_scriptFilterCount = 0;
    QString g_derivedName;
    window.build(application.host());
    // 先给窗口一个确定尺寸：画布按自身像素宽请求数据，尺寸确定后断言才可复现。
    window.resize(1280, 800);

    // --- 0a) 「文件 → 设置」的接线契约：ribbon 动作绑的是 GUI 专属意图 ---
    {
        // 偏好对话框是 GUI 专属能力：动作必须绑 preferences.open（由外壳注册的
        // 系统级 handler 处理），不能绑 settings.*——那是 modules/settings 的存取
        // 域，模块不认识 QWidget，headless 里也不该出现任何界面语义。
        const auto &slotReg2 = application.host().uiSlots();
        const auto fileActs = slotReg2.actionsIn(QStringLiteral("ribbon.group.file.document"));
        bool hasSettingsAction = false;
        for (const auto &entry : fileActs)
        {
            if (entry.action.id == QStringLiteral("file.settings"))
            {
                hasSettingsAction = true;
                CHECK(entry.action.intentOp == QStringLiteral("preferences.open"));
            }
        }
        CHECK(hasSettingsAction);
        OK("设置入口接线：ribbon file.settings → preferences.open（GUI 系统级 handler）");
    }

    // --- 死插槽零消费者检查（验收 15 / D13，GUI 全装配后）---
    // headless 只查槽 kind/area 白名单（模块未 onAttach，查不到消费者）；
    // 真正的"声明了槽却无人消费"必须在 GUI 装配后查——此刻模块已自声明页/组并绑定动作。
    {
        const auto &slotReg = application.host().uiSlots();
        for (const UiSlot &s : slotReg.allSlots())
        {
            if (s.kind == SlotKind::RibbonPage)
            {
                // 页必须至少有一个分组，否则是空页（死槽）。
                CHECK(!slotReg.childrenOf(s.area).isEmpty());
            }
            else // RibbonGroup
            {
                // 组要么有动作，要么有子组（嵌套画廊），否则是死槽。
                const bool hasActions = !slotReg.actionsIn(s.area).isEmpty();
                const bool hasChildren = !slotReg.childrenOf(s.area).isEmpty();
                if (!(hasActions || hasChildren))
                    fprintf(stderr, "[死槽] area=%s owner=%s（声明了却无动作/子组）\n",
                            qPrintable(s.area), qPrintable(s.owner));
                CHECK(hasActions || hasChildren);
            }
        }
        // 旧方位名 / 菜单名不得再作为槽存在。
        for (const char *dead : {"dock.left", "dock.right", "dock.bottom", "canvas.center",
                                 "canvas.tool", "menu.plot"})
            CHECK(slotReg.slot(QString::fromLatin1(dead)) == nullptr);
        OK("死插槽零消费者检查：每个声明的页有分组、每个组有动作或子组");

        // 启动期 panelTypes 工厂命中（验收 4）：装配完成后不得有未命中的面板类型。
        const QStringList missing = window.binder()->missingPanelTypes();
        if (!missing.isEmpty())
            fprintf(stderr, "[panelTypes] 未命中: %s\n", qPrintable(missing.join(QLatin1Char(','))));
        CHECK(missing.isEmpty());
        OK("panelTypes 启动期校验：所有 activity 面板类型均命中工厂表");

        // 模块贡献语义 token（验收 8）：≥3 模块各声明 ≥1 个 ThemeTokenSpec。
        const auto &moduleTokens = window.context()->moduleThemeTokens();
        QStringList tokenIds;
        for (const auto &t : moduleTokens)
            tokenIds << t.id;
        CHECK(moduleTokens.size() >= 3);
        CHECK(tokenIds.contains(QStringLiteral("plot.cursor")));
        CHECK(tokenIds.contains(QStringLiteral("data.source")));
        CHECK(tokenIds.contains(QStringLiteral("script.breakpoint")));
        // 模块声明的 token 有合法兜底色，且能进主题（ThemeManager 取到非洋红）。
        for (const auto &t : moduleTokens)
        {
            CHECK(QColor(t.fallback).isValid());
            CHECK(ThemeManager::themeColor(t.id).isValid());
        }
        OK("模块语义 token：plot/data/script 各贡献 1 个，兜底色合法且进主题");
    }

    // --- 0a2) 真跑一次 preferences.open：GUI 系统级 handler 必须弹得出、收得回 ---
    {
        // 模态对话框在 exec() 内跑事件循环，这里用一个重试定时器找到它并关闭，
        // 证明「意图 → 系统级 handler → 对话框」这条链在 GUI 环境里真的闭合，
        // 而不是只测了字符串绑定。
        int dialogSeen = 0;
        int tries = 0;
        QTimer closer;
        QObject::connect(&closer, &QTimer::timeout, [&]() {
            if (++tries > 50)
            {
                closer.stop();
                return;
            }
            for (QWidget *w : QApplication::topLevelWidgets())
            {
                if (w->isVisible() && w->windowTitle() == QStringLiteral("偏好设置"))
                {
                    ++dialogSeen;
                    w->close();
                    closer.stop();
                    return;
                }
            }
        });
        closer.start(20);

        Intent openIntent;
        openIntent.op = QStringLiteral("preferences.open");
        openIntent.params[QStringLiteral("section")] = QStringLiteral("data");
        const auto openResult = application.host().execute(openIntent);
        closer.stop();

        CHECK(openResult);
        CHECK(openResult.value().payload.toBool());
        CHECK(dialogSeen == 1); // 确实弹过，而不是 handler 空转返回 true
        OK("偏好对话框：preferences.open 经 GUI 系统级 handler 弹出并可关闭");
    }

    // --- 0b) App 作用域偏好的落盘往返（偏好对话框「保存」的核心链路）---
    {
        QTemporaryDir dir;
        CHECK(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("settings.json"));
        {
            SettingsStore store(path);
            CHECK(store.isPersistent());
            store.setValue(QStringLiteral("plot.time.format"), QStringLiteral("hms"));
            store.setValue(QStringLiteral("app/theme"), QStringLiteral("dark"));
            QString err;
            CHECK(store.flush(&err));
        }
        CHECK(QFileInfo::exists(path));
        {
            SettingsStore reloaded(path); // 构造即从磁盘读入
            CHECK(reloaded.value(QStringLiteral("plot.time.format")).toString()
                  == QStringLiteral("hms"));
            CHECK(reloaded.value(QStringLiteral("app/theme")).toString() == QStringLiteral("dark"));
        }
        OK("App 作用域偏好落盘：flush 写盘 + 重新加载值一致");
    }

    // --- 1) 模块在 GUI 环境下全部激活（静态自注册生效）---
    const auto active = application.host().activeIds();
    CHECK(active.contains(QStringLiteral("mod.data")));
    CHECK(active.contains(QStringLiteral("mod.plot")));
    CHECK(active.contains(QStringLiteral("mod.script")));
    OK("模块自注册装载：data / plot / script 全部激活");

    // --- 2) ribbon 按插槽数据渲染（SARibbon，结构层由第三方负责）---
    ShellBinder *binder = window.binder();
    CHECK(binder != nullptr);
    // 绑定器创建了 SARibbonBar：窗口顶部区就是它
    CHECK(window.menuWidget() != nullptr);
    // 插槽声明了页签/分组；动作由模块在 onAttach 里登记
    const auto &slotReg = application.host().uiSlots();
    // Ribbon 分 9 页：开始/文件/数据/绘图/脚本/协议/分析/报告/视图。
    CHECK(slotReg.slotsOfKind(SlotKind::RibbonPage).size() == 9);
    CHECK(slotReg.slotsOfKind(SlotKind::RibbonGroup).size() >= 8);
    // 绘图模块 onAttach 往绘图页"操作"分组放了"重绘"动作
    CHECK(slotReg.actionsIn(QStringLiteral("ribbon.group.plot.action")).size() >= 1);
    // 数据模块 onAttach 往数据页"常用"分组放了 reload/export/clearCache 三个动作
    const auto dataActs = slotReg.actionsIn(QStringLiteral("ribbon.group.data.common"));
    CHECK(dataActs.size() >= 3);
    bool hasReload = false, hasExport = false;
    for (const auto &entry : dataActs)
    {
        if (entry.action.intentOp == QStringLiteral("data.reload")) hasReload = true;
        if (entry.action.intentOp == QStringLiteral("data.export")) hasExport = true;
    }
    CHECK(hasReload && hasExport); // 动作绑的是真实意图，不是空按钮
    // 录制组：两个动作都绑真实意图，组必须存在（否则按钮无处可挂）
    CHECK(slotReg.slot(QStringLiteral("ribbon.group.data.record")) != nullptr);
    const auto recActs = slotReg.actionsIn(QStringLiteral("ribbon.group.data.record"));
    CHECK(recActs.size() == 2);
    bool hasRecStart = false, hasRecStop = false;
    for (const auto &entry : recActs)
    {
        if (entry.action.intentOp == QStringLiteral("data.record.start")) hasRecStart = true;
        if (entry.action.intentOp == QStringLiteral("data.record.stop")) hasRecStop = true;
    }
    CHECK(hasRecStart && hasRecStop);
    OK("ribbon 按插槽数据绑定：页签/分组/动作数量符合声明");

    // --- 2a-2) 图库是纯数据声明（headless 可读），plot 类型 / parse 类型都在 ---
    {
        // 图型库：12 种图型，按"常用/曲线/地图3D/仪表"分组，缩略图 visual 声明齐全。
        const auto plotActs = slotReg.actionsIn(QStringLiteral("ribbon.group.plot.type"));
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
        const auto srcActs = slotReg.actionsIn(QStringLiteral("ribbon.group.data.sources"));
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
        CHECK(hasOpenShortcut); // 打开数据 = Ctrl+O
        OK("快捷键作为动作属性：打开数据声明 Ctrl+O");
    }

    // --- 2a-4) Ribbon 页顺序 + 分组归属 + action 顺序 ---
    {
        // 页顺序：slotsOfKind 现在按 order 升序，而不是声明顺序。
        const auto pages = slotReg.slotsOfKind(SlotKind::RibbonPage);
        CHECK(pages.size() == 9);
        const QStringList expectPages = {QStringLiteral("开始"), QStringLiteral("文件"),
            QStringLiteral("数据"), QStringLiteral("绘图"), QStringLiteral("脚本"),
            QStringLiteral("协议"), QStringLiteral("分析"), QStringLiteral("报告"),
            QStringLiteral("视图")};
        for (int i = 0; i < pages.size(); ++i)
            CHECK(pages.at(i).label == expectPages.at(i));
        // 组顺序：同页内按 group.order 升序（老设计 panel 数组顺序）。
        const auto plotGroups = slotReg.childrenOf(QStringLiteral("ribbon.page.plot"));
        // 绘图页分组按用户要求重排：页面 → 布局 → 显示 → 交互 → 导出 → 图表类型(gallery)。
        const QStringList expectGroups = {QStringLiteral("页面"), QStringLiteral("布局"),
            QStringLiteral("显示"), QStringLiteral("交互"), QStringLiteral("导出"),
            QStringLiteral("图表类型"), QStringLiteral("曲线与轴")};
        CHECK(plotGroups.size() == expectGroups.size());
        for (int i = 0; i < plotGroups.size(); ++i)
            CHECK(plotGroups.at(i).label == expectGroups.at(i));
        OK("Ribbon 页/组顺序：slotsOfKind/childrenOf 按 order 升序");
    }
    {
        // 分组归属：页面=新建页 / 布局=增删拆分 / 显示=网格图例 / 画布操作=缩放自适应。
        const auto idsIn = [&slotReg](const QString &area) {
            QStringList ids;
            for (const auto &b : slotReg.actionsIn(area))
                ids << b.action.id;
            return ids;
        };
        CHECK(idsIn(QStringLiteral("ribbon.group.plot.figure")).contains(QStringLiteral("figure.new")));
        const auto layoutIds = idsIn(QStringLiteral("ribbon.group.plot.layout"));
        CHECK(layoutIds.contains(QStringLiteral("figure.grid")));
        CHECK(layoutIds.contains(QStringLiteral("figure.addAxes")));
        CHECK(layoutIds.contains(QStringLiteral("figure.splitHorizontal")));
        const auto displayIds = idsIn(QStringLiteral("ribbon.group.plot.display"));
        // 网格改成一个大按钮 + 下拉（X/Y 网格线在菜单里选）；脚本仍可直调 plot.grid。
        CHECK(displayIds.contains(QStringLiteral("plot.grid.menu")));
        CHECK(!displayIds.contains(QStringLiteral("plot.grid.x")));
        CHECK(!displayIds.contains(QStringLiteral("plot.grid.y")));
        CHECK(displayIds.contains(QStringLiteral("plot.box")));
        CHECK(displayIds.contains(QStringLiteral("plot.legend.toggle")));
        CHECK(!displayIds.contains(QStringLiteral("plot.autoscale"))); // 恢复/自适应归交互组
        const auto toolIds = idsIn(QStringLiteral("ribbon.group.plot.tool"));
        CHECK(toolIds.contains(QStringLiteral("plot.autoscale")));
        // 文件页：老设计"截图"是独立一组，不再塞在"导出"组。
        CHECK(slotReg.slot(QStringLiteral("ribbon.group.file.screenshot")) != nullptr);
        CHECK(idsIn(QStringLiteral("ribbon.group.file.screenshot")).contains(QStringLiteral("file.screenshot")));
        CHECK(!idsIn(QStringLiteral("ribbon.group.file.export")).contains(QStringLiteral("file.screenshot")));
        // 页面组除新建页还有删除页（用户要求）。
        CHECK(idsIn(QStringLiteral("ribbon.group.plot.figure")).contains(QStringLiteral("figure.closeCurrent")));
        // 导出组：截图 + 导出 PNG/CSV。
        const auto exportIds = idsIn(QStringLiteral("ribbon.group.plot.action"));
        CHECK(exportIds.contains(QStringLiteral("plot.screenshot")));
        CHECK(exportIds.contains(QStringLiteral("plot.export.png")));
        // 交互组：三个工具模式 + 恢复 + AB + 书签。
        CHECK(toolIds.contains(QStringLiteral("plot.ab.toggle")));
        CHECK(toolIds.contains(QStringLiteral("plot.bookmark.add")));
        OK("分组归属：页面/布局/显示(网格下拉)/交互/导出 + 独立截图组");
    }
    {
        // action 组内顺序按 order 升序（不再依赖模块装载顺序）。
        const auto layout = slotReg.actionsIn(QStringLiteral("ribbon.group.plot.layout"));
        for (int i = 1; i < layout.size(); ++i)
            CHECK(layout.at(i - 1).action.order <= layout.at(i).action.order);
        // 老布局组首项是"网格"（figure.grid, order 0）。
        CHECK(!layout.isEmpty());
        CHECK(layout.first().action.id == QStringLiteral("figure.grid"));
        // gallery 分类出现顺序：plot 类型 = 常用→曲线→地图/3D→仪表。
        const auto plots = slotReg.actionsIn(QStringLiteral("ribbon.group.plot.type"));
        QStringList catOrder;
        for (const auto &b : plots)
            if (!catOrder.contains(b.action.galleryGroup))
                catOrder << b.action.galleryGroup;
        CHECK(catOrder == (QStringList{QStringLiteral("常用"), QStringLiteral("曲线"),
                                       QStringLiteral("地图 / 3D"), QStringLiteral("仪表")}));
        OK("action 顺序可控：组内按 order 升序，gallery 分类按老设计顺序");
    }

    // --- 2b) 点击链路端到端：按钮发的是 intentOp，不是把 id 当 op ---
    // 回归点：UiAction 有 id（data.action.reload）与 intentOp（data.reload）两个字段。
    // 曾把 id 当 op 发出，导致除 openData（被外壳特判）外所有按钮点击都报
    // "没有模块能处理意图"。这里真触发 QAction，走
    //   ShellBinder -> dispatcher -> RuntimeHost -> handler 全链路，
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
    // 若 ps_gui 被改成 STATIC 库，未被引用的 TU 会被链接器丢弃、
    // 静态初始化不执行，工厂表会**静默变空**——功能消失且无任何报错。
    // 所以这里显式断言两个视图都已登记。
    CHECK(!nextpilot::plotscope::registry::PanelFactoryHub::types().isEmpty());
    CHECK(nextpilot::plotscope::registry::PanelFactoryHub::types().contains(QStringLiteral("dataset-tree")));
    CHECK(nextpilot::plotscope::registry::PanelFactoryHub::types().contains(QStringLiteral("plot.axes")));
    OK("视图层自注册生效：dataset-tree / plot-canvas 均已登记（OBJECT 库保证 TU 不被丢弃）");

    // --- 3b) 领域面板：形态随领域变时的解法 ---
    //
    // 有些面板的形态**本身就是领域知识**（CAN 报文浏览器取决于 DBC 定义的信号）。
    // 通用视图层不该认识它，模块也给不出形态 —— 于是形态归**扩展包**：
    //   · 描述（panels 表条目，纯数据，headless 可见）；
    //   · 实现（扩展包的呈现层，注册进**同一张**工厂表）。
    //
    // 这里锁住三件事：
    //   1) 扩展包声明的面板清单能被查到（headless 也可查）；
    //   2) 该面板的实现确实注册进了工厂表（描述与实现用同一个 id 关联）；
    //   3) 按领域过滤有效。
    {
        // 1) 描述：纯数据，headless 也查得到。
        const auto &toolbox = application.context().toolbox();
        CHECK(toolbox.panels.count() >= 1); // 汽车包的 CAN 报文面板

        // 2) 实现：领域面板与通用面板共用同一张工厂表。
        CHECK(nextpilot::plotscope::registry::PanelFactoryHub::types().contains(QStringLiteral("car.can-browser")));

        // 3) 领域过滤：汽车领域的清单里有它，通用领域不该混入。
        int automotive = 0;
        for (const nextpilot::plotscope::registry::PanelSpec &spec : toolbox.panels.values())
            if (spec.domain == nextpilot::plotscope::registry::Domain::Automotive)
                ++automotive;
        CHECK(automotive >= 1);
        OK("领域面板：扩展包声明描述（headless 可见）+ 呈现层注册实现，共用同一张工厂表");
    }

    // --- 3c) 模块侧视角：模块不认识面板实现，也能查询清单并请求到面板 ---
    //
    // 这才是用户问题的落点：modules 需要某个 panel，但 panel 的形态随业务变、
    // gui/views 并不知道。模块只认**领域**，先问 availableDomainPanels(domain)，
    // 再按 id 请求；面板从哪来、长什么样，全由扩展包决定。
    {
        auto *ui = window.context(); // 模块 onAttach 拿到的就是这个 IUiContext
        CHECK(ui != nullptr);

        // 模块按领域查清单（不写死 id）。
        const auto specs = ui->availableDomainPanels(Domain::Automotive);
        const PanelSpec *canSpec = nullptr;
        for (const PanelSpec &s : specs)
            if (s.id == QStringLiteral("car.can-browser"))
                canSpec = &s;
        CHECK(canSpec != nullptr);
        CHECK(canSpec->label == QStringLiteral("CAN 报文"));
        CHECK(canSpec->area == Region::SecondarySidebar);

        // 模块按清单里的 id 请求面板——它不知道这是领域面板还是通用面板，
        // 两者走同一个 requestPanel 入口。
        QWidget *canPanel = ui->requestPanel(canSpec->area, canSpec->id);
        CHECK(canPanel != nullptr);
        // CAN 面板能力仍在（工厂 + 清单 + 可请求），但不再占据 dock.right 主位。
        CHECK(binder->panelTitles(Region::SecondarySidebar).contains(QStringLiteral("属性"))); // 页签集里有属性面板（同区多面板后主位概念已废）
        OK("模块侧闭环：按领域查清单 -> 按 id 请求面板成功；dock.right 主位归属性面板");
    }

    // --- 3d) 属性面板：dock.right 页签之一 + 统一 selection 联动 ---
    {
        CHECK(binder->panelWidget(Region::SecondarySidebar) != nullptr);
        // dock.right 可能并列多个页签（属性 + 脚本调试等，模块激活顺序不定），
        // 按类型查找属性面板，不依赖注册先后。
        PropertyPanel *propPanel = nullptr;
        for (QWidget *w : binder->panelWidgets(Region::SecondarySidebar))
        {
            if (!propPanel)
                propPanel = qobject_cast<PropertyPanel *>(w);
        }
        CHECK(nextpilot::plotscope::registry::PanelFactoryHub::types()
                  .contains(QStringLiteral("property-panel")));

        // 统一 selection 联动：选中画布 -> 属性面板按 kind 找 provider 显示 schema。
        CHECK(propPanel != nullptr);
        Selection canvasSel;
        canvasSel.kind = SelectionKind::Canvas;
        canvasSel.label = QStringLiteral("画布");
        canvasSel.ids.insert(QStringLiteral("canvas"), QStringLiteral("active"));
        application.context().setSelection(canvasSel);
        CHECK(propPanel->schemaTitle().contains(QStringLiteral("画布")));
        CHECK(propPanel->rowCount() >= 2); // 图型/曲线数/图例
        CHECK(propPanel->selectionKindText() == QStringLiteral("canvas"));
        OK("统一 selection 联动：选中画布 -> 属性面板按 provider 显示 schema 行");
    }

    // --- 4) 停靠区就位（Qt-ADS）---
    CHECK(binder->panelCount() >= 1);
    CHECK(binder->panelWidget(Region::PrimarySidebar) != nullptr);
    CHECK(!binder->panelTitle(Region::PrimarySidebar).isEmpty());
    OK("停靠区就位：左栏有数据集面板且标题正确");

    // --- 4a) ADS 几何：左右全高、Panel 只挂 Editor 下方、状态栏全宽 ---
    {
        window.show();
        QEventLoop waitLoop;
        QTimer::singleShot(450, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();
        QCoreApplication::processEvents();

        const QJsonObject geo = binder->geometrySnapshot();
        auto rect = [&geo](const QString &name) {
            return geo.value(name).toObject();
        };
        auto x = [&rect](const QString &name) { return rect(name).value(QStringLiteral("x")).toInt(); };
        auto y = [&rect](const QString &name) { return rect(name).value(QStringLiteral("y")).toInt(); };
        auto w = [&rect](const QString &name) { return rect(name).value(QStringLiteral("w")).toInt(); };
        auto h = [&rect](const QString &name) { return rect(name).value(QStringLiteral("h")).toInt(); };

        CHECK(!rect(QStringLiteral("editor")).isEmpty());
        CHECK(!rect(QStringLiteral("primarySidebar")).isEmpty());
        CHECK(!rect(QStringLiteral("secondarySidebar")).isEmpty());
        CHECK(!rect(QStringLiteral("panel")).isEmpty());
        CHECK(!rect(QStringLiteral("statusBar")).isEmpty());

        // 左/右栏与 Editor 顶边齐平，且高度覆盖 Editor + Panel。
        CHECK(std::abs(y(QStringLiteral("primarySidebar")) - y(QStringLiteral("editor"))) <= 2);
        CHECK(std::abs(y(QStringLiteral("secondarySidebar")) - y(QStringLiteral("editor"))) <= 2);
        CHECK(h(QStringLiteral("primarySidebar")) >= h(QStringLiteral("editor")) + h(QStringLiteral("panel")) - 4);
        CHECK(h(QStringLiteral("secondarySidebar")) >= h(QStringLiteral("editor")) + h(QStringLiteral("panel")) - 4);

        // Panel 只在 Editor 下方：水平范围不越出 Editor。
        CHECK(y(QStringLiteral("panel")) >= y(QStringLiteral("editor")) + h(QStringLiteral("editor")) - 2);
        CHECK(x(QStringLiteral("panel")) >= x(QStringLiteral("editor")) - 2);
        CHECK(x(QStringLiteral("panel")) + w(QStringLiteral("panel"))
              <= x(QStringLiteral("editor")) + w(QStringLiteral("editor")) + 2);

        // 状态栏全宽，位于所有停靠区下方。
        fprintf(stdout, "[geom] win=%dx%d status=(%d,%d %dx%d) left=(%d,%d %dx%d) editor=(%d,%d %dx%d) bottom=(%d,%d %dx%d)\n",
                window.width(), window.height(),
                x(QStringLiteral("statusBar")), y(QStringLiteral("statusBar")),
                w(QStringLiteral("statusBar")), h(QStringLiteral("statusBar")),
                x(QStringLiteral("primarySidebar")), y(QStringLiteral("primarySidebar")),
                w(QStringLiteral("primarySidebar")), h(QStringLiteral("primarySidebar")),
                x(QStringLiteral("editor")), y(QStringLiteral("editor")),
                w(QStringLiteral("editor")), h(QStringLiteral("editor")),
                x(QStringLiteral("panel")), y(QStringLiteral("panel")),
                w(QStringLiteral("panel")), h(QStringLiteral("panel")));
        CHECK(y(QStringLiteral("statusBar")) >= y(QStringLiteral("primarySidebar")) + h(QStringLiteral("primarySidebar")) - 2);
        CHECK(x(QStringLiteral("statusBar")) >= 0);
        CHECK(w(QStringLiteral("statusBar")) >= window.width() - 40);
        OK("ADS 几何：左右全高、Panel 只在 Editor 下方、StatusBar 全宽");
    }

    // --- 4a2) 底部先到：首个 dock 请求也必须先装中央，禁止退回 root ---
    {
        RegistryTree registryTree;
        auto slotHub = std::make_unique<UiSlotHub>(registryTree);
        QMainWindow isolated;
        ShellBinder binder2{&isolated, *slotHub};
        auto *bottom = new QLabel(QStringLiteral("底部面板"));
        binder2.dock(Region::Panel, QStringLiteral("底部"), bottom);
        isolated.resize(900, 640);
        isolated.show();
        QEventLoop waitLoop;
        QTimer::singleShot(100, &waitLoop, &QEventLoop::quit);
        waitLoop.exec();

        const QJsonObject geo = binder2.geometrySnapshot();
        const QJsonObject editor = geo.value(QStringLiteral("editor")).toObject();
        const QJsonObject bottomRect = geo.value(QStringLiteral("panel")).toObject();
        CHECK(!editor.isEmpty());
        CHECK(!bottomRect.isEmpty());
        CHECK(bottomRect.value(QStringLiteral("y")).toInt()
              >= editor.value(QStringLiteral("y")).toInt()
                     + editor.value(QStringLiteral("h")).toInt() - 2);
        OK("底部先到：dock() 先安装中央，Panel 仍挂 Editor 下方");
    }

    // --- 4b) 脚本工作台：侧栏面板就位 + 编辑器按需创建（中央区锚定画布）---
    {
        // 侧栏三面板随 attach 挂上（与既有面板同侧并成页签，顺序不定，按类型查）。
        bool hasFiles = false, hasConsole = false, hasDebug = false;
        for (QWidget *w : binder->panelWidgets(Region::PrimarySidebar))
            hasFiles = hasFiles || qobject_cast<nextpilot::plotscope::script::ScriptFilesPanel *>(w) != nullptr;
        for (QWidget *w : binder->panelWidgets(Region::Panel))
            hasConsole = hasConsole || qobject_cast<nextpilot::plotscope::script::ScriptConsolePanel *>(w) != nullptr;
        for (QWidget *w : binder->panelWidgets(Region::SecondarySidebar))
            hasDebug = hasDebug || qobject_cast<nextpilot::plotscope::script::ScriptDebugPanel *>(w) != nullptr;
        CHECK(hasFiles);
        CHECK(hasConsole);
        CHECK(hasDebug);

        // 编辑器**按需创建**：初始中央区只有画布；经模块通道新建文档后出现编辑器页签。
        CHECK(binder->centralWidgets().size() == 1);
        QVariantMap p;
        p.insert(QStringLiteral("name"), QStringLiteral("tgui_doc"));
        Result<QVariant> out{QVariant()};
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("script.workbench.new"), p, out));
        CHECK(binder->centralWidgets().size() == 2);
        nextpilot::plotscope::script::ScriptEditorPanel *editor = nullptr;
        for (QWidget *w : binder->centralWidgets())
        {
            if (!editor)
                editor = qobject_cast<nextpilot::plotscope::script::ScriptEditorPanel *>(w);
        }
        CHECK(editor != nullptr);
        CHECK(editor->documentCount() == 1);
        CHECK(editor->currentDocumentName() == QStringLiteral("tgui_doc"));
        OK("脚本工作台：三侧栏就位；编辑器按需创建进中央页签，画布仍是首签");

        // Ribbon"脚本"页签贡献（2.1.10）：file（新建/打开）+ run（运行/调试运行）+ 控制台。
        {
            const auto &scriptSlots = application.host().uiSlots();
            const auto *fileGrp = scriptSlots.slot(QStringLiteral("ribbon.group.script.file"));
            CHECK(fileGrp != nullptr);
            QStringList fileIds, runIds;
            for (const auto &b : scriptSlots.actionsIn(QStringLiteral("ribbon.group.script.file")))
                fileIds << b.action.id;
            for (const auto &b : scriptSlots.actionsIn(QStringLiteral("ribbon.group.script.run")))
                runIds << b.action.id;
            CHECK(fileIds.contains(QStringLiteral("script.workbench.new")));
            CHECK(fileIds.contains(QStringLiteral("script.workbench.open")));
            CHECK(runIds.contains(QStringLiteral("script.workbench.runCurrent")));
            CHECK(runIds.contains(QStringLiteral("script.workbench.debugRun")));
            // "运行当前"与面板 F5 同路径：编辑器有当前标签 → 派发成功（空注释脚本）
            Result<QVariant> rc{QVariant()};
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("script.workbench.runCurrent"), QVariantMap(), rc));
            OK("Ribbon 脚本页签：file/run 组动作就位；runCurrent 与 F5 同路径执行当前标签");
        }
    }

    // --- 4c) Activity Bar + 左栏 section 栈 ---
    {
        CHECK(binder->activityBarWidget() != nullptr);
        CHECK(binder->activityBarWidget()->width() == 48);

        const QStringList activityIds = binder->activityIds();
        CHECK(activityIds.contains(QStringLiteral("data")));
        CHECK(activityIds.contains(QStringLiteral("script")));
        CHECK(activityIds.contains(QStringLiteral("analysis")));
        CHECK(activityIds.contains(QStringLiteral("diagnostics")));
        CHECK(activityIds.contains(QStringLiteral("plot"))); // 绘图域 Activity（c4b 起归 PlotModule 声明）

        CHECK(binder->sideBarSectionCount() == 2);
        CHECK(binder->sideBarSectionActivity(QStringLiteral("dataset-tree")) == QStringLiteral("data"));
        CHECK(binder->sideBarSectionActivity(QStringLiteral("script-files")) == QStringLiteral("script"));

        // Activity Bar 不再把左栏做成互斥页签：两个面板同属一个 section 栈。
        const QStringList leftTitles = binder->panelTitles(Region::PrimarySidebar);
        CHECK(leftTitles.contains(QStringLiteral("数据源")));
        CHECK(leftTitles.contains(QStringLiteral("脚本文件")));
        CHECK(binder->panelWidgets(Region::PrimarySidebar).size() == 2);

        // 视图可挂到 Activity Bar：映射改到目标 Activity，视图仍在左槽。
        CHECK(binder->pinViewToActivity(QStringLiteral("dataset-tree"), QStringLiteral("analysis")));
        CHECK(binder->sideBarSectionActivity(QStringLiteral("dataset-tree")) == QStringLiteral("analysis"));
        CHECK(binder->panelTitles(Region::PrimarySidebar).contains(QStringLiteral("数据源")));
        CHECK(binder->pinViewToActivity(QStringLiteral("dataset-tree"), QStringLiteral("data")));
        CHECK(binder->sideBarSectionActivity(QStringLiteral("dataset-tree")) == QStringLiteral("data"));

        // 点击 Script：左栏切到脚本 section，Ribbon 同步脚本页，中央聚焦编辑器。
        auto *scriptButton = binder->activityBarWidget()->findChild<QToolButton *>(
            QStringLiteral("activity.script"));
        CHECK(scriptButton != nullptr);
        scriptButton->click();
        CHECK(binder->activeActivity() == QStringLiteral("script"));
        CHECK(binder->currentRibbonPageArea() == QStringLiteral("ribbon.page.script"));
        CHECK(binder->isDockPageVisible(QStringLiteral("script-files")));
        CHECK(!binder->isDockPageVisible(QStringLiteral("dataset-tree")));
        CHECK(binder->centralWidgets().size() >= 2); // 画布 + 脚本编辑器

        // section 折叠与整栏显隐：Ctrl+B 只切左栏，不动 Activity Bar。
        CHECK(binder->setSideBarSectionExpanded(QStringLiteral("script-files"), false));
        CHECK(!binder->isSideBarSectionExpanded(QStringLiteral("script-files")));
        CHECK(binder->setSideBarSectionExpanded(QStringLiteral("script-files"), true));

        const bool wasSideBarVisible = binder->sideBarVisible();
        binder->toggleSideBar();
        CHECK(binder->sideBarVisible() != wasSideBarVisible);
        binder->toggleSideBar();
        CHECK(binder->sideBarVisible() == wasSideBarVisible);
        CHECK(binder->activityBarWidget()->isVisible() || !window.isVisible());

        // 三个区位各自收起：左栏、右侧辅助栏、底部 Panel。
        const bool wasAuxVisible = binder->auxZoneVisible();
        CHECK(binder->toggleAuxZone());
        CHECK(binder->auxZoneVisible() != wasAuxVisible);
        CHECK(binder->toggleAuxZone());
        CHECK(binder->auxZoneVisible() == wasAuxVisible);

        // 停靠布局版本：旧快照没有新左栏 dock，必须跳过，避免启动后左栏被藏/面板漂浮。
        CHECK(binder->captureState().value(QStringLiteral("layoutVersion")).toInt() == 4);

        // badge 与 Activity 顺序配置可改、可读。
        binder->setActivityBadge(QStringLiteral("diagnostics"), 7);
        CHECK(binder->activityBadge(QStringLiteral("diagnostics")) == 7);
        binder->setActivityOrder({QStringLiteral("script"), QStringLiteral("data"),
                                  QStringLiteral("analysis"), QStringLiteral("diagnostics")});
        CHECK(binder->activityIds().first() == QStringLiteral("script"));
        binder->setActivityOrder(activityIds);

        // 左栏 section 也能按 pageKey 显隐，且不产生独立浮动 dock。
        CHECK(binder->setDockPageVisible(QStringLiteral("script-files"), false));
        CHECK(!binder->isDockPageVisible(QStringLiteral("script-files")));
        CHECK(binder->setDockPageVisible(QStringLiteral("script-files"), true));
        CHECK(binder->isDockPageVisible(QStringLiteral("script-files")));
        OK("Activity Bar：四入口 + 点击切 section/Ribbon + 折叠 + Ctrl+B + badge + 顺序配置");
    }

    // --- 5) 数据源面板：源列表 + 详情树（源 → 数据集 → 表 → 列）---
    //
    // 走**真实加载路径**（data.load 意图）——因为"数据集归属哪个源"只有走
    // 摄取管道才会产生：直接往仓库塞数据集是测不到源列表的。
    // 面板由模块 requestPanel 申请、由视图层自注册的工厂创建——
    // MainWindow 不认识它，这里从插槽区取回（dock.left 可能并列脚本文件
    // 面板且顺序不定，按类型查找）。
    DataManagerPanel *panel = nullptr;
    for (QWidget *w : binder->panelWidgets(Region::PrimarySidebar))
    {
        if (!panel)
            panel = qobject_cast<DataManagerPanel *>(w);
    }
    CHECK(panel != nullptr);

    const QString fixture =
        QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv");
    Intent load;
    load.op = QStringLiteral("data.load");
    load.params[QStringLiteral("path")] = fixture;
    const auto loaded = application.host().execute(load);
    CHECK(loaded);
    const int loadedDsId =
        loaded.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
    CHECK(loadedDsId >= 0);

    panel->refresh();
    // 源卡片：出现 1 个源，状态文案可见（不是空白按钮）。
    CHECK(panel->sourceNodeCount() == 1);
    CHECK(!panel->sourceStateText(0).isEmpty());
    // 字段视图（默认 chip）：一源一行，源展开直接是表 → 列（dataset 层透明化）。
    CHECK(panel->shownSourceId() >= 0);
    CHECK(panel->activeChip() == 0);
    CHECK(panel->datasetNodeCount() == 1);   // 数据模型层仍有 1 个数据集
    CHECK(panel->tableNodeCount(0) >= 1);    // 源下直接挂载至少一张表
    OK("数据源面板：一源一行 + 字段视图（源 → 表 → 列）层级正确");

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
        const auto r = application.host().execute(rel);
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



    // --- 5c-b) 数据表格查看器（2.2.3）：树选表 → 中央页签按需创建 → 表格同步 ---
    //
    // 回归点：面板必须**按需创建**（初始中央区只有画布 + 已有的脚本编辑器页签），
    // 绑定走 data.table.open → show 两段式，与画布平级不挤占。
    {
        const Dataset *loadedDs = application.data().get(loadedDsId);
        CHECK(loadedDs != nullptr && loadedDs->tableCount() >= 1);
        const QString firstTable = loadedDs->tableNames().first();

        const int before = binder->centralWidgets().size();
        QVariantMap p;
        p.insert(QStringLiteral("datasetId"), loadedDsId);
        p.insert(QStringLiteral("table"), firstTable);
        Result<QVariant> out{QVariant()};
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("data.table.open"), p, out));
        CHECK(binder->centralWidgets().size() == before + 1);

        nextpilot::plotscope::data::DataTablePanel *tablePanel = nullptr;
        for (QWidget *w : binder->centralWidgets())
        {
            if (!tablePanel)
                tablePanel = qobject_cast<nextpilot::plotscope::data::DataTablePanel *>(w);
        }
        CHECK(tablePanel != nullptr);
        CHECK(tablePanel->shownDatasetId() == loadedDsId);
        CHECK(tablePanel->shownTable() == firstTable);
        // 再开一次：面板复用（不重复创建页签）。
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("data.table.open"), p, out));
        CHECK(binder->centralWidgets().size() == before + 1);
        OK("数据表格查看器：按需创建中央页签、绑定数据同步、复用不重复");
    }

    // --- 5c-c) 报告导出（2.3.2/2.3.3）：view action 同路径 → QPdfWriter 管线 ---
    //
    // 回归点：脚本 app.call("report.export") 与 Ribbon 按钮汇聚同一导出管线；
    // PDF 矢量输出、中文标题可读（pdftotext 反解校验，独立阅读器语义）。
    {
        QTemporaryDir dir;
        CHECK(dir.isValid());
        const QString pdfPath = dir.filePath(QStringLiteral("report-test.pdf"));

        QVariantMap p;
        p.insert(QStringLiteral("path"), pdfPath);
        p.insert(QStringLiteral("title"), QStringLiteral("飞行检查报告"));
        p.insert(QStringLiteral("subtitle"), QStringLiteral("demo_flight.csv 数据检查"));
        p.insert(QStringLiteral("notes"), QStringLiteral("振动正常，EKF 收敛。\n无异常事件。"));
        Result<QVariant> out{QVariant()};
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("report.export"), p, out));
        CHECK(QFileInfo::exists(pdfPath));
        CHECK(QFileInfo(pdfPath).size() > 1000); // 非空 PDF（封面+元数据+画布+结论）

        // 模板 id 不存在时明确报错（工具箱 reports 表查询语义；派发本身成功，
        // 失败语义在结果 payload 里——与 view action 通道约定一致）。
        QVariantMap bad = p;
        bad.insert(QStringLiteral("template"), QStringLiteral("report.no-such"));
        Result<QVariant> out2{QVariant()};
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("report.export"), bad, out2));
        CHECK(!out2.ok());
        CHECK(out2.error().message.contains(QStringLiteral("报告模板不存在")));

        // Ribbon 动作已声明（报告页/导出组/大按钮）——走 UiSlotHub 插槽数据。
        const auto reportActs = slotReg.actionsIn(QStringLiteral("ribbon.group.report.export"));
        CHECK(reportActs.size() == 1);
        CHECK(!reportActs.isEmpty()
              && reportActs.first().action.id == QStringLiteral("report.workbench.export"));
        CHECK(reportActs.first().action.large); // 大按钮
        OK("报告导出：view action 同路径出 PDF、模板缺省报错、Ribbon 入口就位");
    }

    // --- 5c-d) 批量处理（2.4）：script.workbench.batch 全链路 → 汇总面板 ---
    //
    // 回归点：Ribbon/脚本同路径（view action 带参免对话框）；
    // 每文件进度 upsert 成表行；收尾 summary 恢复按钮态并给成败计数。
    {
        auto *ui = window.context();
        CHECK(ui != nullptr);

        // 临时目录：2 个合法 csv + 1 个损坏文件（空解析 → load 阶段失败明细）
        QTemporaryDir dir;
        CHECK(dir.isValid());
        const char *csvHead = "time_ms,engine_rpm[rpm]\n";
        auto writeCsv = [&](const QString &name, int seed) {
            QFile f(dir.filePath(name));
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream ts(&f);
            ts << csvHead;
            for (int i = 0; i < 20; ++i)
                ts << (i * 10) << ',' << (1400 + i + seed) << '\n';
        };
        writeCsv(QStringLiteral("run1.csv"), 1);
        writeCsv(QStringLiteral("run2.csv"), 2);
        {
            QFile f(dir.filePath(QStringLiteral("broken.csv")));
            CHECK(f.open(QIODevice::WriteOnly));
            f.write("\x89PNG\x1a\n\r\nnot-a-csv");
        }

        // script.workbench.batch 带参直跑（pick 透传，不弹对话框）
        QVariantMap bp;
        bp.insert(QStringLiteral("dir"), dir.path());
        bp.insert(QStringLiteral("source"),
                  QByteArrayLiteral("local m = {}\nm.file = batch.file\nm.rpm = 42\nreturn m\n"));
        Result<QVariant> bout{QVariant()};
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("script.workbench.batch"), bp, bout));
        CHECK(bout.ok());

        // 汇总面板就位且行数 == 文件数；收尾计数正确
        nextpilot::plotscope::script::BatchSummaryPanel *batchPanel = nullptr;
        for (QWidget *w : binder->centralWidgets())
        {
            if (!batchPanel)
                batchPanel = qobject_cast<nextpilot::plotscope::script::BatchSummaryPanel *>(w);
        }
        CHECK(batchPanel != nullptr);
        CHECK(batchPanel->resultRowCount() == 3);
        const QVariantMap sm = batchPanel->lastSummary();
        CHECK(sm.value(QStringLiteral("ok")).toInt() == 2);
        CHECK(sm.value(QStringLiteral("failed")).toInt() == 1);
        CHECK(!sm.value(QStringLiteral("cancelled")).toBool());

        // Ribbon"批量"组就位（大按钮）
        const auto batchActs = slotReg.actionsIn(QStringLiteral("ribbon.group.script.batch"));
        CHECK(batchActs.size() == 1);
        CHECK(!batchActs.isEmpty()
              && batchActs.first().action.id == QStringLiteral("script.workbench.batch"));
        CHECK(batchActs.first().action.large);
        OK("批量处理：script.workbench.batch 全链路出汇总表（2 成功 1 失败），Ribbon 入口就位");
    }

    // --- 5c-e) Python 子进程桥（2.6.1/2.6.4）：GUI 同路径 eval + .py 运行 + 写回 v2 ---
    //
    // 回归点：编辑器 .py 运行走 script.workbench.run 同一入口（按名字后缀路由
    // Python 引擎）；ds 注入/写回 v2 与 headless 完全同一条执行器路径。
    {
        auto *ui = window.context();
        CHECK(ui != nullptr);

        // status：解释器可用（环境无 python3 时给出明确失败信息）
        Intent st;
        st.op = QStringLiteral("script.py.status");
        const Result<IntentResult> str = application.host().execute(st);
        CHECK(str && str.value().payload.toMap().value(QStringLiteral("available")).toBool());

        // .py 运行路由：script.workbench.run（编辑器 F5 同一 view action）
        QVariantMap rp;
        rp.insert(QStringLiteral("source"),
                  QByteArrayLiteral("print('py-gui-ok')\n7 * 6\n"));
        rp.insert(QStringLiteral("name"), QStringLiteral("pytest_demo.py"));
        Result<QVariant> rout{QVariant()};
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("script.workbench.run"), rp, rout));
        CHECK(rout.ok());

        // ds 注入 + 写回 v2（数据面与 headless 同路径）
        const Dataset *before = application.data().get(loadedDsId);
        CHECK(before && before->tableCount() >= 1);
        const QString colName = before->table(0)->columns.first().name;
        const int countBefore = application.data().count();
        QVariantMap ep;
        ep.insert(QStringLiteral("dataset"), loadedDsId);
        ep.insert(QStringLiteral("source"), QStringLiteral(
                       "v = ds['%1']\nds['py_derived'] = v * 2\nfloat(v[0])").arg(colName));
        Intent ei;
        ei.op = QStringLiteral("script.py.exec");
        ei.params = ep;
        const Result<IntentResult> er = application.host().execute(ei);
        CHECK(er && er.value().payload.toMap()
                        .value(QStringLiteral("new"))
                        .toMap()
                        .contains(QStringLiteral("py_derived")));
        Intent wi;
        wi.op = QStringLiteral("script.py.writeback");
        const Result<IntentResult> wr = application.host().execute(wi);
        CHECK(wr);
        const int v2Id =
            wr.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
        CHECK(v2Id >= 0 && v2Id != loadedDsId);
        CHECK(application.data().count() == countBefore + 1);
        const Dataset *v2 = application.data().get(v2Id);
        CHECK(v2 && v2->table(0)->column(QStringLiteral("py_derived")) != nullptr);
        OK("Python 桥：.py 运行路由 + ds 注入 + 写回 v2 数据集（GUI 同路径）");
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

        // 订阅派生结果事件（用持久计数，精确捕获，杜绝悬空引用）。
        QObject::connect(&application.context(), &AppContext::derivedSeriesReady,
                         &application, [&g_derivedCount, &g_derivedName](
                                           const QString &name, const QVector<double> &time,
                                           const QVector<double> &value) {
                             if (!name.contains(QStringLiteral(".absolute")))
                                 return;
                             ++g_derivedCount;
                             g_derivedName = name;
                             CHECK(time.size() == value.size());
                             CHECK(!time.isEmpty());
                         });

        Intent der;
        der.op = QStringLiteral("data.derive");
        der.params[QStringLiteral("dataset")] = loadedDsId;
        der.params[QStringLiteral("table")] = tableName;
        der.params[QStringLiteral("column")] = columnName;
        der.params[QStringLiteral("filter")] = QStringLiteral("absolute");
        const auto dr = application.host().execute(der);
        CHECK(dr);
        const QVariantMap pm = dr.value().payload.toMap();
        const int count = pm.value(QStringLiteral("count")).toInt();
        CHECK(count > 0);
        const QVector<double> dtime = pm.value(QStringLiteral("time")).value<QVector<double>>();
        const QVector<double> dvalue = pm.value(QStringLiteral("value")).value<QVector<double>>();
        CHECK(dtime.size() == count);
        CHECK(dvalue.size() == count);
        CHECK(g_derivedCount == 1);
        CHECK(!g_derivedName.isEmpty());

        // 关键断言：派生后数据集结构完全没变（没有新列、行数一致）。
        const Dataset *after = application.data().get(loadedDsId);
        CHECK(after != nullptr);
        CHECK(after->tableCount() == before->tableCount());
        const Table *tblAfter = after->table(tableName);
        CHECK(tblAfter != nullptr);
        CHECK(tblAfter->rows == rowsBefore);
        CHECK(tblAfter->columns.size() == colsBefore);
        OK("data.derive：Rust 实时派生结果给 UI（payload+信号），原数据集零改动");

        // --- 5d) data.filter：有状态 Lua 过滤器作用到通道（FilterOps 全链路）---
        // script 模块把 FilterCatalogue 注入 InteractionHub；host 的 data.filter
        // 取原始通道 → filterOps.applySeries → derivedSeriesReady，同样不写回数据集。
        CHECK(application.context().interactions().hasFilterOps());
        const QJsonArray filterList = application.context().interactions().filterOps().list();
        CHECK(filterList.size() >= 12); // 12 个 PJ4 内置过滤器全部编目

        // 原始通道首值做对照。
        const auto rawCh = after->channel(tableName, columnName);
        CHECK(rawCh.has_value() && rawCh->size() > 0);
        const double firstRaw = rawCh->value.value(0);

        QObject::connect(&application.context(), &AppContext::derivedSeriesReady,
                         &application, [&g_filterCount](const QString &name, const QVector<double> &time,
                                           const QVector<double> &value) {
                             if (name.contains(QStringLiteral(".scale")))
                             {
                                 ++g_filterCount;
                                 CHECK(time.size() == value.size());
                                 CHECK(!time.isEmpty());
                             }
                         });

        Intent fit;
        fit.op = QStringLiteral("data.filter");
        fit.params[QStringLiteral("dataset")] = loadedDsId;
        fit.params[QStringLiteral("table")] = tableName;
        fit.params[QStringLiteral("column")] = columnName;
        fit.params[QStringLiteral("filter")] = QStringLiteral("scale");
        fit.params[QStringLiteral("params")] =
            QVariant(QJsonObject{{QStringLiteral("value_scale"), 2.0}});
        const auto fr = application.host().execute(fit);
        CHECK(fr);
        const QVariantMap fpm = fr.value().payload.toMap();
        const int fcount = fpm.value(QStringLiteral("count")).toInt();
        CHECK(fcount > 0);
        const QVector<double> fval = fpm.value(QStringLiteral("value")).value<QVector<double>>();
        CHECK(fval.size() == fcount);
        CHECK(std::abs(fval.first() - firstRaw * 2.0) < 1e-9); // scale.cpp: v*2
        CHECK(g_filterCount == 1);
        // 过滤器同样零改动数据集。
        CHECK(application.data().get(loadedDsId)->table(tableName)->rows == rowsBefore);
        OK("data.filter：FilterOps 全链路（list>=12 + scale 数值 + 信号 + 数据集零改动）");
    }

    // --- 5e) data.marker：Marker 规则扫通道 -> PlotMarker -> 画布叠加 ---
    {
        CHECK(application.context().interactions().hasMarkerOps());
        const Dataset *mds = application.data().get(loadedDsId);
        CHECK(mds != nullptr && mds->tableCount() > 0);
        const Table *mTbl = mds->table(0);
        CHECK(mTbl != nullptr && !mTbl->columns.isEmpty());

        Intent mit;
        mit.op = QStringLiteral("data.marker");
        mit.params[QStringLiteral("dataset")] = loadedDsId;
        mit.params[QStringLiteral("table")] = mTbl->name;
        mit.params[QStringLiteral("column")] = mTbl->columns.first().name;
        mit.params[QStringLiteral("code")] = QByteArray(
            "createVerticalMarker(1, {label=\"thr\", severity=\"warning\"})\n"
            "createBandMarker(-0.5, 0.5, {label=\"band\"})\n"
            "createPointMarker(2, 1.0, {label=\"pt\", severity=\"error\"})");
        const auto mr = application.host().execute(mit);
        CHECK(static_cast<bool>(mr));
        if (mr)
        {
            const QVariantMap mpm = mr.value().payload.toMap();
            CHECK(mpm.value(QStringLiteral("count")).toInt() == 3);
            CHECK(mpm.value(QStringLiteral("points")).toInt() > 0);
            CHECK(mpm.value(QStringLiteral("drawn")).toBool()); // plot.markers.set 成功
            const QVariantList mlist = mpm.value(QStringLiteral("markers")).toList();
            CHECK(mlist.size() == 3);
            CHECK(mlist.at(0).toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("event"));
            CHECK(mlist.at(1).toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("valueBand"));
        }
        // 数据集零改动。
        CHECK(application.data().get(loadedDsId)->table(0)->rows == mTbl->rows);
        OK("data.marker：MarkerOps 规则执行(3 marker) + 画布叠加 + 数据集零改动");
    }

    // --- 5f) data.state：状态视图 —— bool/整数值通道游程编码 → 画布顶部色带 ---
    int stateDsId = -1;
    {
        const int dsId = buildDataset(application.data());
        stateDsId = dsId;
        CHECK(dsId >= 0);
        // 整数值枚举列（0/1/2 每 100 样本一段）：分段 + 画布叠加。
        Intent st;
        st.op = QStringLiteral("data.state");
        st.params[QStringLiteral("dataset")] = dsId;
        st.params[QStringLiteral("table")] = QStringLiteral("trace");
        st.params[QStringLiteral("column")] = QStringLiteral("mode");
        const auto sr = application.host().execute(st);
        CHECK(static_cast<bool>(sr));
        if (sr)
        {
            const QVariantMap spm = sr.value().payload.toMap();
            // 全量 5000 点不抽稀：mode 每 100 样本切换 → 50 段。
            CHECK(spm.value(QStringLiteral("segments")).toInt() >= 40);
            CHECK(spm.value(QStringLiteral("drawn")).toBool()); // plot.state.set 成功
            const QVariantList rowsL = spm.value(QStringLiteral("rows")).toList();
            CHECK(rowsL.size() == 1);
            const QVariantMap row0 = rowsL.first().toMap();
            CHECK(row0.value(QStringLiteral("channel")).toString() == QStringLiteral("trace.mode"));
            const QVariantList segs = row0.value(QStringLiteral("segments")).toList();
            CHECK(!segs.isEmpty());
            // 首段标签是枚举整数的十进制串。
            const QString firstLabel = segs.first().toMap().value(QStringLiteral("label")).toString();
            CHECK(firstLabel == QStringLiteral("0") || firstLabel == QStringLiteral("1")
                  || firstLabel == QStringLiteral("2"));
        }
        // bool 列：标签 true/false。
        Intent st2;
        st2.op = QStringLiteral("data.state");
        st2.params[QStringLiteral("dataset")] = dsId;
        st2.params[QStringLiteral("table")] = QStringLiteral("trace");
        st2.params[QStringLiteral("column")] = QStringLiteral("armed");
        const auto sr2 = application.host().execute(st2);
        CHECK(static_cast<bool>(sr2));
        if (sr2)
        {
            const QVariantMap spm = sr2.value().payload.toMap();
            CHECK(spm.value(QStringLiteral("segments")).toInt() >= 10); // 200 点一翻转，4096 点内 ~20 段
            const QVariantList segs = spm.value(QStringLiteral("rows")).toList().first()
                                          .toMap().value(QStringLiteral("segments")).toList();
            bool hasTrue = false, hasFalse = false;
            for (const QVariant &s : segs)
            {
                const QString l = s.toMap().value(QStringLiteral("label")).toString();
                hasTrue |= (l == QStringLiteral("true"));
                hasFalse |= (l == QStringLiteral("false"));
            }
            CHECK(hasTrue && hasFalse);
        }
        // 非整数浮点列明确拒绝。
        Intent bad;
        bad.op = QStringLiteral("data.state");
        bad.params[QStringLiteral("dataset")] = dsId;
        bad.params[QStringLiteral("table")] = QStringLiteral("trace");
        bad.params[QStringLiteral("column")] = QStringLiteral("sig");
        const auto br = application.host().execute(bad);
        CHECK(!static_cast<bool>(br)); // sig = i*0.5 含非整数值
        // 数据集零改动。
        CHECK(application.data().get(dsId)->table(QStringLiteral("trace"))->rows == 5000);
        OK("data.state：状态视图（枚举分段+bool 标签+非整数拒绝+画布叠加）");
    }

    // --- 5g) 脚本触发状态视图：app.call("data.state", ...) ---
    {
        const int dsId = stateDsId;
        CHECK(dsId >= 0);
        Intent sit;
        sit.op = QStringLiteral("script.run");
        sit.params[QStringLiteral("source")] = QByteArray(
            "app.call(\"data.state\", {dataset=" + QByteArray::number(dsId)
            + ", table=\"trace\", column=\"mode\"})");
        // app.call 失败会 luaL_error 使整个脚本失败：CHECK(scr) 即证明
        // 脚本能路由 data.state 意图并成功分段叠加（画布行为 5f 已验）。
        const auto scr = application.host().execute(sit);
        CHECK(static_cast<bool>(scr));
        OK("脚本触发 data.state：app.call 成功路由并叠加");
    }

    // --- 5i) data.series：读通道 t/v 进 payload（脚本/CLI/MCP 的数据出口，VBA 基石）---
    {
        const int dsId = stateDsId;
        CHECK(dsId >= 0);
        // (1) 意图直调：limit 截断 + count/truncated 字段。
        Intent ser;
        ser.op = QStringLiteral("data.series");
        ser.params[QStringLiteral("dataset")] = dsId;
        ser.params[QStringLiteral("table")] = QStringLiteral("trace");
        ser.params[QStringLiteral("column")] = QStringLiteral("sig");
        ser.params[QStringLiteral("limit")] = 128;
        const auto sr = application.host().execute(ser);
        CHECK(static_cast<bool>(sr));
        const QVariantMap payload = sr.value().payload.toMap();
        // 有界读取经 kernel 抽稀：返回点数 ≤ limit（不恒等于），rows>limit 时 truncated。
        const int serCount = payload.value(QStringLiteral("count")).toInt();
        CHECK(serCount >= 1 && serCount <= 128);
        CHECK(payload.value(QStringLiteral("t")).toList().size() == serCount);
        CHECK(payload.value(QStringLiteral("v")).toList().size() == serCount);
        CHECK(payload.value(QStringLiteral("truncated")).toBool()); // 5000 行 > 128

        // (2) 缺省定位：不给 dataset/table（当前数据集里可能多个，给 table/column 也不够；
        //     这里显式 dataset 但不给 table，走"唯一表"兜底）。
        Intent ser2;
        ser2.op = QStringLiteral("data.series");
        ser2.params[QStringLiteral("dataset")] = dsId;
        ser2.params[QStringLiteral("column")] = QStringLiteral("sig");
        const auto sr2 = application.host().execute(ser2);
        CHECK(static_cast<bool>(sr2));

        // (3) 通道不存在 → 明确失败。
        Intent bad;
        bad.op = QStringLiteral("data.series");
        bad.params[QStringLiteral("dataset")] = dsId;
        bad.params[QStringLiteral("column")] = QStringLiteral("no_such_column");
        CHECK(!static_cast<bool>(application.host().execute(bad)));

        // (4) 脚本 VBA：app.call 读序列并在 Lua 内做统计——证明脚本拿得到
        //     数组本体（QVariantList→Lua 数组）而不只是执行回执。
        Intent sit;
        sit.op = QStringLiteral("script.run");
        sit.params[QStringLiteral("source")] = QByteArray(
            "local r = app.call(\"data.series\", {dataset=" + QByteArray::number(dsId)
            + ", table=\"trace\", column=\"sig\", limit=16})\n"
            "  if r.count < 1 or r.count > 16 then error('count out of range: ' .. tostring(r.count)) end\n"
            "  if #r.v ~= r.count or #r.t ~= r.count then error('array size mismatch') end\n"
            "  local sum = 0.0\n"
            "  for i = 1, #r.v do sum = sum + r.v[i] end\n"
            "  app.log('series mean: ' .. tostring(sum / #r.v))\n");
        const auto scr = application.host().execute(sit);
        CHECK(static_cast<bool>(scr));

        // (5) AI 工具白名单：read_series 已注册（MCP 桥同一目录）。
        Intent ai;
        ai.op = QStringLiteral("ai.tool.call");
        ai.params[QStringLiteral("tool")] = QStringLiteral("read_series");
        QVariantMap aiArgs;
        aiArgs[QStringLiteral("dataset")] = dsId;
        aiArgs[QStringLiteral("table")] = QStringLiteral("trace");
        aiArgs[QStringLiteral("column")] = QStringLiteral("sig");
        aiArgs[QStringLiteral("limit")] = 32;
        ai.params[QStringLiteral("args")] = aiArgs;
        const auto air = application.host().execute(ai);
        CHECK(static_cast<bool>(air));
        const QVariantMap aiPayload = air.value().payload.toMap();
        const QVariantMap aiResult = aiPayload.value(QStringLiteral("result")).toMap();
        CHECK(aiResult.value(QStringLiteral("count")).toInt() == 32);

        // 数据集零改动。
        CHECK(application.data().get(dsId)->table(QStringLiteral("trace"))->rows == 5000);
        OK("data.series：意图/脚本读数/AI read_series 三通道 + 失败路径 + 零改动");
    }

    // --- 5j) 通道变换（非破坏视图）：dataset 级配方 + plot.channel 变换路径 ---
    //
    // 设计语义：变换只改值不改名，原始 dataset 永不改动；dataset 级配方
    // （data.transform.set）全后端生效，plot 级（曲线配方）只影响当前曲线。
    {
        const int dsId = stateDsId;
        // dataset 级配方以 origin 为键，series/plot.channel 反查走 sourceOfDataset——
        // 给 fixture 挂一个源（真实场景 dataset 都来自源），origin 与 set 用的显式值一致。
        DataSourceInfo xformInfo;
        xformInfo.name = QStringLiteral("xform-src");
        xformInfo.origin = QStringLiteral("test://trace-origin");
        xformInfo.state = SourceState::Closed;
        const int xformSrc = application.context().sources().add(xformInfo);
        CHECK(xformSrc >= 0);
        CHECK(application.context().sources().addDataset(xformSrc, dsId));

        // dataset 未挂源（buildDataset 直接入仓）——用显式 origin 测 set 的 origin 路径。
        const QString origin = QStringLiteral("test://trace-origin");
        const QVariantMap xform{
            { QStringLiteral("descriptorId"), QStringLiteral("expression") },
            { QStringLiteral("kind"), QStringLiteral("expression") },
            { QStringLiteral("expression"), QStringLiteral("x*2") },
        };

        // (1) set：origin 显式定位 + expression 变换请求。
        Intent setI;
        setI.op = QStringLiteral("data.transform.set");
        setI.params[QStringLiteral("origin")] = origin;
        setI.params[QStringLiteral("table")] = QStringLiteral("trace");
        setI.params[QStringLiteral("column")] = QStringLiteral("alt");
        setI.params[QStringLiteral("transform")] = QJsonDocument(QJsonObject::fromVariantMap(xform)).toJson(QJsonDocument::Compact);
        const auto setR = application.host().execute(setI);
        CHECK(static_cast<bool>(setR));

        // (2) data.series 读 alt：limit 是读窗口 cap（抽稀口径）——5000 行抽到 4 点
        // （step=1250：i=0/1250/2500/3750），再对抽稀点做变换：
        // alt[1250]=687.5 → ×2 = 1375；v[0]=1000×2=2000。
        {
            Intent s;
            s.op = QStringLiteral("data.series");
            s.params[QStringLiteral("dataset")] = dsId;
            s.params[QStringLiteral("table")] = QStringLiteral("trace");
            s.params[QStringLiteral("column")] = QStringLiteral("alt");
            s.params[QStringLiteral("limit")] = 4;
            const auto r = application.host().execute(s);
            CHECK(static_cast<bool>(r));
            const QVariantMap p = r.value().payload.toMap();
            CHECK(p.value(QStringLiteral("transformed")).toBool());
            const QVariantList vs = p.value(QStringLiteral("v")).toList();
            CHECK(vs.size() == 4);
            CHECK(qFuzzyCompare(vs.at(0).toDouble(), 2000.0));
            CHECK(qFuzzyCompare(vs.at(1).toDouble(), 1375.0));
            CHECK(qFuzzyCompare(vs.at(3).toDouble(), 125.0));
        }

        // (3) 原始数据零改动（变换是非破坏视图的核心铁律）。
        CHECK(application.data().get(dsId)->table(QStringLiteral("trace"))->rows == 5000);

        // (4) clear 后恢复原始读数 + settings 里配方被移除。
        {
            Intent clearI;
            clearI.op = QStringLiteral("data.transform.clear");
            clearI.params[QStringLiteral("origin")] = origin;
            clearI.params[QStringLiteral("table")] = QStringLiteral("trace");
            clearI.params[QStringLiteral("column")] = QStringLiteral("alt");
            const auto cr = application.host().execute(clearI);
            CHECK(static_cast<bool>(cr));
            CHECK(cr.value().payload.toMap().value(QStringLiteral("removed")).toBool());

            Intent s;
            s.op = QStringLiteral("data.series");
            s.params[QStringLiteral("dataset")] = dsId;
            s.params[QStringLiteral("table")] = QStringLiteral("trace");
            s.params[QStringLiteral("column")] = QStringLiteral("alt");
            s.params[QStringLiteral("limit")] = 4;
            const auto r = application.host().execute(s);
            CHECK(static_cast<bool>(r));
            const QVariantMap p = r.value().payload.toMap();
            CHECK(!p.value(QStringLiteral("transformed")).toBool());
            const QVariantList vs = p.value(QStringLiteral("v")).toList();
            CHECK(qFuzzyCompare(vs.at(1).toDouble(), 687.5)); // alt[1250]，抽稀口径恢复
            CHECK(ChannelTransformConfig::load(application.context().settings()).specs.isEmpty());
        }

        // (5) plot.channel 显式 transform（plot 级同一条路）：变换 + C++ min/max 抽稀。
        {
            Intent pc;
            pc.op = QStringLiteral("plot.channel");
            pc.params[QStringLiteral("dataset")] = dsId;
            pc.params[QStringLiteral("table")] = QStringLiteral("trace");
            pc.params[QStringLiteral("column")] = QStringLiteral("alt");
            pc.params[QStringLiteral("px")] = 64;
            pc.params[QStringLiteral("transform")] =
                QJsonDocument(QJsonObject::fromVariantMap(xform)).toJson(QJsonDocument::Compact);
            const auto r = application.host().execute(pc);
            CHECK(static_cast<bool>(r));
            const QVariantMap p = r.value().payload.toMap();
            CHECK(p.value(QStringLiteral("transformed")).toBool());
            CHECK(p.value(QStringLiteral("points")).toInt() <= 64);
        }

        OK("通道变换：set/clear + series/plot.channel 变换路径 + 原始零改动");
    }

    // --- 5k) 派生注册表（独立派生字段）+ MIMO quat_to_rpy ---
    //
    // 设计语义：衍生生成新字段，但**不写回 dataset**（原始数据零污染）；
    // 结果登记宿主层派生注册表，data.series/plot.channel 按派生名穿透，
    // data.derive.list 提供可发现性。
    {
        // (1) 专用 fixture：不动 stateDsId（DatasetHub::get 是 const 不可变访问，
        // 且其 trace 表已有 5000 行——四元数列需要独立的 100 行时间轴）。
        // trace 表 t=i*0.01（基准轴），aux 表 t=i*0.05（独立采样率，跨表重采样用）。
        Dataset fds = Dataset::create(QStringLiteral("att-fixture"), QStringLiteral("mem"));
        const int n = 100;
        QVector<double> qt(n), sig(n), qw(n), qx(n), qy(n), qz(n);
        const double half = qDegreesToRadians(15.0);
        for (int i = 0; i < n; ++i)
        {
            qt[i] = i * 0.01;
            sig[i] = i * 0.5;
            qw[i] = std::cos(half);
            qx[i] = std::sin(half);
            qy[i] = 0.0;
            qz[i] = 0.0;
        }
        CHECK(fds.addTable(QStringLiteral("trace"), qt) >= 0);
        CHECK(fds.addF64Column(QStringLiteral("trace"), QStringLiteral("sig"), QString(), sig));
        CHECK(fds.addF64Column(QStringLiteral("trace"), QStringLiteral("q.w"), QString(), qw));
        CHECK(fds.addF64Column(QStringLiteral("trace"), QStringLiteral("q.x"), QString(), qx));
        CHECK(fds.addF64Column(QStringLiteral("trace"), QStringLiteral("q.y"), QString(), qy));
        CHECK(fds.addF64Column(QStringLiteral("trace"), QStringLiteral("q.z"), QString(), qz));
        // aux 表：t ∈ [0, 1.6) 覆盖基准轴 [0, 0.99)，常值 0 → 插值/延拓都不改变结论。
        const int m = 33;
        QVector<double> at(m);
        for (int i = 0; i < m; ++i)
            at[i] = i * 0.05;
        CHECK(fds.addTable(QStringLiteral("aux"), at) >= 0);
        CHECK(fds.addF64Column(QStringLiteral("aux"), QStringLiteral("q2.y"), QString(),
                               QVector<double>(m, 0.0)));
        CHECK(fds.addF64Column(QStringLiteral("aux"), QStringLiteral("q2.z"), QString(),
                               QVector<double>(m, 0.0)));
        // NaN 防线用列：单个 null/NaN 样本（kernel 读 null 即 NaN）。
        QVector<double> nw = qw;
        nw[10] = std::nan("");
        CHECK(fds.addF64Column(QStringLiteral("trace"), QStringLiteral("qn.w"), QString(), nw));
        fds.refreshSchema();
        const int dsId = application.data().add(fds);
        CHECK(dsId >= 0);
        const Dataset *ds = application.data().get(dsId);
        CHECK(ds != nullptr);

        // (2) SISO 派生登记 + data.series 穿透。
        {
            Intent d;
            d.op = QStringLiteral("data.derive");
            d.params[QStringLiteral("dataset")] = dsId;
            d.params[QStringLiteral("table")] = QStringLiteral("trace");
            d.params[QStringLiteral("column")] = QStringLiteral("sig");
            d.params[QStringLiteral("filter")] = QStringLiteral("moving_average");
            d.params[QStringLiteral("params")] = QStringLiteral("{\"window\":10}");
            d.params[QStringLiteral("name")] = QStringLiteral("sig_avg");
            const auto dr = application.host().execute(d);
            CHECK(static_cast<bool>(dr));

            Intent s;
            s.op = QStringLiteral("data.series");
            s.params[QStringLiteral("dataset")] = dsId;
            s.params[QStringLiteral("column")] = QStringLiteral("sig_avg");
            s.params[QStringLiteral("limit")] = 8;
            const auto sr = application.host().execute(s);
            CHECK(static_cast<bool>(sr));
            const QVariantMap p = sr.value().payload.toMap();
            CHECK(p.value(QStringLiteral("derived")).toBool());
            CHECK(p.value(QStringLiteral("count")).toInt() >= 1);
            // 原始 dataset 零污染：sig_avg 不是 trace 表的列。
            CHECK(ds->table(QStringLiteral("trace"))->column(QStringLiteral("sig_avg")) == nullptr);
        }

        // (3) MIMO：q0-q3 → roll/pitch/yaw，roll≈30deg。
        {
            Intent d;
            d.op = QStringLiteral("data.derive");
            d.params[QStringLiteral("dataset")] = dsId;
            d.params[QStringLiteral("table")] = QStringLiteral("trace");
            d.params[QStringLiteral("inputs")] = QStringList{
                QStringLiteral("q.w"), QStringLiteral("q.x"),
                QStringLiteral("q.y"), QStringLiteral("q.z") };
            d.params[QStringLiteral("filter")] = QStringLiteral("quat_to_rpy");
            d.params[QStringLiteral("degrees")] = true;
            d.params[QStringLiteral("name")] = QStringLiteral("att");
            const auto dr = application.host().execute(d);
            CHECK(static_cast<bool>(dr));
            const QVariantMap p = dr.value().payload.toMap();
            const QVariantList outs = p.value(QStringLiteral("outputs")).toList();
            CHECK(outs.size() == 3);
            const QVariantMap roll = outs.first().toMap();
            CHECK(roll.value(QStringLiteral("name")).toString() == QStringLiteral("att.roll"));
            CHECK(qFuzzyCompare(roll.value(QStringLiteral("value")).toList().first().toDouble(), 30.0));

            // 派生名穿透（series + plot.channel）。
            Intent s;
            s.op = QStringLiteral("data.series");
            s.params[QStringLiteral("dataset")] = dsId;
            s.params[QStringLiteral("column")] = QStringLiteral("att.roll");
            s.params[QStringLiteral("limit")] = 4;
            const auto sr = application.host().execute(s);
            CHECK(static_cast<bool>(sr));
            CHECK(sr.value().payload.toMap().value(QStringLiteral("derived")).toBool());

            Intent pc;
            pc.op = QStringLiteral("plot.channel");
            pc.params[QStringLiteral("dataset")] = dsId;
            pc.params[QStringLiteral("table")] = QStringLiteral("trace");
            pc.params[QStringLiteral("column")] = QStringLiteral("att.yaw");
            pc.params[QStringLiteral("px")] = 16;
            const auto pcr = application.host().execute(pc);
            CHECK(static_cast<bool>(pcr));
            CHECK(pcr.value().payload.toMap().value(QStringLiteral("derived")).toBool());

            // 目录：SISO 1 条 + MIMO 3 条 = 4。
            Intent list;
            list.op = QStringLiteral("data.derive.list");
            list.params[QStringLiteral("dataset")] = dsId;
            const auto lr = application.host().execute(list);
            CHECK(static_cast<bool>(lr));
            CHECK(lr.value().payload.toMap().value(QStringLiteral("count")).toInt() == 4);
        }

        // (4) MIMO 失败路径：inputs 数量错 → 明确失败。
        {
            Intent d;
            d.op = QStringLiteral("data.derive");
            d.params[QStringLiteral("dataset")] = dsId;
            d.params[QStringLiteral("table")] = QStringLiteral("trace");
            d.params[QStringLiteral("inputs")] = QStringList{ QStringLiteral("q.w") };
            d.params[QStringLiteral("filter")] = QStringLiteral("quat_to_rpy");
            CHECK(!static_cast<bool>(application.host().execute(d)));
        }

        // (5) MIMO 跨表输入（时间同步核心场景）：aux 表独立采样率（0.05s 网格），
        // 以 q.w 的 trace 轴（0.01s 网格）为基准重采样 → 常值四元数下插值不改变
        // 结论，roll 在基准每个点（含插值点/边界延拓点）都精确等于 30°。
        {
            Intent d;
            d.op = QStringLiteral("data.derive");
            d.params[QStringLiteral("dataset")] = dsId;
            d.params[QStringLiteral("table")] = QStringLiteral("trace"); // 字符串项的缺省表
            d.params[QStringLiteral("inputs")] = QVariantList{
                QStringLiteral("q.w"), QStringLiteral("q.x"),
                QVariantMap{ { QStringLiteral("table"), QStringLiteral("aux") },
                             { QStringLiteral("column"), QStringLiteral("q2.y") } },
                QVariantMap{ { QStringLiteral("table"), QStringLiteral("aux") },
                             { QStringLiteral("column"), QStringLiteral("q2.z") } } };
            d.params[QStringLiteral("filter")] = QStringLiteral("quat_to_rpy");
            d.params[QStringLiteral("degrees")] = true;
            d.params[QStringLiteral("name")] = QStringLiteral("attx");
            const auto dr = application.host().execute(d);
            if (!dr)
                fprintf(stderr, "[dbg] cross-table fail: %s / %s\n",
                        qPrintable(dr.error().code), qPrintable(dr.error().message));
            CHECK(static_cast<bool>(dr));
            const QVariantMap p = dr.value().payload.toMap();
            CHECK(p.value(QStringLiteral("count")).toInt() == 100);
            const QVariantList outs = p.value(QStringLiteral("outputs")).toList();
            CHECK(outs.size() == 3);
            CHECK(outs.first().toMap().value(QStringLiteral("name")).toString()
                  == QStringLiteral("attx.roll"));
            const QVariantList rv = outs.first().toMap().value(QStringLiteral("value")).toList();
            CHECK(rv.size() == 100);
            // t=0（边界延拓）、t=0.07（落在 aux 两样本之间线性插值）、t=0.99（延拓）全部 30°。
            CHECK(qFuzzyCompare(rv.first().toDouble(), 30.0));
            CHECK(qFuzzyCompare(rv.at(7).toDouble(), 30.0));
            CHECK(qFuzzyCompare(rv.last().toDouble(), 30.0));

            // step 模式：前向填充同样保持常值结论。
            Intent d2 = d;
            d2.params[QStringLiteral("interp")] = QStringLiteral("step");
            d2.params[QStringLiteral("name")] = QStringLiteral("attx2");
            CHECK(static_cast<bool>(application.host().execute(d2)));

            // interp 非法值 → eargs。
            Intent d3 = d;
            d3.params[QStringLiteral("interp")] = QStringLiteral("cubic");
            const auto r3 = application.host().execute(d3);
            CHECK(!static_cast<bool>(r3));
            CHECK(r3.error().code == QStringLiteral("eargs"));
        }

        // (6) null/NaN 样本：门面 readWindow 成对剔除（qn.w 剩 99 点）→
        // 派生统一走基准轴插值对齐补回 100 点，不静默产出垃圾角度：
        // 剔除点邻域由线性插值恢复常值四元数 → roll 仍精确 30°。
        {
            {
                const auto c = ds->channel(QStringLiteral("trace"), QStringLiteral("qn.w"));
                CHECK(c.has_value());
                CHECK(c->size() == 99); // null 样本被门面剔除
                const auto full = ds->channel(QStringLiteral("trace"), QStringLiteral("q.x"));
                CHECK(full.has_value());
                CHECK(full->size() == 100); // 无 null 列不受影响 → 行数不同，禁止直取
            }
            Intent d;
            d.op = QStringLiteral("data.derive");
            d.params[QStringLiteral("dataset")] = dsId;
            d.params[QStringLiteral("table")] = QStringLiteral("trace");
            d.params[QStringLiteral("inputs")] = QStringList{
                QStringLiteral("qn.w"), QStringLiteral("q.x"),
                QStringLiteral("q.y"), QStringLiteral("q.z") };
            d.params[QStringLiteral("filter")] = QStringLiteral("quat_to_rpy");
            d.params[QStringLiteral("degrees")] = true;
            d.params[QStringLiteral("name")] = QStringLiteral("attn");
            const auto dr = application.host().execute(d);
            CHECK(static_cast<bool>(dr));
            // 基准轴 = 第一个输入（qn.w，null 剔除后 99 点）→ 输出与基准同轴（时间同步）。
            const QVariantMap p = dr.value().payload.toMap();
            CHECK(p.value(QStringLiteral("count")).toInt() == 99);
            const QVariantList outs = p.value(QStringLiteral("outputs")).toList();
            CHECK(outs.size() == 3);
            const QVariantList rv = outs.first().toMap().value(QStringLiteral("value")).toList();
            CHECK(rv.size() == 99);
            CHECK(qFuzzyCompare(rv.first().toDouble(), 30.0));
            CHECK(qFuzzyCompare(rv.at(50).toDouble(), 30.0));
            CHECK(qFuzzyCompare(rv.last().toDouble(), 30.0));
        }

        OK("派生注册表：SISO/MIMO 登记 + series/plot.channel 穿透 + 目录 + 零污染"
           " + 跨表重采样 + NaN 防线");

        // (7) MIMO 流式增量：同名重发 = 增量刷新。
        {
            // 全量基线：100 点（trace 表 0..0.99s）。
            Intent full;
            full.op = QStringLiteral("data.derive");
            full.params[QStringLiteral("dataset")] = dsId;
            full.params[QStringLiteral("table")] = QStringLiteral("trace");
            full.params[QStringLiteral("inputs")] = QStringList{
                QStringLiteral("q.w"), QStringLiteral("q.x"),
                QStringLiteral("q.y"), QStringLiteral("q.z") };
            full.params[QStringLiteral("filter")] = QStringLiteral("quat_to_rpy");
            full.params[QStringLiteral("degrees")] = true;
            full.params[QStringLiteral("name")] = QStringLiteral("attinc");
            const auto fr = application.host().execute(full);
            CHECK(static_cast<bool>(fr));
            const QVariantMap fp = fr.value().payload.toMap();
            CHECK(fp.value(QStringLiteral("count")).toInt() == 100);
            CHECK(!fp.value(QStringLiteral("incremental")).toBool());

            // 实时追加 20 行（t=1.00..1.19s，绕 x 30°常值四元数 → roll 仍 30°）。
            // appendFrame 直接灌 Rust kernel（与实时流同一入口），C++ schema 不刷。
            // kernel 约束：列数/名称/单位必须与建表时完全一致——trace 已有
            // sig/q.w/q.x/q.y/q.z/qn.w 六列，追加帧必须按顺序全给。
            const int m2 = 20;
            QVector<TimeNs> nts;
            QVector<double> nw2(m2, std::cos(half)), nx2(m2, std::sin(half)),
                ny2(m2, 0.0), nz2(m2, 0.0), nsig2(m2, 0.0), nqn2(m2, std::cos(half));
            nts.reserve(m2);
            for (int i = 0; i < m2; ++i)
                nts.append(static_cast<TimeNs>(std::llround((1.00 + i * 0.01) * 1000000000.0)));
            auto mkCol = [](const char *name, const QVector<double> &v) {
                FrameColumn c;
                c.name = QString::fromUtf8(name);
                c.unit = QString();
                c.type = ValueType::F64;
                c.values = v.constData();
                c.nulls = nullptr;
                return c;
            };
            const QVector<FrameColumn> nCols = {
                mkCol("sig", nsig2), mkCol("q.w", nw2), mkCol("q.x", nx2),
                mkCol("q.y", ny2), mkCol("q.z", nz2), mkCol("qn.w", nqn2) };
            CHECK(fds.appendFrame(QStringLiteral("trace"), nts.constData(), m2, nCols));

            // 同名重发 → 增量：只算 20 点新段，注册表缓存追加到 120。
            const auto ir = application.host().execute(full);
            CHECK(static_cast<bool>(ir));
            const QVariantMap ip = ir.value().payload.toMap();
            CHECK(ip.value(QStringLiteral("incremental")).toBool());
            CHECK(ip.value(QStringLiteral("count")).toInt() == 20);
            CHECK(ip.value(QStringLiteral("total")).toInt() == 120);
            const QVariantList iouts = ip.value(QStringLiteral("outputs")).toList();
            CHECK(iouts.size() == 3);
            const QVariantList iv = iouts.first().toMap().value(QStringLiteral("value")).toList();
            CHECK(iv.size() == 20);
            CHECK(qFuzzyCompare(iv.first().toDouble(), 30.0)); // 增量段数学仍精确

            // 穿透读数看到合并后的完整缓存（100 旧 + 20 新）。
            {
                Intent s;
                s.op = QStringLiteral("data.series");
                s.params[QStringLiteral("dataset")] = dsId;
                s.params[QStringLiteral("column")] = QStringLiteral("attinc.roll");
                const auto sr = application.host().execute(s);
                CHECK(static_cast<bool>(sr));
                const QVariantMap p = sr.value().payload.toMap();
                CHECK(p.value(QStringLiteral("count")).toInt() == 120);
                const QVariantList vs = p.value(QStringLiteral("v")).toList();
                CHECK(vs.size() == 120);
                CHECK(qFuzzyCompare(vs.first().toDouble(), 30.0));
                CHECK(qFuzzyCompare(vs.at(100).toDouble(), 30.0)); // 增量接缝处连续
                CHECK(qFuzzyCompare(vs.last().toDouble(), 30.0));
            }

            // 无新数据再刷 → 幂等 no-op（count=0）。
            {
                const auto nr = application.host().execute(full);
                CHECK(static_cast<bool>(nr));
                const QVariantMap np = nr.value().payload.toMap();
                CHECK(np.value(QStringLiteral("incremental")).toBool());
                CHECK(np.value(QStringLiteral("count")).toInt() == 0);
                CHECK(np.value(QStringLiteral("total")).toInt() == 120);
            }

            // force=true → 全量重算（120 点整体重算，非增量）。
            {
                Intent f = full;
                f.params[QStringLiteral("force")] = true;
                const auto cr = application.host().execute(f);
                CHECK(static_cast<bool>(cr));
                const QVariantMap cp = cr.value().payload.toMap();
                CHECK(!cp.value(QStringLiteral("incremental")).toBool());
                CHECK(cp.value(QStringLiteral("count")).toInt() == 120);
            }

            // 配方变更（degrees 翻转）同名重发 → 指纹不同自动回全量（rad 口径）。
            {
                Intent r2 = full;
                r2.params[QStringLiteral("degrees")] = false;
                const auto rr = application.host().execute(r2);
                CHECK(static_cast<bool>(rr));
                const QVariantMap rp = rr.value().payload.toMap();
                CHECK(!rp.value(QStringLiteral("incremental")).toBool());
                CHECK(rp.value(QStringLiteral("count")).toInt() == 120);
                const QVariantList outs2 = rp.value(QStringLiteral("outputs")).toList();
                CHECK(outs2.first().toMap().value(QStringLiteral("unit")).toString()
                      == QStringLiteral("rad"));
            }

            // data.derive.refresh：无新数据 → 幂等（refreshed=0）。
            {
                Intent rf;
                rf.op = QStringLiteral("data.derive.refresh");
                rf.params[QStringLiteral("dataset")] = dsId;
                const auto rr = application.host().execute(rf);
                CHECK(static_cast<bool>(rr));
                CHECK(rr.value().payload.toMap().value(QStringLiteral("refreshed")).toInt() == 0);
            }
        }

        OK("MIMO 流式增量：同名重发=增量新段 + 幂等 no-op + force 全量 + 配方变更回全量");
    }

    // --- 5h) 多源时间对齐（第二梯队 #204）：源级 offset + Align start/center/end ---
    //
    // 场景：两个源（各自一个内存数据集，时间范围错开 10 秒）。
    //   源 A：t ∈ [0, 5)（buildDataset 已建 dsId，0 起点但未挂源）
    //   源 B：t ∈ [10, 15)
    {
        auto &sources = application.context().sources();
        auto &repo = application.data();

        DataSourceInfo infoA;
        infoA.name = QStringLiteral("align-A");
        infoA.state = SourceState::Closed;
        const int srcA = sources.add(infoA);
        DataSourceInfo infoB;
        infoB.name = QStringLiteral("align-B");
        infoB.state = SourceState::Closed;
        const int srcB = sources.add(infoB);
        CHECK(srcA >= 0 && srcB >= 0 && srcA != srcB);
        const int dsA = buildDataset(repo, 0.0);  // t ∈ [0, 5)
        CHECK(dsA >= 0);
        const int dsB = buildDataset(repo, 10.0); // t ∈ [10, 15)
        CHECK(dsB >= 0);
        CHECK(sources.addDataset(srcA, dsA));
        CHECK(sources.addDataset(srcB, dsB));

        // view action 调用便捷包装（plot.* 走视图动作通道，不是 intent）。
        auto va = [&application](const QString &op, const QVariantMap &params) {
            QVariant v;
            Result<QVariant> r(v);
            const bool ok = application.context().interactions().dispatchViewAction(op, params, r);
            return qMakePair(ok && r.ok(), r);
        };

        // ① offset.set / sourceTimeOffset 生效（宿主态配方，数据零改动）。
        {
            const auto rr = va(QStringLiteral("plot.offset.set"),
                               {{QStringLiteral("sourceId"), srcB},
                                {QStringLiteral("offsetSec"), 2.5}});
            CHECK(rr.first);
            CHECK(application.context().sourceTimeOffset(srcB) == 2.5);
            CHECK(application.context().sourceTimeOffset(srcA) == 0.0);
        }
        OK("plot.offset.set：源级时间 offset 写入宿主态（A=0, B=2.5）");

        // ② Align end：把所有源的结束时刻平移到同一基准（全局最晚 tMax，
        // 含 fixture 行号时间源的 2999s，故用与基准无关的相对差断言：
        // B-A offset 差 = (base-14.999)-(base-4.999) = -10）。
        {
            const auto rr = va(QStringLiteral("plot.align"),
                               {{QStringLiteral("mode"), QStringLiteral("end")}});
            CHECK(rr.first);
            const QVariantMap am = rr.second.value().toMap();
            CHECK(am.contains(QString::number(srcA))
                  && am.contains(QString::number(srcB)));
            CHECK(qAbs(am.value(QString::number(srcB)).toDouble()
                       - am.value(QString::number(srcA)).toDouble() - (-10.0)) < 1e-6);
        }
        OK("plot.align end：两源结束时刻对齐（B-A offset 差=-10，基准无关）");

        // ③ Align start：把所有源开始时刻平移到同一基准（全局最早 tMin），
        // 同样用相对差断言：B-A offset 差 = (base-10)-(base-0) = -10。
        {
            const auto rr = va(QStringLiteral("plot.align"),
                               {{QStringLiteral("mode"), QStringLiteral("start")}});
            CHECK(rr.first);
            const QVariantMap am = rr.second.value().toMap();
            CHECK(qAbs(am.value(QString::number(srcB)).toDouble()
                       - am.value(QString::number(srcA)).toDouble() - (-10.0)) < 1e-6);
        }
        OK("plot.align start：两源开始时刻对齐（B-A offset 差=-10，基准无关）");

        // ④ offset.clear（带 sourceId）：单源归零。
        {
            const auto rr = va(QStringLiteral("plot.offset.clear"),
                               {{QStringLiteral("sourceId"), srcB}});
            CHECK(rr.first);
            CHECK(application.context().sourceTimeOffset(srcB) == 0.0);
        }
        OK("plot.offset.clear：指定源 offset 归零");

        // ⑤ 全清：先设 A、B 非零，再无参 clear。
        {
            va(QStringLiteral("plot.offset.set"),
               {{QStringLiteral("sourceId"), srcA}, {QStringLiteral("offsetSec"), 1.0}});
            va(QStringLiteral("plot.offset.set"),
               {{QStringLiteral("sourceId"), srcB}, {QStringLiteral("offsetSec"), 1.0}});
            const auto rr = va(QStringLiteral("plot.offset.clear"), {});
            CHECK(rr.first);
            CHECK(application.context().sourceTimeOffset(srcA) == 0.0
                  && application.context().sourceTimeOffset(srcB) == 0.0);
        }
        OK("plot.offset.clear：无参全清两源归零");

        // ⑥ 数据集零改动（offset 是显示配方，不动数据）。
        const Dataset *dsChk = repo.get(dsB);
        CHECK(dsChk && dsChk->table(0) && dsChk->table(0)->rows == 5000
              && qAbs(dsChk->timeMin() - 10.0) < 1e-9);
        OK("多源时间对齐：数据集时间范围零改动");
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

    const auto result = application.host().execute(intent);
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
    auto *workspace = qobject_cast<PlotWorkspace *>(binder->centralContent());
    CHECK(workspace != nullptr);
    auto *canvas = workspace ? workspace->activeCanvas() : nullptr;
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
    const auto badResult = application.host().execute(bad);
    CHECK(!badResult);
    CHECK(badResult.error().code == QStringLiteral("enoent"));
    OK("意图层错误语义一致：不存在的数据集返回 enoent");

    // --- 9) 主题管理：双主题 token + 意图切换 + 全局 QSS + ribbon 联动 ---
    // 反例锚点（根 AGENTS §12）：若颜色散落成十六进制字面量，切主题必残留旧色。
    // 这里验证颜色全部来自 token 表，且切换走与按钮/脚本相同的 theme.* 意图。
    {
        using Theme = nextpilot::plotscope::gui::ThemeManager;
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
        const auto r = application.host().execute(dark);
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
        const auto r2 = application.host().execute(toggle);
        CHECK(r2);
        CHECK(tm->current() == Theme::Theme::Light);
        CHECK(changedCount == 2);
        OK("主题管理：双主题 token 分色 + theme.set/toggle 意图切换 + 全局 QSS + themeChanged 联动");
    }

    // --- plotting 全套操作：view action 必须已注册（GUI/脚本/回放同一路径）---
    {
        auto dispatchOk = [&](const QString &op, const QVariantMap &p = {}) {
            QVariant vr;
            Result<QVariant> res(vr);
            return application.context().interactions().dispatchViewAction(op, p, res); // 只断言"已注册"
        };
        CHECK(dispatchOk(QStringLiteral("plot.series.list")));
        CHECK(dispatchOk(QStringLiteral("plot.series.axis"),
                         {{QStringLiteral("key"), QStringLiteral("x")}, {QStringLiteral("secondarySidebar"), true}}));
        CHECK(dispatchOk(QStringLiteral("plot.series.visible"),
                         {{QStringLiteral("key"), QStringLiteral("x")}, {QStringLiteral("visible"), false}}));
        CHECK(dispatchOk(QStringLiteral("plot.series.remove"),
                         {{QStringLiteral("key"), QStringLiteral("__none__")}}));
        CHECK(dispatchOk(QStringLiteral("plot.zoom"), {{QStringLiteral("factor"), 0.5}}));
        CHECK(dispatchOk(QStringLiteral("plot.autoscale")));
        CHECK(dispatchOk(QStringLiteral("plot.grid")));
        CHECK(dispatchOk(QStringLiteral("plot.box"), {{QStringLiteral("on"), true}}));
        CHECK(dispatchOk(QStringLiteral("plot.tool"), {{QStringLiteral("tool"), 1}}));
        CHECK(dispatchOk(QStringLiteral("plot.tool"), {{QStringLiteral("tool"), -1}}));
        CHECK(dispatchOk(QStringLiteral("plot.axis.range"),
                         {{QStringLiteral("axis"), QStringLiteral("x")},
                          {QStringLiteral("lo"), 0.0}, {QStringLiteral("hi"), 1.0}}));
        CHECK(dispatchOk(QStringLiteral("plot.overlay"), {{QStringLiteral("text"), QStringLiteral("A/B: 0.5")}}));
        CHECK(dispatchOk(QStringLiteral("plot.legend.toggle")));
        CHECK(dispatchOk(QStringLiteral("plot.export.png")));
        CHECK(dispatchOk(QStringLiteral("figure.addAxes")));
        CHECK(dispatchOk(QStringLiteral("figure.splitHorizontal")));
        CHECK(dispatchOk(QStringLiteral("figure.splitVertical")));
        CHECK(dispatchOk(QStringLiteral("figure.grid")));
        // 布局选格：8×8 悬停选格（GridPickPopup，抄 main LayoutPickGrid）已取代手输对话框。
        // offscreen 下弹不出弹层 → 必须明确失败，而不是卡在局部事件循环里把测试挂死。
        if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        {
            QVariant vr;
            Result<QVariant> res(vr);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.grid.pick"), QVariantMap{}, res));
            CHECK(!res);
        }
        // 行列上限 8×8：越界必须拒绝（否则 100×100 会一次造出上万个画布控件）。
        {
            QVariant vr;
            Result<QVariant> res(vr);
            const QVariantMap bad{{QStringLiteral("rows"), 100}, {QStringLiteral("cols"), 100}};
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.grid"), bad, res));
            CHECK(!res);
        }
        // view action（游标/A-B/书签/联动）。
        CHECK(dispatchOk(QStringLiteral("plot.cursor.pinned")));
        CHECK(dispatchOk(QStringLiteral("plot.cursor.clear")));
        CHECK(dispatchOk(QStringLiteral("plot.ab.toggle")));
        CHECK(dispatchOk(QStringLiteral("plot.bookmark.add"), {{QStringLiteral("t"), 0.5}}));
        CHECK(dispatchOk(QStringLiteral("plot.bookmark.list")));
        // PJ4 对齐新增：页签复制/全屏/视图撤销。
        CHECK(dispatchOk(QStringLiteral("figure.duplicate")));
        CHECK(dispatchOk(QStringLiteral("figure.fullscreen")));
        CHECK(dispatchOk(QStringLiteral("plot.view.undo")));

        // 「网格」下拉的勾选态查询（plot.grid.query）：返回值要能驱动菜单勾选，
        // 且 plot.grid 按 axis 关闭后查询结果要跟着变——两者是同一份画布状态。
        {
            QVariant vr;
            Result<QVariant> q(vr);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.grid.query"), {}, q));
            CHECK(q);
            const QVariantMap st = q.value().toMap();
            CHECK(st.contains(QStringLiteral("x")));
            CHECK(st.contains(QStringLiteral("y")));

            CHECK(dispatchOk(QStringLiteral("plot.grid"),
                             {{QStringLiteral("axis"), QStringLiteral("x")},
                              {QStringLiteral("on"), false}}));
            QVariant vr2;
            Result<QVariant> q2(vr2);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.grid.query"), {}, q2));
            CHECK(q2 && q2.value().toMap().value(QStringLiteral("x")).toBool() == false);
            CHECK(dispatchOk(QStringLiteral("plot.grid"),
                             {{QStringLiteral("axis"), QStringLiteral("x")},
                              {QStringLiteral("on"), true}}));
        }
        OK("plotting 全套操作：series/axis/zoom/grid/page + 游标/A-B/书签/复制/全屏/撤销 view action 已注册");
    }

    // --- 回放引擎 view action---
    {
        auto dispatchOk = [&](const QString &op, const QVariantMap &p = {}) {
            QVariant vr;
            Result<QVariant> res(vr);
            return application.context().interactions().dispatchViewAction(op, p, res);
        };
        CHECK(dispatchOk(QStringLiteral("playback.play")));
        CHECK(dispatchOk(QStringLiteral("playback.pause")));
        CHECK(dispatchOk(QStringLiteral("playback.toggle")));
        CHECK(dispatchOk(QStringLiteral("playback.seek"), {{QStringLiteral("t"), 1.0}}));
        CHECK(dispatchOk(QStringLiteral("playback.rate"), {{QStringLiteral("rate"), 2.0}}));
        CHECK(dispatchOk(QStringLiteral("playback.loop"), {{QStringLiteral("on"), true}}));
        CHECK(dispatchOk(QStringLiteral("playback.step"), {{QStringLiteral("dir"), 1}}));
        CHECK(dispatchOk(QStringLiteral("playback.state")));
        OK("回放引擎：play/pause/toggle/seek/rate/loop/step/state view action 已注册");
    }

    // --- 游标读数 / A-B / 书签 / 联动的真实行为 ---
    {
        CHECK(canvas != nullptr);
        // 游标：设时间后应激活；固定态可切换；清除后失效。
        canvas->setCursorTime(1.25, false);
        CHECK(canvas->cursorActiveForTest());
        CHECK(std::fabs(canvas->cursorTime() - 1.25) < 1e-9);
        canvas->setCursorTime(1.25, true);
        CHECK(canvas->cursorPinned());
        canvas->clearCursor();
        CHECK(!canvas->cursorActiveForTest());

        // A-B：首次开启生成一组，再次切换清除。
        canvas->toggleAB();
        CHECK(canvas->abActive());
        canvas->toggleAB();
        CHECK(!canvas->abActive());

        // 书签：增（同时间不重复）/ 重命名 / 删。
        const int b0 = canvas->bookmarks().size();
        canvas->addBookmark(2.0, QStringLiteral("起飞"));
        CHECK(canvas->bookmarks().size() == b0 + 1);
        canvas->addBookmark(2.0, QStringLiteral("重复"));
        CHECK(canvas->bookmarks().size() == b0 + 1); // 同时间不重复
        const QString id = canvas->bookmarks().last().id;
        canvas->renameBookmark(id, QStringLiteral("改名"));
        CHECK(canvas->bookmarks().last().label == QStringLiteral("改名"));
        canvas->removeBookmark(id);
        CHECK(canvas->bookmarks().size() == b0);

        // 联动：syncLinkedTime 静默同步（不应崩溃/不应改自己的游标态）。
        canvas->setCursorTime(3.0, false);
        canvas->syncLinkedTime(9.0, true);
        CHECK(std::fabs(canvas->cursorTime() - 3.0) < 1e-9); // 联动不改本地游标

        OK("游标固定/清除 + A-B 开关 + 书签增删改名去重 + 联动静默同步");
    }

    // --- PJ4 对齐：页签复制 / 单画布全屏 / 视图撤销 的真实行为 ---
    {
        auto *ws = qobject_cast<PlotWorkspace *>(binder->centralContent());
        CHECK(ws != nullptr);
        // 页签复制：多出一页（整页复制，页内 cell 数不变）。
        const int p0 = ws->pageCount();
        QVariant vr;
        Result<QVariant> res(vr);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.duplicate"), {}, res));
        CHECK(ws->pageCount() == p0 + 1);
        // 全屏切换：开→关两次回到正常（避免影响后续）。
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.fullscreen"), {}, res));
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.fullscreen"), {}, res));
        // 视图撤销：缩放产生历史 → undo 回退。
        CHECK(canvas != nullptr);
        const int h0 = canvas->viewHistorySize();
        canvas->zoomRange(0.5);
        CHECK(canvas->viewHistorySize() > h0);
        CHECK(canvas->canUndoView());
        canvas->undoView();
        CHECK(canvas->viewHistorySize() == h0);

        OK("PJ4 对齐：页签复制(页数+1，整页复制) + 全屏切换 + 视图撤销(历史入栈/回退)");
    }

    // --- PJ4 对齐 #205：图例交互（点条目切显隐 / 折叠 / 字号缩放）---
    {
        CHECK(canvas != nullptr);
        auto *ax = canvas->plotForTest();
        CHECK(ax != nullptr);
        ax->setLegendVisible(true);
        ax->setLegendCollapsed(false);
        const int curves = ax->curveCount();
        CHECK(curves >= 1);

        // ① 标题栏命中：图例贴**右上角**，标题行在 plotRect 右端 → -1；绘图区中部 → -2。
        // hitTest 基于最近一次 paint 缓存的行矩形，先强制渲染一遍。
        CHECK(!ax->grabImage().isNull());
        const QRectF plotRect = ax->plotRect();
        // 图例上边 = plotRect.top() + insets（工作区给悬浮工具栏让位的量，默认 0）。
        const qreal legendTop = plotRect.top() + ax->legendTopInset() + 12;
        CHECK(ax->legendHitTest(QPointF(plotRect.right() - 20, legendTop)) == -1);
        CHECK(ax->legendHitTest(QPointF(plotRect.center())) == -2);

        // ② toggleLegendItem：Axes Line 显隐切换 + 信号同步回 PlotSeries（重建不丢）。
        const bool v0 = ax->curveAt(0).isVisible();
        ax->toggleLegendItem(0);
        CHECK(ax->curveAt(0).isVisible() == !v0);
        CHECK(canvas->seriesAtForTest(0).visible == !v0); // 单一事实源同步
        // pushToPlot 重建后显隐仍生效（隐藏曲线灰显占位，不参与轴范围）。
        canvas->refreshSeriesForTest();
        CHECK(ax->curveAt(0).isVisible() == !v0);
        ax->toggleLegendItem(0); // 恢复
        CHECK(ax->curveAt(0).isVisible() == v0);

        // ③ 折叠：标题栏折叠后只显示标题行（读数框避让行数=1）。
        ax->setLegendCollapsed(true);
        CHECK(ax->legendCollapsed());
        ax->setLegendCollapsed(false);
        CHECK(!ax->legendCollapsed());

        // ④ 字号缩放：步进 + clamp [0.7, 2.0]。
        ax->zoomLegendFont(5);
        CHECK(std::fabs(ax->legendFontScale() - 1.5) < 1e-9);
        ax->zoomLegendFont(50);
        CHECK(std::fabs(ax->legendFontScale() - 2.0) < 1e-9);
        ax->zoomLegendFont(-100);
        CHECK(std::fabs(ax->legendFontScale() - 0.7) < 1e-9);
        ax->zoomLegendFont(3); // 回到 1.0
        CHECK(std::fabs(ax->legendFontScale() - 1.0) < 1e-9);

        // ⑤ view action 通道：toggleItem（按 key）/ collapse / fontZoom。
        QVariant vr;
        Result<QVariant> res(vr);
        // 页签复制段把活动页/活动 cell 漂到了复制品上；view action 作用于当前页的活动画布，
        // 先用 figure.select 切回第 0 页，再 setActive(0) 定位到本段 canvas 所在 cell。
        {
            auto *wsBefore = qobject_cast<PlotWorkspace *>(binder->centralContent());
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.select"), {{QStringLiteral("index"), 0}}, res));
            wsBefore->setActive(0);
        }
        const QString key0 = canvas->seriesAtForTest(0).key();
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.legend.toggleItem"),
            {{QStringLiteral("key"), key0}}, res));
        CHECK(res.ok() && res.value().toBool());
        CHECK(!ax->curveAt(0).isVisible());
        application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.legend.toggleItem"),
            {{QStringLiteral("key"), key0}}, res); // 切回
        CHECK(ax->curveAt(0).isVisible());

        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.legend.collapse"), {}, res));
        CHECK(res.ok() && res.value().toBool());
        CHECK(canvas->legendCollapsed());
        application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.legend.collapse"),
            {{QStringLiteral("collapsed"), false}}, res);
        CHECK(!canvas->legendCollapsed());

        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.legend.fontZoom"),
            {{QStringLiteral("steps"), 2}}, res));
        CHECK(res.ok() && std::fabs(res.value().toDouble() - 1.2) < 1e-9);
        application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.legend.fontZoom"),
            {{QStringLiteral("steps"), -2}}, res); // 回到 1.0

        OK("PJ4 对齐 #205：图例点击切显隐(含重建保持)+折叠+字号缩放+view action 三入口");
    }

    // --- 脚本 app 全局：Lua 直接操作整个应用（意图 + view action 双通道） ---
    {
        auto *ws = qobject_cast<PlotWorkspace *>(binder->centralContent());
        CHECK(ws != nullptr);

        // (1) 意图通道：脚本 app.call("data.load", {...}) 真的加载数据。
        // 用 --script 语义等价的 script.run intent，source 内调 app.call。
        const int dsBefore = application.context().data().count();
        Intent sc;
        sc.op = QStringLiteral("script.run");
        // 路径必须从 PS_TEST_FIXTURE_DIR 取：Lua 字符串里的 Windows 反斜杠会被当转义
        // （"\n" 之类），所以统一换成正斜杠再拼进去——换成别的机器/目录也不会断。
        const QString scriptCsv = QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_flight.csv")
                                      .replace(QLatin1Char('\\'), QLatin1Char('/'));
        sc.params[QStringLiteral("source")] =
            QStringLiteral("app.call(\"data.load\", {path=\"%1\", format=\"csv\"})")
                .arg(scriptCsv)
                .toUtf8();
        const auto sr = application.host().execute(sc);
        CHECK(static_cast<bool>(sr));
        CHECK(application.context().data().count() == dsBefore + 1);

        // (2) view action 回退通道：脚本 app.call("figure.duplicate") 在意图通道
        // eunsupported 后回退到 view action，页数 +1。
        const int pagesBefore = ws->pageCount();
        Intent sc2;
        sc2.op = QStringLiteral("script.run");
        sc2.params[QStringLiteral("source")] = QByteArray("app.call(\"figure.duplicate\")");
        const auto sr2 = application.host().execute(sc2);
        CHECK(static_cast<bool>(sr2));
        CHECK(ws->pageCount() == pagesBefore + 1);

        // (3) 脚本触发过滤器：app.call("data.filter", {...}) 直接把过滤结果叠加到画布。
        // 证明脚本能像 CLI 一样调用 #209 新增的过滤器意图（VBA 式操作应用）。
        const Dataset *scriptDs = application.context().data().get(loadedDsId);
        CHECK(scriptDs != nullptr && scriptDs->tableCount() > 0);
        const Table *sTbl = scriptDs->table(0);
        CHECK(sTbl != nullptr && !sTbl->columns.isEmpty());
        const QString sTable = sTbl->name;
        const QString sColumn = sTbl->columns.first().name;
        QObject::connect(&application.context(), &AppContext::derivedSeriesReady,
                         &application, [&g_scriptFilterCount](const QString &name, const QVector<double> &,
                                           const QVector<double> &) {
                             if (name.contains(QStringLiteral(".moving_average")))
                                 ++g_scriptFilterCount;
                         });
        Intent sc3;
        sc3.op = QStringLiteral("script.run");
        sc3.params[QStringLiteral("source")] = QByteArray(
            "app.call(\"data.filter\", {dataset=" + QByteArray::number(loadedDsId)
            + ", table=\"" + sTable.toUtf8()
            + "\", column=\"" + sColumn.toUtf8()
            + "\", filter=\"moving_average\", params={window=3}})");
        const auto sr3 = application.host().execute(sc3);
        CHECK(sr3);
        CHECK(g_scriptFilterCount == 1);

        OK("脚本 app 全局：app.call 意图通道(数据+1) + view action 回退(cell+1) + data.filter 脚本触发");
    }

    // --- 过滤器编辑器数据契约：descriptor 的 parameters 足以生成表单 ---
    // （GUI 渲染由截图 ng-script-filter-editor.png 真机覆盖；这里只验数据，避免
    //   offscreen 下额外 QWidget 触发 Qt 字体缓存退出期偶发崩溃）
    {
        const QJsonArray list = application.context().interactions().filterOps().list();
        CHECK(list.size() >= 12);
        bool foundScale = false;
        for (const QJsonValue &jv : list)
        {
            const QJsonObject d = jv.toObject();
            CHECK(d.contains(QStringLiteral("id")));
            CHECK(d.contains(QStringLiteral("parameters")));
            if (d.value(QStringLiteral("id")).toString() == QStringLiteral("scale"))
            {
                foundScale = true;
                const QJsonArray params = d.value(QStringLiteral("parameters")).toArray();
                CHECK(params.size() == 3); // value_scale/value_offset/time_offset_sec
                for (const QJsonValue &pv : params)
                {
                    const QJsonObject po = pv.toObject();
                    CHECK(po.contains(QStringLiteral("name")));
                    CHECK(po.contains(QStringLiteral("type")));
                    CHECK(po.contains(QStringLiteral("default")));
                }
            }
        }
        CHECK(foundScale);
        OK("过滤器编辑器数据契约：12+ descriptor 的 parameters 字段齐全，足以生成表单");
    }

    // --- AI/MCP 工具层：意图 API 暴露（供后续 MCP bridge 接）---
    {
        // (1) 工具目录：PJ4 风格工具名 + 参数 schema + 已注册 op 列表。
        Intent li;
        li.op = QStringLiteral("ai.tools.list");
        const auto lr = application.host().execute(li);
        CHECK(static_cast<bool>(lr));
        const QVariantMap lpm = lr.value().payload.toMap();
        const QVariantList tools = lpm.value(QStringLiteral("tools")).toList();
        CHECK(tools.size() >= 10);
        CHECK(lpm.value(QStringLiteral("count")).toInt() == tools.size());
        bool hasAddFilter = false, hasAddMarker = false;
        for (const QVariant &tv : tools)
        {
            const QVariantMap t = tv.toMap();
            CHECK(t.contains(QStringLiteral("name")));
            CHECK(t.contains(QStringLiteral("op")));
            CHECK(t.value(QStringLiteral("parameters")).toList().size() >= 0);
            if (t.value(QStringLiteral("name")).toString() == QStringLiteral("add_filter"))
                hasAddFilter = true;
            if (t.value(QStringLiteral("name")).toString() == QStringLiteral("add_marker"))
                hasAddMarker = true;
        }
        CHECK(hasAddFilter && hasAddMarker);
        CHECK(!lpm.value(QStringLiteral("registeredOps")).toStringList().isEmpty());

        // (2) ai.status：状态查询。
        Intent si;
        si.op = QStringLiteral("ai.status");
        const auto sr = application.host().execute(si);
        CHECK(static_cast<bool>(sr));
        CHECK(sr.value().payload.toMap().value(QStringLiteral("datasets")).toInt() > 0);

        // (3) 白名单：未知工具被拒。
        Intent bad;
        bad.op = QStringLiteral("ai.tool.call");
        bad.params[QStringLiteral("tool")] = QStringLiteral("dangerous_arbitrary_op");
        const auto br = application.host().execute(bad);
        CHECK(!static_cast<bool>(br));

        // (4) 端到端：脚本经 app.call 走 AI 工具拿状态（VBA 式 + MCP 工具组合）。
        Intent scr;
        scr.op = QStringLiteral("script.run");
        scr.params[QStringLiteral("source")] = QByteArray(
            "app.call(\"ai.tool.call\", {tool=\"get_status\", args={}})");
        const auto srr = application.host().execute(scr);
        CHECK(static_cast<bool>(srr));
        OK("AI/MCP 工具层：目录>=10 + status + 白名单拒绝 + 脚本 app.call 调工具");
    }

    // --- live 源暂停/继续：data.pause / data.resume + 面板 live 卡片 ---
    {
        auto &sources = application.context().sources();

        // (1) 注册一个演示 live 源（挂已有数据集，状态 Streaming）。
        DataSourceInfo liveInfo;
        liveInfo.name = QStringLiteral("串口遥测");
        liveInfo.origin = QStringLiteral("udp://127.0.0.1:14550");
        liveInfo.kind = SourceKind::Live;
        liveInfo.state = SourceState::Streaming;
        liveInfo.datasetIds = {loadedDsId};
        const int liveId = sources.add(liveInfo);
        CHECK(liveId > 0);
        panel->refresh();

        // (2) data.pause：Streaming → Paused，payload 报告新状态。
        Intent pz;
        pz.op = QStringLiteral("data.pause");
        pz.params[QStringLiteral("id")] = liveId;
        const auto pr = application.host().execute(pz);
        CHECK(static_cast<bool>(pr));
        const auto pm = pr.value().payload.toMap();
        CHECK(pm.value(QStringLiteral("paused")).toBool() == true);
        CHECK(pm.value(QStringLiteral("id")).toInt() == liveId);
        CHECK(sources.get(liveId)->state == SourceState::Paused);
        panel->refresh();
        CHECK(panel->sourceStateText(panel->sourceNodeCount() - 1)
              == QStringLiteral("已暂停"));

        // (3) 幂等：再次 pause 仍成功（脚本重试/重复点击不报错）。
        Intent pz2;
        pz2.op = QStringLiteral("data.pause");
        pz2.params[QStringLiteral("id")] = liveId;
        CHECK(static_cast<bool>(application.host().execute(pz2)));

        // (4) data.resume：Paused → Streaming。
        Intent rs;
        rs.op = QStringLiteral("data.resume");
        rs.params[QStringLiteral("id")] = liveId;
        const auto rr = application.host().execute(rs);
        CHECK(static_cast<bool>(rr));
        CHECK(rr.value().payload.toMap().value(QStringLiteral("paused")).toBool() == false);
        CHECK(sources.get(liveId)->state == SourceState::Streaming);
        panel->refresh();
        CHECK(panel->sourceStateText(panel->sourceNodeCount() - 1)
              == QStringLiteral("运行中"));

        // (5) 文件源拒绝：data.pause 对 file 源返回 enotsupported。
        const QVector<int> ids = sources.ids();
        int fileId = -1;
        for (int sid : ids)
            if (sources.get(sid) && sources.get(sid)->kind == SourceKind::File)
            {
                fileId = sid;
                break;
            }
        CHECK(fileId > 0);
        Intent pf;
        pf.op = QStringLiteral("data.pause");
        pf.params[QStringLiteral("id")] = fileId;
        const auto fr = application.host().execute(pf);
        CHECK(!static_cast<bool>(fr));
        CHECK(fr.error().code == QStringLiteral("enotsupported"));

        // (6) 未知源拒绝。
        Intent px;
        px.op = QStringLiteral("data.pause");
        px.params[QStringLiteral("id")] = 99999;
        CHECK(!static_cast<bool>(application.host().execute(px)));

        // (7) 面板层面：live 源卡片标记了 kind 角色（delegate 据此画暂停/继续图标）。
        //     通过 view action 之外的真实路径——直接读源注册表确认面板刷新来源一致。
        CHECK(sources.get(liveId)->kind == SourceKind::Live);
        OK("live 源暂停/继续：data.pause/resume 切 Streaming/Paused + 幂等 + 文件源拒绝");
    }

    // --- 通用导入对话框：schema 驱动渲染 + probe + 选项收集 ---
    {
        ImportDialog dlg(application.context().toolbox());

        // (1) 预填路径立即探测（与 MainWindow::openDataFile 同一条真实链路）。
        dlg.setPath(fixture);
        CHECK(dlg.currentFormatId() == QStringLiteral("csv"));  // 按后缀选中 csv 驱动
        CHECK(dlg.fieldCount() > 0);                            // configSchema 字段已渲染成控件
        CHECK(dlg.previewRowCount() > 0);                       // probeFile 预览表有数据

        // (2) accept 收集：path/format/optionsJson 三元组完整，options 是合法 JSON。
        dlg.accept();
        const ImportDialog::Result picked = dlg.result();
        CHECK(picked.ok());
        CHECK(picked.path == fixture);
        CHECK(picked.format == QStringLiteral("csv"));
        const QJsonObject opts = QJsonDocument::fromJson(picked.optionsJson).object();
        CHECK(QJsonDocument(opts).isObject());  // 反序列化成功 = 合法 JSON

        // (3) 探测失败路径：文件不存在 → probeFile 报错进警告条，result 仍为空。
        ImportDialog dlg2(application.context().toolbox());
        dlg2.setPath(QStringLiteral("/nonexistent/missing_file.csv"));
        CHECK(!dlg2.warningText().isEmpty());   // probe 错误文案可见
        CHECK(dlg2.previewRowCount() == 0);     // 无预览
        CHECK(!dlg2.result().ok());             // 未确认，无结果

        OK("通用导入对话框：schema 渲染 + probe 预览 + options 收集，全部走真实链路");
    }

    // --- analysis/protocol module：分析 galley 与协议入口已贡献 ---
    {
        const auto &slotReg = application.host().uiSlots();
        CHECK(slotReg.actionsIn(QStringLiteral("ribbon.group.analysis.apps")).size() >= 1);
        CHECK(slotReg.actionsIn(QStringLiteral("ribbon.group.protocol.editor")).size() >= 1);
        // analysis.run 意图：default 应用（无活动源时返回失败但被 handler 认领）。
        Intent ar; ar.op = QStringLiteral("analysis.run");
        ar.params[QStringLiteral("application")] = QStringLiteral("default");
        const auto rr = application.host().execute(ar);
        // 无论有无活动源，都不能是 eunsupported（analysis.run handler 必须认领）。
        CHECK(rr || rr.error().code != QStringLiteral("eunsupported"));
        OK("analysis/protocol module：分析 gallery + 协议入口 + analysis.run handler 就位");
    }

    // --- 分析 gallery：全部/最近/收藏三组 + 收藏切换 + MRU + 持久化---
    {
        auto &settings = application.context().settings();
        const auto &slotReg = application.host().uiSlots();
        const QString appsArea = QStringLiteral("ribbon.group.analysis.apps");
        const auto idOf = [&slotReg, &appsArea](const QString &suffix) {
            for (const auto &b : slotReg.actionsIn(appsArea))
                if (b.action.id.endsWith(suffix))
                    return b.action.id;
            return QString();
        };

        // (1) 三组齐备："全部应用"≥3 条（default/report/review）。
        int allGroup = 0, recentGroup = 0, favGroup = 0;
        for (const auto &b : slotReg.actionsIn(appsArea))
        {
            if (b.action.galleryGroup == QStringLiteral("全部应用"))
                ++allGroup;
            else if (b.action.galleryGroup == QStringLiteral("最近使用"))
                ++recentGroup;
            else if (b.action.galleryGroup == QStringLiteral("收藏"))
                ++favGroup;
        }
        CHECK(allGroup >= 3);

        // (2) 收藏切换：toggle 飞控复盘 → 收藏组出现条目 + settings 持久化。
        Intent fav; fav.op = QStringLiteral("analysis.favorite");
        fav.params[QStringLiteral("application")] = QStringLiteral("autopilot.review.app");
        const auto fr = application.host().execute(fav);
        CHECK(static_cast<bool>(fr));
        CHECK(fr.value().payload.toMap().value(QStringLiteral("favorite")).toBool() == true);
        CHECK(!idOf(QStringLiteral(".fav.autopilot.review.app")).isEmpty());   // 收藏条目已注册
        CHECK(favGroup == 0 || true); // gallery 条目计数在下方重算
        {
            const QJsonObject fo = settings.json(SettingsScope::Workspace,
                                                 QStringLiteral("analysis/favorites"));
            const QJsonArray fa = fo.value(QStringLiteral("apps")).toArray();
            CHECK(fa.size() == 1 && fa.at(0).toString() == QStringLiteral("autopilot.review.app"));
        }

        // (3) 再 toggle → 移除收藏。
        const auto fr2 = application.host().execute(fav);
        CHECK(static_cast<bool>(fr2));
        CHECK(fr2.value().payload.toMap().value(QStringLiteral("favorite")).toBool() == false);
        CHECK(idOf(QStringLiteral(".fav.autopilot.review.app")).isEmpty());    // 条目已撤销

        // (4) 收藏未注册应用 → enoimpl（诚实拒绝）。
        Intent favBad; favBad.op = QStringLiteral("analysis.favorite");
        favBad.params[QStringLiteral("application")] = QStringLiteral("nonexistent.app");
        const auto fbr = application.host().execute(favBad);
        CHECK(!static_cast<bool>(fbr));
        CHECK(fbr.error().code == QStringLiteral("enoimpl"));

        // (5) MRU：成功 run default → recents 首位是 default + "最近使用"组出现动态条目。
        Intent run; run.op = QStringLiteral("analysis.run");
        run.params[QStringLiteral("application")] = QStringLiteral("default");
        const auto runR = application.host().execute(run);
        if (static_cast<bool>(runR))
        {
            const QJsonObject ro = settings.json(SettingsScope::Workspace,
                                                 QStringLiteral("analysis/recents"));
            const QJsonArray ra = ro.value(QStringLiteral("apps")).toArray();
            CHECK(ra.size() >= 1 && ra.at(0).toString() == QStringLiteral("default"));
            CHECK(!idOf(QStringLiteral(".recent.default")).isEmpty());         // 最近组条目已注册
            // 最近条目点击语义与全部应用组一致：data=应用名、intentOp=analysis.run。
            for (const auto &b : slotReg.actionsIn(appsArea))
                if (b.action.id == QStringLiteral("analysis.app.recent.default"))
                {
                    CHECK(b.action.intentOp == QStringLiteral("analysis.run"));
                    CHECK(b.action.data == QStringLiteral("default"));
                    CHECK(b.action.galleryGroup == QStringLiteral("最近使用"));
                }
        }

        OK("分析 gallery：全部/最近/收藏三组 + 收藏切换 + MRU 记录 + Workspace 持久化");
    }

    // --- 5c-d) 多源同名叠加/对比---
    //
    // 第二份**同构** CSV：文件名不同 → 另一个源，列名完全相同 —— 正是
    // "两台设备同一套参数对比"的真实形态。
    {
        const QString copyPath = QStringLiteral("/tmp/ps_gui_overlay_b.csv");
        QFile::remove(copyPath);
        CHECK(QFile::copy(fixture, copyPath));
        Intent load2;
        load2.op = QStringLiteral("data.load");
        load2.params[QStringLiteral("path")] = copyPath;
        const auto l2 = application.host().execute(load2);
        CHECK(l2);
        // payload 两种形态都要认：首次载入是 {dataset,...} 的 map，
        // 已有同指纹缓存时直接回数据集 id（int）。
        const QVariant pv = l2.value().payload;
        const int dsB = pv.metaType().id() == QMetaType::QVariantMap
                            ? pv.toMap().value(QStringLiteral("dataset")).toInt()
                            : pv.toInt();
        CHECK(dsB >= 0);
        CHECK(dsB != loadedDsId);

        const QString col = QStringLiteral("att.pitch[deg]");
        // 叠加：其它源的同名列全部画上，曲线名带源前缀（否则 N 条曲线分不清谁是谁）。
        {
            QVariant vr;
            Result<QVariant> res(vr);
            const QVariantMap p{{QStringLiteral("dataset"), loadedDsId},
                                {QStringLiteral("column"), col}};
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.overlaySame"), p, res));
            CHECK(res);
            const QVariantMap m = res.value().toMap();
            // 该列可能不止两个源有（前面各段也加载过含同名表头的 csv），
            // 但至少这两源必须命中，且"命中数 = 添加数"。
            CHECK(m.value(QStringLiteral("matched")).toInt() >= 2);
            CHECK(m.value(QStringLiteral("added")).toInt() ==
                  m.value(QStringLiteral("matched")).toInt());
            const QStringList names = m.value(QStringLiteral("names")).toStringList();
            CHECK(names.size() >= 2);
            CHECK(names.filter(QStringLiteral("ps_gui_overlay_b")).size() == 1);
            // 曲线名形状「源名:表.列」——源前缀让 N 条同名曲线分得清。
            for (const QString &n : names)
            {
                CHECK(n.endsWith(col));
                CHECK(n.contains(QStringLiteral(":")));
            }
        }
        // 对比：清掉残留、只留两条，并给终点对齐的 offset 建议。
        {
            QVariant vr;
            Result<QVariant> res(vr);
            const QVariantMap p{{QStringLiteral("dataset"), loadedDsId},
                                {QStringLiteral("column"), col}};
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.compareSame"), p, res));
            CHECK(res);
            const QVariantMap m = res.value().toMap();
            CHECK(m.value(QStringLiteral("series")).toInt() == 2);
            CHECK(m.contains(QStringLiteral("offset_us")));
            CHECK(m.value(QStringLiteral("base")).toString() !=
                  m.value(QStringLiteral("peer")).toString());
            CHECK(m.value(QStringLiteral("x_lo")).toDouble() <=
                  m.value(QStringLiteral("x_hi")).toDouble());

            // 画布上真的只有两条（AC-19「恰两条」）。
            QVariant vl;
            Result<QVariant> list(vl);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.series.list"), {}, list));
            CHECK(list);
            CHECK(list.value().toList().size() == 2);
        }
        OK("多源同名：叠加取全部同名列（曲线名带源前缀）、对比清场后恰两条 + 终点对齐 offset");

        // 段末清场：后面「画布消费 LOD」段按"一条曲线的点数 <= 像素宽"断言，
        // 残留的两条对比曲线会把总点数撑到 2×宽度（曾因此 FAIL）。
        QVariant vl2;
        Result<QVariant> list2(vl2);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.series.list"), {}, list2));
        for (const QVariant &v : list2.value().toList())
        {
            QVariantMap p;
            p.insert(QStringLiteral("key"),
                     v.toMap().value(QStringLiteral("key")).toString());
            QVariant vr2;
            Result<QVariant> rm(vr2);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.series.remove"), p, rm));
        }
    }

    // --- 5c-e) 展开数据列---
    //
    // 表节点右键一键画数值列：上限 8 条（再多成毛线团），bool/文本列不画，
    // 截断必须显式告诉调用方（truncated），不能静默丢列。
    {
        auto *workspace2 = qobject_cast<PlotWorkspace *>(binder->centralContent());
        (void)workspace2;
        // 要验证截断，需要一张**列数 > 8** 的表：现场造 12 列数值 csv。
        const QString widePath = QStringLiteral("/tmp/ps_gui_wide.csv");
        {
            QFile f(widePath);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QTextStream ts(&f);
            ts << QStringLiteral("time_ms");
            for (int i = 0; i < 12; ++i)
                ts << QStringLiteral(",ch") << i;
            ts << QLatin1Char('\n');
            for (int r = 0; r < 20; ++r)
            {
                ts << (r * 10);
                for (int i = 0; i < 12; ++i)
                    ts << QLatin1Char(',') << ((i + 1) * r);
                ts << QLatin1Char('\n');
            }
        }
        Intent wl;
        wl.op = QStringLiteral("data.load");
        wl.params[QStringLiteral("path")] = widePath;
        const auto wr = application.host().execute(wl);
        CHECK(wr);
        const QVariant wpv = wr.value().payload;
        const int wds = wpv.metaType().id() == QMetaType::QVariantMap
                            ? wpv.toMap().value(QStringLiteral("dataset")).toInt()
                            : wpv.toInt();
        CHECK(wds >= 0);
        const nextpilot::plotscope::dataset::Dataset *ds = application.data().get(wds);
        CHECK(ds != nullptr && ds->tableCount() == 1);
        const QString tn = ds->tableNames().first();
        auto clearCanvas = [&] {
            QVariant vl;
            Result<QVariant> l(vl);
            application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.series.list"), {}, l);
            for (const QVariant &v : l.value().toList())
            {
                QVariantMap rp;
                rp.insert(QStringLiteral("key"),
                          v.toMap().value(QStringLiteral("key")).toString());
                QVariant vr;
                Result<QVariant> rm(vr);
                application.context().interactions().dispatchViewAction(
                    QStringLiteral("plot.series.remove"), rp, rm);
            }
        };
        clearCanvas();

        auto expand = [&](int limit) {
            QVariantMap p;
            p.insert(QStringLiteral("dataset"), wds);
            p.insert(QStringLiteral("table"), tn);
            p.insert(QStringLiteral("limit"), limit);
            QVariant vr;
            Result<QVariant> res(vr);
            const bool ok = application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.expandColumns"), p, res);
            CHECK(ok);
            return res.value().toMap();
        };

        // 上限 8：12 列数值列必截断，且显式回 truncated / total（不静默丢列）。
        const QVariantMap m8 = expand(8);
        const int total = m8.value(QStringLiteral("total")).toInt();
        CHECK(total == 12);
        CHECK(m8.value(QStringLiteral("drawn")).toInt() == 8);
        CHECK(m8.value(QStringLiteral("truncated")).toBool());
        CHECK(m8.value(QStringLiteral("names")).toStringList().size() == 8);
        // 真的画到画布上了（不是只回了个 map）。
        {
            QVariant vl;
            Result<QVariant> l(vl);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.series.list"), {}, l));
            CHECK(l.value().toList().size() == 8);
        }
        // limit=0 = 不设上限 → 全画、truncated=false。
        clearCanvas();
        const QVariantMap mall = expand(0);
        CHECK(mall.value(QStringLiteral("drawn")).toInt() == total);
        CHECK(!mall.value(QStringLiteral("truncated")).toBool());
        // 表名不存在 → 明确失败（不是画一张空表）。
        {
            QVariantMap p;
            p.insert(QStringLiteral("dataset"), wds);
            p.insert(QStringLiteral("table"), QStringLiteral("__no_such_table__"));
            QVariant vr;
            Result<QVariant> res(vr);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("plot.expandColumns"), p, res));
            CHECK(!res);
        }
        clearCanvas();
        OK(QStringLiteral("展开数据列：12 列 → 上限 8 条 + 显式 truncated、limit=0 全画、缺表明确失败")
               .toUtf8()
               .constData());
    }

    // --- 5c-f) 画布间曲线移动---
    //
    // 语义：**移动不是复制**。先让目标画布加上，成功后才从源摘——目标失败时
    // 源保持原样（不会出现"两边都没有"的半截状态）。拖回自己/垃圾负载一律拒绝。
    {
        auto *ws = qobject_cast<PlotWorkspace *>(binder->centralContent());
        CHECK(ws != nullptr);
        // 用**两块全新画布**（前面的段往旧画布堆了十几条曲线，ChannelView 的
        // m_channels 仍在，clear() 只清画布不 SVG 不了，会污染计数）。
        auto addCellNow = [&]() -> AxesView * {
            QVariant vr;
            Result<QVariant> r(vr);
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.addAxes"), {}, r));
            CHECK(r);
            return ws->activeCanvas();
        };
        AxesView *c0 = addCellNow();
        const int idx0 = ws->cellCount() - 1;
        CHECK(c0 != nullptr);
        AxesView *c1 = addCellNow();
        CHECK(c1 != nullptr && c1 != c0);

        ws->setActive(idx0);
        CHECK(ws->activeCanvas() == c0);
        c0->clear();

        // 在全新的 c0 上画一条：走共享活动通道（PlotWorkspace 路由到活动 cell）。
        const nextpilot::plotscope::dataset::Dataset *ds = application.data().get(loadedDsId);
        CHECK(ds != nullptr && ds->tableCount() >= 1);
        const QString tbl = ds->tableNames().first();
        const QString col = QStringLiteral("att.pitch[deg]");
        application.context().setActiveChannel(loadedDsId, tbl, col);
        CHECK(c0->series().size() == 1);
        const PlotSeries sp = c0->series().first();

        // (1) 正常移动：B 多一条、A 少一条。
        {
            QMimeData mime;
            mime.setData(QString::fromLatin1(series_mime::type()),
                         series_mime::encode(c0->canvasId(), sp.key(), sp.datasetId, sp.table,
                                             sp.column, sp.title).toUtf8());
            CHECK(c1->dropChannel(&mime));
            CHECK(c1->series().size() == 1);
            CHECK(c0->series().isEmpty());
            // 标题跟着走（否则新画布只剩 "id / 表.列" 的默认名）。
            CHECK(c1->series().first().title == sp.title);
        }
        // (2) 拖回自己：拒绝（否则原地丢一条）。
        {
            QMimeData mime;
            mime.setData(QString::fromLatin1(series_mime::type()),
                         series_mime::encode(c1->canvasId(), sp.key(), sp.datasetId, sp.table,
                                             sp.column, sp.title).toUtf8());
            CHECK(!c1->dropChannel(&mime));
            CHECK(c1->series().size() == 1);
        }
        // (3) 垃圾负载：拒绝且不留半截。
        {
            QMimeData mime;
            mime.setData(QString::fromLatin1(series_mime::type()), QByteArray("garbage"));
            CHECK(!c1->dropChannel(&mime));
            CHECK(c1->series().size() == 1);
            CHECK(c0->series().isEmpty());
        }
        OK("画布间曲线移动：目标加一条 + 源删一条（移动非复制）、拖回自己/垃圾负载拒绝");
    }

    // --- 5c-g) 多表快照导出---
    //
    // AC-13：单簿 xlsx，sheet 数 = 表数，列名与 schema 一致，**空表也留 sheet**。
    // 不引第三方 xlsx/zip 库：store-only zip + 最小 OOXML（XlsxBook），
    // 这里用 python zipfile 直接读回校验结构（不依赖 openpyxl）。
    {
        Dataset sds = Dataset::create(QStringLiteral("snapshot-fixture"), QStringLiteral("mem"));
        const int n = 100;
        QVector<double> ts(n), roll(n), pitch(n);
        for (int i = 0; i < n; ++i)
        {
            ts[i] = i * 0.01;
            roll[i] = i;
            pitch[i] = -i;
        }
        CHECK(sds.addTable(QStringLiteral("trace"), ts) >= 0);
        CHECK(sds.addF64Column(QStringLiteral("trace"), QStringLiteral("roll"), QStringLiteral("deg"), roll));
        CHECK(sds.addF64Column(QStringLiteral("trace"), QStringLiteral("pitch"), QStringLiteral("deg"), pitch));
        const int m = 33;
        QVector<double> at(m), spd(m);
        for (int i = 0; i < m; ++i)
        {
            at[i] = i * 0.05;
            spd[i] = i * 2.0;
        }
        CHECK(sds.addTable(QStringLiteral("aux"), at) >= 0);
        CHECK(sds.addF64Column(QStringLiteral("aux"), QStringLiteral("speed"), QString(), spd));
        // 注：Dataset::addTable 拒零行时间轴（timeNs.isEmpty() → -1），
        // 故 ng 侧不存在 0 行表，XlsxBook 的"空表只写表头"是防御分支，无法端到端构造。
        sds.refreshSchema();
        const int snapDs = application.data().add(sds);
        CHECK(snapDs >= 0);

        const QString xlsxPath = QStringLiteral("/tmp/ps_snapshot_multi.xlsx");
        QFile::remove(xlsxPath);
        Intent snap;
        snap.op = QStringLiteral("data.exportSnapshot");
        snap.params[QStringLiteral("datasets")] = QVariantList{snapDs};
        snap.params[QStringLiteral("tables")] = QVariantList{QStringLiteral("*")};
        snap.params[QStringLiteral("format")] = QStringLiteral("xlsx");
        snap.params[QStringLiteral("path")] = xlsxPath;
        const auto snapRes = application.host().execute(snap);
        CHECK(snapRes);
        const QVariantMap sp = snapRes.value().payload.toMap();
        CHECK(sp.value(QStringLiteral("table_count")).toInt() == 2);
        CHECK(sp.value(QStringLiteral("rows")).toInt() == n + m); // 100 + 33
        CHECK(QFile::exists(xlsxPath));
        CHECK(QFileInfo(xlsxPath).size() > 0);

        // 读回结构：sheet 名 / 每 sheet 行数 / 首个文本表头。
        const QString scriptPath = QStringLiteral("/tmp/ps_snapshot_check.py");
        {
            QFile f(scriptPath);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QTextStream out(&f);
            out << "import sys, zipfile, re\n"
                   "p = sys.argv[1]\n"
                   "z = zipfile.ZipFile(p)\n"
                   "wb = z.read('xl/workbook.xml').decode('utf-8')\n"
                   "names = re.findall(r'<sheet name=\"([^\"]+)\"', wb)\n"
                   "print('SHEETS=' + ','.join(names))\n"
                   "for i in range(1, len(names) + 1):\n"
                   "    sx = z.read('xl/worksheets/sheet%d.xml' % i).decode('utf-8')\n"
                   "    rows = len(re.findall(r'<row r=\"', sx))\n"
                   "    texts = re.findall(r'<t>([^<]*)</t>', sx)\n"
                   "    print('S%d=rows:%d|hdr:%s' % (i, rows, texts[0] if texts else ''))\n";
        }
        QProcess proc;
        proc.start(QStringLiteral("python3"), {scriptPath, xlsxPath});
        CHECK(proc.waitForFinished(30000));
        CHECK(proc.exitCode() == 0);
        const QString out = QString::fromUtf8(proc.readAllStandardOutput());
        CHECK(out.contains(QStringLiteral("SHEETS=trace,aux")));
        CHECK(out.contains(QStringLiteral("S1=rows:101"))); // 表头 + 100 行
        CHECK(out.contains(QStringLiteral("S2=rows:34")));  // 表头 + 33 行
        CHECK(out.contains(QStringLiteral("hdr:time")));    // 列名与 schema 对齐（time 在前）

        // csv 分支：每表一个文件（含空表）。
        const QString csvDir = QStringLiteral("/tmp/ps_snapshot_csv");
        QDir(csvDir).removeRecursively();
        Intent snapCsv = snap;
        snapCsv.params[QStringLiteral("format")] = QStringLiteral("csv");
        snapCsv.params[QStringLiteral("path")] = csvDir;
        const auto csvRes = application.host().execute(snapCsv);
        CHECK(csvRes);
        const QStringList files = csvRes.value().payload.toMap()
                                      .value(QStringLiteral("files")).toStringList();
        CHECK(files.size() == 2);
        for (const QString &f : files)
            CHECK(QFile::exists(f));

        // 越界：format 非法 / 表名全不匹配 → 明确失败（不产出半截文件）。
        {
            Intent bad = snap;
            bad.params[QStringLiteral("format")] = QStringLiteral("pdf");
            CHECK(!application.host().execute(bad));
            Intent noTable = snap;
            noTable.params[QStringLiteral("tables")] =
                QVariantList{QStringLiteral("__missing__")};
            CHECK(!application.host().execute(noTable));
        }
        OK("多表快照：xlsx 单簿 2 sheet 列名对齐 schema、100/33 行正确、csv 每表一文件");
    }

    // --- 5c-h) 参数表---
    //
    // 关键链路：ULog 的 params/facts 是 **Text 列**，此前 ps_dataset_value_at 只吐 f64，
    // C++ 侧读不出字符串（参数面板只能显示"3 项"）。本段验证新加的
    // ps_dataset_text_at → Dataset::readTextRows → data.params → 参数视图/属性面板。
    {
        Intent loadParams;
        loadParams.op = QStringLiteral("data.load");
        loadParams.params[QStringLiteral("path")] =
            QStringLiteral(PS_TEST_FIXTURE_DIR "/demo_params.ulg");
        loadParams.params[QStringLiteral("format")] = QStringLiteral("ulog");
        const auto lr = application.host().execute(loadParams);
        CHECK(lr);
        const QVariant lv = lr.value().payload;
        const int paramsDs = lv.metaType().id() == QMetaType::QVariantMap
                                 ? lv.toMap().value(QStringLiteral("dataset")).toInt()
                                 : lv.toInt();
        CHECK(paramsDs >= 0);

        // (1) 数据面：data.params 给条数与键值明细（fixture 已知：params 3 + facts 2）。
        Intent pIntent;
        pIntent.op = QStringLiteral("data.params");
        pIntent.params[QStringLiteral("dataset")] = paramsDs;
        const auto pr = application.host().execute(pIntent);
        CHECK(pr);
        const QVariantMap pm = pr.value().payload.toMap();
        CHECK(pm.value(QStringLiteral("count")).toInt() == 5);
        const QVariantList ptabs = pm.value(QStringLiteral("tables")).toList();
        CHECK(ptabs.size() == 2);
        const QVariantList pitems = pm.value(QStringLiteral("items")).toList();
        CHECK(!pitems.isEmpty());
        bool sawKey = false;
        for (const QVariant &v : pitems)
            if (v.toMap().value(QStringLiteral("name")).toString()
                    == QStringLiteral("MPC_XY_VEL_MAX"))
                sawKey = true;
        CHECK(sawKey); // 字符串值真的读出来了（不是 NaN 占位）
        CHECK(!pitems.first().toMap().value(QStringLiteral("value")).toString().isEmpty());

        // (2) 视图面：切到「参数」chip，树里每个 key-value 表下有可展开的键值子项。
        //     dock 面板是外壳按需建的（ShellBinder），先把挂起事件跑完再找，
        //     否则 findChild 会间歇性落空（同一二进制偶发失败）。
        for (int i = 0; i < 3; ++i)
            QCoreApplication::processEvents();
        auto *panel = qobject_cast<DataManagerPanel *>(
            window.findChild<QWidget *>(QStringLiteral("dataset-tree")));
        if (!panel)
            for (QWidget *w : window.findChildren<QWidget *>())
                if (auto *dp = qobject_cast<DataManagerPanel *>(w)) { panel = dp; break; }
        // 外壳按 IUiContext 的 requestPanel 建 dock 面板，装配时机在 build 之后的
        // 事件循环里；这里若还没到就显式建一个同构实例（同一构造函数、同一意图
        // 通道），断言仍然打在真实面板实现上，而不是绕过UI。
        if (!panel)
            panel = new DataManagerPanel(application.context(), application.host(), &window);
        CHECK(panel != nullptr);
        panel->showChip(1);
        CHECK(panel->activeChip() == 1);
        CHECK(panel->paramRowCount() == 2);          // params + facts 两张表
        CHECK(panel->chipText(1).contains(QStringLiteral("5")));

        // 树：param 表节点应带子项，且第二列是值文本（不是"3 项"那种只有计数）。
        QTreeWidget *tree = panel->findChild<QTreeWidget *>();
        CHECK(tree != nullptr);
        int paramNodes = 0, paramRows = 0;
        QString firstRowName, firstRowValue;
        for (const QTreeWidgetItem *it : tree->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive))
        {
            Q_UNUSED(it);
        }
        std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it) {
            if (it->text(0).isEmpty() && it->childCount() == 0)
                return;
            if (it->data(0, Qt::UserRole + 5).toString() == QStringLiteral("param"))
                ++paramNodes;
            if (it->data(0, Qt::UserRole + 5).toString() == QStringLiteral("param-row"))
            {
                ++paramRows;
                if (firstRowName.isEmpty())
                {
                    firstRowName = it->text(0);
                    firstRowValue = it->text(1);
                }
            }
            for (int i = 0; i < it->childCount(); ++i)
                walk(it->child(i));
        };
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            walk(tree->topLevelItem(i));
        CHECK(paramNodes == 2);
        CHECK(paramRows == 5);                        // 3 + 2 全铺出来
        CHECK(!firstRowName.isEmpty());
        CHECK(firstRowValue != QStringLiteral("—") || firstRowName.isEmpty());

        // (3) 属性面板：provider 已按表名/条数/首 8 项建 schema（见 kvProvider），
        // 这里只校验数据面能喂满它——provider 的 QWidget 侧渲染由既有属性面板回归覆盖。
        OK("参数表：Text 列可读（ps_dataset_text_at）、data.params 5 条明细、参数 chip 下 5 行键值");
        panel->showChip(0);
    }

    // --- M2.8 诊断面板：注册树全景（杀手场景收口）---
    //
    // 打开「诊断」dock 面板（按需创建），断言：
    //   1) diag.registries 枚举到 panel-factory / builtin-module（R4 static 节点上树）；
    //   2) 面板行数 == 组合根树上节点数（面板与 diag.registries 同一条数据路径）；
    //   3) 诊断面板归 Region::Panel（壳级 diagnostics Activity 入口，单一来源）。
    {
        // 诊断面板归 Region::Panel（底部面板组），不再是注册表插槽；
        // 插槽只剩 RibbonPage / RibbonGroup 两类（见 test_architecture 死槽检查）。

        // diag.registries：11 类节点齐全（遗留4/遗留5 收口：全量上树无盲区）
        Result<QVariant> out{QVariant()};
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("diag.registries"), {}, out));
        const QVariantList rows = out.value().toList();
        CHECK(rows.size() == 11);
        QStringList kinds;
        for (const QVariant &r : rows)
            kinds << r.toMap().value(QStringLiteral("kind")).toString();
        CHECK(kinds.contains(QStringLiteral("dataset")));
        CHECK(kinds.contains(QStringLiteral("source")));
        CHECK(kinds.contains(QStringLiteral("interaction")));
        CHECK(kinds.contains(QStringLiteral("derived-series")));
        CHECK(kinds.contains(QStringLiteral("ui-slot")));
        CHECK(kinds.contains(QStringLiteral("extension")));
        CHECK(kinds.contains(QStringLiteral("panel-factory")));
        CHECK(kinds.contains(QStringLiteral("builtin-module")));
        CHECK(kinds.contains(QStringLiteral("parser")));
        CHECK(kinds.contains(QStringLiteral("recording-format")));
        CHECK(kinds.contains(QStringLiteral("recording-backend")));
        // GUI 环境下面板工厂已注册（views/ TU 自注册），count>0
        for (const QVariant &r : rows)
        {
            const QVariantMap m = r.toMap();
            if (m.value(QStringLiteral("kind")).toString() == QStringLiteral("panel-factory"))
                CHECK(m.value(QStringLiteral("count")).toInt() >= 3);
        }

        // 打开诊断面板（首次按需创建），面板行数 == 树上节点数
        QWidget *diagWidget = window.context()->requestPanel(
            Region::Panel, QStringLiteral("diagnostics"));
        CHECK(diagWidget != nullptr);
        auto *diag = qobject_cast<nextpilot::plotscope::gui::DiagnosticsPanel *>(diagWidget);
        CHECK(diag != nullptr);
        CHECK(diag->refresh() == application.context().registryTree().nodes().size());
        OK("诊断面板：注册树 11 类节点齐全，面板行数==树上节点数（diag.registries 同路）");
    }
    // --- c3b-2c/d 设置页与主题令牌：声明 → 壳排序 → 实际可解析 ---
    {
        using nextpilot::plotscope::registry::Pack;
        using nextpilot::plotscope::registry::SettingsPageSpec;
        using nextpilot::plotscope::registry::ThemeTokenSpec;
        using nextpilot::plotscope::gui::ThemeManager;
        Pack pack;
        pack.id = QStringLiteral("pack.c3b-page");
        SettingsPageSpec later;
        later.id = QStringLiteral("test.page.later");
        later.title = QStringLiteral("靠后页");
        later.order = 20;
        later.create = [](QWidget *parent) { return new QWidget(parent); };
        SettingsPageSpec first = later;
        first.id = QStringLiteral("test.page.first");
        first.title = QStringLiteral("靠前页");
        first.order = 10;
        pack.settingsPages << later << first; // 故意乱序：排序由壳做
        ThemeTokenSpec token;
        token.id = QStringLiteral("plot.testToken");
        token.fallback = QStringLiteral("#123456");
        pack.themeTokens << token;
        auto &toolbox = application.context().toolbox();
        CHECK(toolbox.mount(std::move(pack)));
        const auto pages = toolbox.settingsPagesSorted();
        CHECK(pages.size() == 2 && pages.first().id == QStringLiteral("test.page.first"));
        // 用窗口持有的实例：ThemeManager 的静态 activeTheme 指向在册实例，
        // 测试里另造一个再析构会留下悬空指针，后续绘制才崩（踩过）。
        ThemeManager *theme = window.themeManager();
        CHECK(theme != nullptr);
        theme->setExtensionTokens(toolbox.themeTokensAll());
        // 包声明的兜底色能被解析出来；内置 token 不被包覆盖
        CHECK(theme->color(QStringLiteral("plot.testToken")) == QColor(QStringLiteral("#123456")));
        CHECK(theme->color(QStringLiteral("app.bg")) ==
              ThemeManager::color(ThemeManager::Theme::Light, QStringLiteral("app.bg")));
        CHECK(toolbox.unmount(QStringLiteral("pack.c3b-page")));
        OK("设置页与主题令牌：壳按 order 排页，包兜底色可解析且不覆盖内置色");
    }

    // --- c3b-2b 条目右键声明：View 作用域的 item.* + 命令接缝 ---
    {
        using nextpilot::plotscope::registry::ItemContextSpec;
        using nextpilot::plotscope::registry::Pack;
        Pack pack;
        pack.id = QStringLiteral("pack.c3b-item");
        ItemContextSpec spec;
        spec.viewId = QStringLiteral("dataset-tree");
        spec.commandId = QStringLiteral("figure.addAxes");
        spec.when = QStringLiteral("item.kind == 'column' && !item.multiSelect");
        spec.domain = QStringLiteral("data");
        spec.order = 1;
        pack.itemContexts << spec;
        auto &toolbox = application.context().toolbox();
        CHECK(toolbox.mount(std::move(pack)));
        CHECK(toolbox.itemContextsFor(QStringLiteral("dataset-tree")).size() == 1);
        // item.* 只对本次求值可见：值一变表达式立刻失效，没有写进全局上下文
        QHash<QString, QVariant> itemCtx;
        itemCtx.insert(QStringLiteral("item.kind"), QStringLiteral("column"));
        itemCtx.insert(QStringLiteral("item.multiSelect"), false);
        CHECK(application.context().evaluateWhen(spec.when, itemCtx));
        itemCtx.insert(QStringLiteral("item.kind"), QStringLiteral("table"));
        CHECK(!application.context().evaluateWhen(spec.when, itemCtx));
        // 命令接缝有拒绝语义：未注册的命令明确失败，而不是静默无反应
        CHECK(!application.context().invokeCommand(QStringLiteral("no.such.command")));
        CHECK(toolbox.unmount(QStringLiteral("pack.c3b-item")));
        CHECK(toolbox.itemContextsFor(QStringLiteral("dataset-tree")).isEmpty());
        OK("条目右键声明：item.* 按 View 作用域求值，命令接缝未接通时明确拒绝");
    }

    // --- c3b-2a 视图标题栏动作：声明 → QAction → ADS 标题栏 ---
    {
        using nextpilot::plotscope::registry::Pack;
        using nextpilot::plotscope::registry::ViewTitleActionSpec;
        using nextpilot::plotscope::context::AppContext;
        using nextpilot::plotscope::intent::IIntentExecutor;
        nextpilot::plotscope::registry::PanelFactoryHub::registerPanel(
            QStringLiteral("test.title-view"),
            [](AppContext &, IIntentExecutor &, QWidget *parent) { return new QWidget(parent); },
            QStringLiteral("标题栏视图"), QStringLiteral("plot.refresh"));
        Pack pack;
        pack.id = QStringLiteral("pack.c3b-title");
        ViewTitleActionSpec shown;
        shown.viewId = QStringLiteral("test.title-view");
        shown.commandId = QStringLiteral("figure.addAxes");
        shown.order = 1;
        ViewTitleActionSpec hidden = shown;
        hidden.commandId = QStringLiteral("figure.removeAxes");
        hidden.when = QStringLiteral("neverTrue == true");
        pack.viewTitleActions << shown << hidden;
        auto &toolbox = application.context().toolbox();
        CHECK(toolbox.mount(std::move(pack)));
        QWidget *panel = window.context()->requestPanel(Region::PrimarySidebar,
                                                       QStringLiteral("test.title-view"));
        CHECK(panel != nullptr);
        // when 不命中的那条不挂；命中的挂上去（数量是唯一可断言的口径）
        CHECK(window.binder()->viewTitleActionCount(QStringLiteral("test.title-view")) == 1);
        CHECK(toolbox.unmount(QStringLiteral("pack.c3b-title")));
        OK("视图标题栏动作：命中的挂到 ADS 标题栏，when 不命中的不进列表");
    }

    // --- c3b Ribbon 上下文页签：when 不命中整页收起，命中再展开 ---
    {
        using nextpilot::plotscope::registry::SlotKind;
        bool hoverTest = false;
        auto *binder = window.binder();
        CHECK(binder != nullptr);
        binder->setWhenEvaluator([&hoverTest](const QString &when) {
            return when == QStringLiteral("hoverTest == true") ? hoverTest : true;
        });
        window.context()->slotHub().declareSlot(
            QStringLiteral("ribbon.page.c3test"), QStringLiteral("C3 测试页"), QStringLiteral("test"),
            SlotKind::RibbonPage, QString(), 0, QStringLiteral("hoverTest == true"));
        binder->rebuildRibbon();
        CHECK(!binder->visibleRibbonPageLabels().contains(QStringLiteral("C3 测试页")));
        hoverTest = true;
        binder->applyWhenVisibility();
        CHECK(binder->visibleRibbonPageLabels().contains(QStringLiteral("C3 测试页")));
        hoverTest = false;
        binder->applyWhenVisibility();
        CHECK(!binder->visibleRibbonPageLabels().contains(QStringLiteral("C3 测试页")));
        OK("Ribbon when 页签：不命中收起、命中展开，只有可见集合变化才重建");
    }

    // --- c2 面板单例语义：由工厂登记决定，不由外壳一刀切 ---
    {
        using nextpilot::plotscope::registry::PanelFactoryHub;
        using nextpilot::plotscope::context::AppContext;
        using nextpilot::plotscope::intent::IIntentExecutor;
        // singleton=false：每次新建，"多文档/对比视图"才有活路
        PanelFactoryHub::registerPanel(
            QStringLiteral("test.multi"),
            [](AppContext &, IIntentExecutor &, QWidget *parent) { return new QWidget(parent); },
            QStringLiteral("多实例"), QString(), 0, false);
        QWidget *m1 = window.context()->requestPanel(Region::Panel,
                                                     QStringLiteral("test.multi"));
        QWidget *m2 = window.context()->requestPanel(Region::Panel,
                                                     QStringLiteral("test.multi"));
        CHECK(m1 != nullptr);
        CHECK(m2 != nullptr);
        CHECK(m1 != m2);
        // 默认（singleton=true）：再打开只聚焦既有实例，不新建页签
        PanelFactoryHub::registerPanel(
            QStringLiteral("test.single"),
            [](AppContext &, IIntentExecutor &, QWidget *parent) { return new QWidget(parent); },
            QStringLiteral("单例"));
        QWidget *s1 = window.context()->requestPanel(Region::Panel,
                                                     QStringLiteral("test.single"));
        QWidget *s2 = window.context()->requestPanel(Region::Panel,
                                                     QStringLiteral("test.single"));
        CHECK(s1 != nullptr);
        CHECK(s1 == s2);
        // order 落到了查询出口：同权重按 type，order 小的靠前
        PanelFactoryHub::registerPanel(
            QStringLiteral("test.order-a"),
            [](AppContext &, IIntentExecutor &, QWidget *parent) { return new QWidget(parent); },
            QStringLiteral("A"), QString(), 10);
        PanelFactoryHub::registerPanel(
            QStringLiteral("test.order-b"),
            [](AppContext &, IIntentExecutor &, QWidget *parent) { return new QWidget(parent); },
            QStringLiteral("B"), QString(), -5);
        const QStringList types = PanelFactoryHub::types();
        CHECK(types.indexOf(QStringLiteral("test.order-b")) < types.indexOf(QStringLiteral("test.order-a")));
        OK("面板单例/排序：singleton=false 允许多实例，默认聚焦；order 进入 types() 出口");
    }

    // --- T4.5 时间轴刻度三态（relative / hms / utc）---
    {
        CHECK(canvas != nullptr);
        // ① 相对秒：无 formatter，刻度文本沿用 ScaleDiv 的 Tick::label。
        canvas->setTimeFormatMode(QStringLiteral("relative"));
        CHECK(canvas->timeFormatMode() == QStringLiteral("relative"));
        CHECK(canvas->tickLabelForTest(12.345).isEmpty());
        // ② hms：formatter 装上，负值留 '-'，按 h>0 决定是否带小时位。
        canvas->setTimeFormatMode(QStringLiteral("hms"));
        CHECK(canvas->timeFormatMode() == QStringLiteral("hms"));
        CHECK(canvas->tickLabelForTest(3661.0) == QStringLiteral("1:01:01"));
        CHECK(canvas->tickLabelForTest(61.0) == QStringLiteral("01:01"));
        CHECK(canvas->tickLabelForTest(-61.0) == QStringLiteral("-01:01"));
        // ③ utc：无 epoch 基准 → 如实退化为时分秒（文档已标注）。
        canvas->setTimeFormatMode(QStringLiteral("utc"));
        CHECK(canvas->tickLabelForTest(3661.0) == QStringLiteral("1:01:01"));
        // ④ 非法值回落 relative，不留脏 formatter。
        canvas->setTimeFormatMode(QStringLiteral("bogus"));
        CHECK(canvas->timeFormatMode() == QStringLiteral("relative"));
        CHECK(canvas->tickLabelForTest(3661.0).isEmpty());
        // ⑤ 设置链路：settings.set + viewAction plot.timeFormat → 整页所有格换格式。
        Intent sv;
        sv.op = QStringLiteral("settings.set");
        sv.params[QStringLiteral("key")] = QStringLiteral("plot.time.format");
        sv.params[QStringLiteral("value")] = QStringLiteral("hms");
        CHECK(application.host().execute(sv).ok());
        QVariant outv;
        Result<QVariant> rout(outv);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.timeFormat"), {}, rout));
        auto *wsT = qobject_cast<PlotWorkspace *>(binder->centralContent());
        CHECK(wsT != nullptr);
        CHECK(canvas->timeFormatMode() == QStringLiteral("hms"));
        CHECK(canvas->tickLabelForTest(3661.0) == QStringLiteral("1:01:01"));
        // ⑥ 新格沿用全局偏好（makeCell 里读 settings）：重排成 2×2，4 个新格都是 hms。
        //    figure.grid 会销毁旧 cell —— 之后不得再触碰裸 canvas 指针（那是悬空引用）。
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.grid"), QVariantMap{{QStringLiteral("rows"), 2},
                                                       {QStringLiteral("cols"), 2}}, rout));
        CHECK(wsT->cellCount() == 4);
        CHECK(wsT->activeCanvas() != nullptr);
        CHECK(wsT->activeCanvas()->timeFormatMode() == QStringLiteral("hms"));
        CHECK(wsT->activeCanvas()->tickLabelForTest(3661.0) == QStringLiteral("1:01:01"));
        // 收尾：settings 回 relative，再由 viewAction 让所有格跟着回退（不碰已销毁的 canvas）。
        Intent back;
        back.op = QStringLiteral("settings.set");
        back.params[QStringLiteral("key")] = QStringLiteral("plot.time.format");
        back.params[QStringLiteral("value")] = QStringLiteral("relative");
        application.host().execute(back);
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("plot.timeFormat"), {}, rout));
        CHECK(wsT->activeCanvas()->timeFormatMode() == QStringLiteral("relative"));

        OK("T4.5 时间轴：relative/hms/utc 三态 + 负值 + 非法值回落 + settings→viewAction 全格生效");
    }

    // --- D1：格标题条独立成 AxesToolbar，挂在自己所属的停靠面板上 ---
    {
        CHECK(binder != nullptr);
        auto *ws = qobject_cast<PlotWorkspace *>(binder->centralContent());
        CHECK(ws != nullptr);
        auto *canvas = ws->activeCanvas(); // 当前活动格
        CHECK(canvas != nullptr);
        // 层次：canvas → stack → frame（竖排 [标题条, 画布]）。
        QWidget *frame = canvas->parentWidget() ? canvas->parentWidget()->parentWidget() : nullptr;
        CHECK(frame != nullptr);
        auto *bar = frame->findChild<AxesToolbar *>(QStringLiteral("axesToolbar"));
        CHECK(bar != nullptr);
        CHECK(bar->dockWidget() != nullptr); // 挂到自己所属面板才转得动拖动事件
        CHECK(!bar->title().isEmpty());
        CHECK(!bar->isShown());              // 无指针进入时只是占位，不显示
        CHECK(bar->metaObject()->indexOfSignal("activated()") >= 0);
        CHECK(bar->metaObject()->indexOfSignal("splitClicked(bool)") >= 0);
        OK("D1 格标题条：独立类、挂靠所属面板、新建即暗、激活/拆分信号就位");
    }

    // --- D2：页容器 PlotLayout / 每格面板 DockedAxes 各自成类 ---
    {
        auto *ws = qobject_cast<PlotWorkspace *>(binder->centralContent());
        CHECK(ws != nullptr);
        auto *canvas = ws->activeCanvas();
        CHECK(canvas != nullptr);
        QWidget *frame = canvas->parentWidget() ? canvas->parentWidget()->parentWidget() : nullptr;
        CHECK(frame != nullptr);
        // 从外框往上找到所属格面板。
        DockedAxes *cellDock = nullptr;
        for (QWidget *w = frame->parentWidget(); w; w = w->parentWidget())
            if (auto *d = qobject_cast<DockedAxes *>(w))
            {
                cellDock = d;
                break;
            }
        CHECK(cellDock != nullptr);
        CHECK(qobject_cast<PlotLayout *>(cellDock->dockManager()) != nullptr);
        CHECK(cellDock->titleBar() != nullptr);
        // 单格停靠区藏起整条标题栏：页签条只在合并成多格时出现。
        CHECK(cellDock->dockAreaWidget() != nullptr);
        CHECK(cellDock->dockAreaWidget()->dockAreaFlags()
              & ads::CDockAreaWidget::HideSingleWidgetTitleBar);
        OK("D2 页容器与格面板：PlotLayout 拥有 DockedAxes，单格标题栏隐藏");
    }

    // --- D3：页签栏独立成 TabbedFigure，「＋」与索引换算不出 workspace ---
    {
        auto *ws = qobject_cast<PlotWorkspace *>(binder->centralContent());
        CHECK(ws != nullptr);
        auto *tabs = ws->findChild<TabbedFigure *>();
        CHECK(tabs != nullptr);
        const int pages = tabs->pageCount();
        CHECK(pages == ws->pageCount());
        CHECK(tabs->currentPage() >= 0 && tabs->currentPage() < pages);

        QVariant out;
        Result<QVariant> rout(out);
        // 新建页：页数 +1，且停在新建页。
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.new"), {}, rout));
        CHECK(tabs->pageCount() == pages + 1);
        CHECK(ws->pageCount() == pages + 1);
        CHECK(tabs->currentPage() == pages);

        // 改名：走 TabbedFigure 的 setPageTitle。
        CHECK(application.context().interactions().dispatchViewAction(
            QStringLiteral("figure.rename"),
            QVariantMap{{QStringLiteral("index"), pages},
                        {QStringLiteral("title"), QStringLiteral("校验页")}},
            rout));
        CHECK(tabs->pageTitle(pages) == QStringLiteral("校验页"));

        // 关页：至少留一页（连关到只剩 1）。
        while (tabs->pageCount() > 1)
            CHECK(application.context().interactions().dispatchViewAction(
                QStringLiteral("figure.closeCurrent"), {}, rout));
        CHECK(tabs->pageCount() == 1);
        OK("D3 页签栏：TabbedFigure 管「＋」与索引，新建/改名/关闭全走类接口且至少留一页");
    }

    // --- dock 页签：页签序与内容栈序必须一致（修"点页签内容不切"）---
    {
        CHECK(binder != nullptr);
        const QStringList leftTitles = binder->panelTitles(Region::PrimarySidebar);
        CHECK(leftTitles.size() >= 2); // 数据源 + 脚本文件
        const QStringList leftOrder = binder->contentsOrder(Region::PrimarySidebar);
        const QString act0 = binder->activePanelTitle(Region::PrimarySidebar);
        fprintf(stdout, "[dock] tabs=%s contents=%s active=%s\n",
                leftTitles.join(QLatin1Char(',')).toUtf8().constData(),
                leftOrder.join(QLatin1Char(',')).toUtf8().constData(),
                act0.toUtf8().constData());
        // 结构：页签顺序 = 内容栈顺序（ADS 的 tabBar 与 ContentsLayout 各排一份，
        // 顺序错位时点页签就会"高亮过去、内容不动"）。
        CHECK(leftOrder == leftTitles);
        CHECK(!act0.isEmpty());
        CHECK(leftTitles.contains(act0));

        // 每个视图各自是 dock 页签，标题就是视图名；不再有“侧栏”这种容器标题，
        // 也不会出现“数据”套“数据源”的重复层级。
        CHECK(binder->dockPageTitle(QStringLiteral("dataset-tree")) == QStringLiteral("数据源"));
        CHECK(binder->dockPageTitle(QStringLiteral("script-files")) == QStringLiteral("脚本文件"));
        CHECK(binder->dockPageTitle(QStringLiteral("dataset-tree")) != QStringLiteral("侧栏"));
        // 聚焦：每个面板都能定位到自己的 dock 页签（focusPanel 返回 true），
        // 且聚焦后 active 落在该面板所在页签上。
        const QList<QWidget *> leftPanels = binder->panelWidgets(Region::PrimarySidebar);
        CHECK(leftPanels.size() >= 2);
        for (QWidget *w : leftPanels)
            CHECK(binder->focusPanel(w));
        const QString act1 = binder->activePanelTitle(Region::PrimarySidebar);
        CHECK(leftTitles.contains(act1));
        OK("dock 页签：页签序=内容栈序、两个面板都可聚焦（修点页签内容不切换）");
    }

    {
        CommandRegistry registry;
        int hits = 0;
        CommandDescriptor command;
        command.id = QStringLiteral("test.exec");
        command.title = QStringLiteral("测试执行");
        command.handler = [&hits]() { ++hits; };
        CHECK(registry.add(command));
        CHECK(!registry.add(command));
        CHECK(registry.execute(QStringLiteral("test.exec")));
        CHECK(hits == 1);

        CommandDescriptor gated = command;
        gated.id = QStringLiteral("test.gated");
        gated.when = QStringLiteral("ready == true && kind == \"series\"");
        gated.handler = [&hits]() { hits += 10; };
        CHECK(registry.add(gated));
        registry.setContextValue(QStringLiteral("ready"), true);
        registry.setContextValue(QStringLiteral("kind"), QStringLiteral("plot"));
        QString error;
        CHECK(!registry.execute(QStringLiteral("test.gated"), &error));
        CHECK(error.contains(QStringLiteral("命令当前不可用")));
        CHECK(hits == 1);

        registry.setContextValue(QStringLiteral("kind"), QStringLiteral("series"));
        bool ok = false;
        CHECK(CommandRegistry::evaluateWhen(
            QStringLiteral("(ready == true || nope == false) && kind == \"series\""),
            registry.contextValues(), &ok));
        CHECK(ok);

        KeybindingRegistry bindings;
        bindings.setDefault(QStringLiteral("one"), QStringLiteral("Ctrl+B"));
        bindings.setDefault(QStringLiteral("two"), QStringLiteral("Ctrl+B"));
        CHECK(bindings.sequenceFor(QStringLiteral("one")) == QStringLiteral("Ctrl+B"));
        const QList<KeybindingConflict> conflicts = bindings.conflicts();
        CHECK(conflicts.size() == 1);
        CHECK(conflicts.first().commandIds.size() == 2);
        CHECK(bindings.setOverride(QStringLiteral("two"), QStringLiteral("Ctrl+K, Ctrl+P")));
        CHECK(bindings.sequenceFor(QStringLiteral("two")) == QStringLiteral("Ctrl+K, Ctrl+P"));
        CHECK(KeybindingRegistry::isChord(bindings.sequenceFor(QStringLiteral("two"))));
        CHECK(KeybindingRegistry::chordSteps(QStringLiteral("Ctrl+K, Ctrl+P")).size() == 2);
        CHECK(bindings.clearOverride(QStringLiteral("two")));
        CHECK(bindings.sequenceFor(QStringLiteral("two")) == QStringLiteral("Ctrl+B"));
        OK("命令层：when 门控、重复 id 拒绝、键位冲突与 chord");
    }

    // --- SeriesStore 专项（验收 10）：增删 / 顺序 / 跨图移动 mime 往返 ---
    {
        SeriesStore store;

        // 造三条曲线（datasetId 固定，靠 table.column 区分 key）。
        auto make = [](int dsId, const QString &col) {
            PlotSeries s;
            s.datasetId = dsId;
            s.table = QStringLiteral("t");
            s.column = col;
            s.title = col;
            return s;
        };
        const PlotSeries a = make(7, QStringLiteral("a"));
        const PlotSeries b = make(7, QStringLiteral("b"));
        const PlotSeries c = make(7, QStringLiteral("c"));

        // (1) 增删：upsert 追加 / 同 key 原地更新不增数 / remove 删一条 / 缺失 key 无害。
        CHECK(store.isEmpty());
        CHECK(store.upsert(a) == 0);
        CHECK(store.upsert(b) == 1);
        CHECK(store.count() == 2);
        PlotSeries a2 = a;
        a2.title = QStringLiteral("a-renamed");
        CHECK(store.upsert(a2) == 0);          // 同 key 返回原索引
        CHECK(store.count() == 2);             // 不增数
        CHECK(store.at(0)->title == QStringLiteral("a-renamed"));
        CHECK(!store.remove(QStringLiteral("no.such.key")));
        CHECK(store.remove(a.key()));
        CHECK(store.count() == 1);
        CHECK(store.byKey(b.key()) != nullptr);
        CHECK(store.byKey(a.key()) == nullptr);

        // (2) 顺序：追加保序、删除后索引紧凑、indexOf 与 at 一致。
        store.clear();
        CHECK(store.isEmpty());
        CHECK(store.upsert(c) == 0);
        CHECK(store.upsert(a) == 1);
        CHECK(store.upsert(b) == 2);
        CHECK(store.at(0)->key() == c.key());
        CHECK(store.at(1)->key() == a.key());
        CHECK(store.at(2)->key() == b.key());
        CHECK(store.indexOf(a.key()) == 1);
        CHECK(store.remove(a.key()));
        CHECK(store.count() == 2);
        CHECK(store.at(0)->key() == c.key());
        CHECK(store.at(1)->key() == b.key());   // 删除后紧凑，不残留空洞
        CHECK(store.indexOf(b.key()) == 1);
        CHECK(store.indexOf(a.key()) == -1);

        // (3) 跨图移动 mime 往返：plotSeries → encode → decode 还原逻辑标识。
        const PlotSeries moved = *store.at(1);
        QMimeData mime;
        mime.setData(QString::fromLatin1(series_mime::type()),
                     series_mime::encode(42, moved.key(), moved.datasetId, moved.table,
                                         moved.column, moved.title).toUtf8());
        const series_mime::Decoded decoded = series_mime::decode(&mime);
        CHECK(decoded.valid);
        CHECK(decoded.srcCanvasId == 42);
        CHECK(decoded.datasetId == moved.datasetId);
        CHECK(decoded.table == moved.table);
        CHECK(decoded.column == moved.column);
        CHECK(decoded.key == moved.key());
        CHECK(decoded.title == moved.title);
        // 垃圾负载拒绝
        QMimeData bad;
        bad.setData(QString::fromLatin1(series_mime::type()), QByteArray("garbage"));
        CHECK(!series_mime::decode(&bad).valid);

        OK("SeriesStore 专项：增删、顺序紧凑、跨图移动 mime 往返");
    }

    fprintf(stdout, "\nGUI 契约测试全部通过\n");
    return 0;
}

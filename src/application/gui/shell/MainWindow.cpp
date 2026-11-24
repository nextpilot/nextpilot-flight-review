#include "MainWindow.h"

#include <cstdio>

#include "GuiContext.h"
#include "CommandPalette.h"
#include "command/CommandRegistry.h"
#include "KeybindingDialog.h"
#include "command/KeybindingRegistry.h"
#include "ImportDialog.h"
#include "PreferencesIntentHandler.h"
#include "ProtocolInspectorDialog.h"
#include "ProtocolEditorDialog.h"
#include "ReportExporter.h"
#include "ShellBinder.h"
#include "theme/PlotThumbnails.h"
#include "ThemeManager.h"

#include "AppContext.h"
#include "RuntimeHost.h"
#include "SkeletonSlots.h"
#include "Intent.h"
#include "Result.h"
#include "UiSlotHub.h"
#include "IMessageParser.h"
#include "Contribution.h"

#include "SARibbonMainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QAction>
#include <QKeyEvent>
#include <QKeySequence>
#include "WorkspaceDataConfig.h"

#include <QCloseEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QResource>
#include <QSaveFile>
#include <QShowEvent>
#include <QTimer>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;
using namespace nextpilot::plotscope::host;
namespace gui = nextpilot::plotscope::gui;

// SARibbon 是**静态库**：它的 qrc 资源（主题配色 JSON、图标等）不会随
// 链接自动初始化——静态库的目标文件若未被引用，链接器会直接丢弃，
// 于是主题加载失败并退回内置 QSS（表现为启动时一条警告 + 观感降级）。
//
// Q_INIT_RESOURCE 展开成 `extern int qInitResources_xxx();`，
// 声明会带上当前命名空间——所以要在**全局作用域**的函数里调用它。
// 具名全局函数：Q_INIT_RESOURCE 展开的 extern 声明会带上所在命名空间，
// 放在匿名 namespace 里仍会变成 ns::qInitResources_xxx() 而链接不上。
// 放在**全局作用域**才与资源文件里生成的符号一致。
static void initSaribbonResources()
{
    Q_INIT_RESOURCE(SARibbonResource);
}

namespace nextpilot::plotscope::gui
{

MainWindow::MainWindow(AppContext &appCtx, QWidget *parent)
    : SARibbonMainWindow(parent,
                         // UseRibbonFrame    = 无边框（framelesshelper 接管拖拽/缩放/贴边，
                         //                     并由 SARibbon 自绘窗口按钮）
                         // UseRibbonMenuBar = 用 ribbon 条取代系统菜单栏
                         SARibbonMainWindowStyleFlag::UseRibbonFrame
                             | SARibbonMainWindowStyleFlag::UseRibbonMenuBar),
      m_appCtx(appCtx)
{
    initSaribbonResources();

    setWindowTitle(QStringLiteral("PlotScope"));
    resize(1280, 800);

    // 菜单栏说明：SARibbonMainWindow 已经把 **ribbon 条**装进了 QMainWindow 的
    // 菜单槽（menuWidget）。QMainWindow 的 menuBar 与 menuWidget 是**同一个槽位**
    // —— 调 setMenuBar(nullptr) 会把 ribbon 一起清掉，所以这里不再另建 QMenuBar；
    // 真正的动作由模块/工具箱声明到 ribbon 分组里。
    statusBar()->showMessage(QStringLiteral("就绪"));
}

MainWindow::~MainWindow()
{
    // 关窗时保存窗口状态：几何 + ADS 布局 + ribbon 页签。
    // 声明式快照，重启后可恢复；数据/扩展缺失时安全降级。
    saveState();
}

void MainWindow::declareStandardSlots()
{
    // app 骨架提供的标准插槽，声明清单在 host 层 SkeletonSlots（不写死在 core）。
    // 这里只做 GUI 侧的**幂等确认**（宿主已声明过，重复声明不会产生副本），
    // 保证即便脱离宿主单独使用 MainWindow，槽也齐备。
    UiSlotHub &slotReg = m_context->slotHub();
    nextpilot::plotscope::host::declareSkeletonSlots(slotReg);

    // 骨架自带的**标准动作**：往标准插槽里放内容。
    //
    // "打开数据"是 app 骨架级能力（data.load 本来就是系统级意图，
    // 不归属任何可卸载模块），所以由骨架提供；模块与工具箱往别的
    // 分组里放自己的入口，两者互不干扰。
    // action 归位到 8 页对应分组：每个 action 只放一个页，不再全堆在"主页"。
    // 骨架级能力（data.load/主题等系统级意图）由骨架声明；module 级入口由各 module 声明。
    auto addSkeleton = [&slotReg](const QString &id, const QString &label,
                                  const QString &area, const QString &op,
                                  const QString &icon, const QString &shortcut = QString(),
                                  int order = 0) {
        UiAction a;
        a.id = id;
        a.label = label;
        a.area = area;
        a.intentOp = op;
        a.icon = icon;
        a.shortcut = shortcut;
        a.order = order;
        slotReg.bindLocal(area, a, QStringLiteral("app.skeleton"));
    };

    // --- 开始页：落地页的高频入口聚合（打开 / 最近 / 新建绘图）---
    // "打开数据"：系统级意图 data.load，放"开始"页常用组（最高频入口）。
    addSkeleton(QStringLiteral("app.action.openData"), QStringLiteral("打开数据"),
                QStringLiteral("ribbon.group.home.data"), QStringLiteral("data.load"),
                QStringLiteral("file_open"), QStringLiteral("Ctrl+O"), 0);
    // "最近打开"：data.recent 意图，与打开数据同组相邻，符合"启动→打开或续开"的习惯。
    addSkeleton(QStringLiteral("home.action.recent"), QStringLiteral("最近打开"),
                QStringLiteral("ribbon.group.home.data"), QStringLiteral("data.recent"),
                QStringLiteral("history"), QString(), 1);
    // "新建绘图页"：id 直接用 figure.new，命中 PlotWorkspace 注册的 view action
    // （dispatcher 按 id 派发），intentOp 留空。
    addSkeleton(QStringLiteral("figure.new"), QStringLiteral("新建绘图页"),
                QStringLiteral("ribbon.group.home.data"), QString(),
                QStringLiteral("newpage"), QString(), 2);
    // 文件页也放一个"打开文件"（同一意图）。
    addSkeleton(QStringLiteral("file.open"), QStringLiteral("打开文件"),
                QStringLiteral("ribbon.group.file.document"), QStringLiteral("data.load"),
                QStringLiteral("file_open"), QStringLiteral("Ctrl+O"), 0);

    // "协议检查器"：消费 IMessageParser 自述逐帧看字段，按 id 特判弹窗 → 协议页"检查"组。
    addSkeleton(QStringLiteral("app.action.protocolInspector"), QStringLiteral("协议检查器"),
                QStringLiteral("ribbon.group.protocol.debug"), QString(),
                QStringLiteral("file_open"), QString(), 0);
    // "协议编辑器"：创建/编辑/校验 .psproto，按 id 特判弹窗 → 协议页"编辑器"组。
    addSkeleton(QStringLiteral("app.action.protocolEditor"), QStringLiteral("协议编辑器"),
                QStringLiteral("ribbon.group.protocol.editor"), QString(),
                QStringLiteral("file_open"), QString(), 0);

    // "切换主题"：GUI 展示态，声明 intentOp=theme.toggle → 视图页"外观"组。
    addSkeleton(QStringLiteral("app.action.toggleTheme"), QStringLiteral("切换主题"),
                QStringLiteral("ribbon.group.view.appearance"), QStringLiteral("theme.toggle"),
                QStringLiteral("theme_toggle"), QStringLiteral("Ctrl+T"), 0);

    // --- 文件页：四个组 + 组内顺序 ---
    // 文件组：打开(0) / 导入布局(1) / 设置(2)
    addSkeleton(QStringLiteral("file.importLayout"), QStringLiteral("导入布局"),
                QStringLiteral("ribbon.group.file.document"), QStringLiteral("layout.import"),
                QStringLiteral("import"), QString(), 1);
    addSkeleton(QStringLiteral("file.settings"), QStringLiteral("设置"),
                QStringLiteral("ribbon.group.file.document"), QStringLiteral("preferences.open"),
                QStringLiteral("settings"), QStringLiteral("Ctrl+,"), 2);
    // 工作区预设：整份"源清单 + 画布布局"存成 .psw，或按名字整体打开。
    // 需要选名字/输入名字的交互，外壳按 id 特判弹窗，仍然落到 workspace.preset.* 意图。
    addSkeleton(QStringLiteral("workspace.open"), QStringLiteral("打开工作区"),
                QStringLiteral("ribbon.group.file.document"), QString(),
                QStringLiteral("file_open"), QString(), 3);
    addSkeleton(QStringLiteral("workspace.save"), QStringLiteral("保存工作区"),
                QStringLiteral("ribbon.group.file.document"), QString(),
                QStringLiteral("export"), QString(), 4);
    // 导出组：保存布局(0) / 导出设置(1) / 导出数据(2)
    addSkeleton(QStringLiteral("file.saveLayout"), QStringLiteral("保存布局"),
                QStringLiteral("ribbon.group.file.export"), QStringLiteral("layout.save"),
                QStringLiteral("export"), QStringLiteral("Ctrl+S"), 0);
    addSkeleton(QStringLiteral("file.exportSettings"), QStringLiteral("导出设置"),
                QStringLiteral("ribbon.group.file.export"), QStringLiteral("settings.export"),
                QStringLiteral("export_cfg"), QString(), 1);
    addSkeleton(QStringLiteral("file.exportData"), QStringLiteral("导出数据"),
                QStringLiteral("ribbon.group.file.export"), QStringLiteral("data.export"),
                QStringLiteral("export"), QString(), 2);
    // 截图组（老设计独立一组）：截图(0)
    addSkeleton(QStringLiteral("file.screenshot"), QStringLiteral("截图"),
                QStringLiteral("ribbon.group.file.screenshot"), QStringLiteral("file.screenshot"),
                QStringLiteral("screenshot"), QString(), 0);
    // 帮助组：关于(0)
    addSkeleton(QStringLiteral("help.about"), QStringLiteral("关于"),
                QStringLiteral("ribbon.group.file.help"), QStringLiteral("help.about"),
                QStringLiteral("settings"), QString(), 0);

    // --- 数据页"常用"组前两项：快速打开 / 最近 ---
    // 快速打开与"打开数据"同为 data.load（走同一对话框/意图），最近走 data.recent。
    // 其余 重载/导出/清缓存 由 DataModule 贡献（order 10+）。
    addSkeleton(QStringLiteral("data.action.quickOpen"), QStringLiteral("快速打开"),
                QStringLiteral("ribbon.group.data.common"), QStringLiteral("data.load"),
                QStringLiteral("open"), QString(), 0);
    addSkeleton(QStringLiteral("data.action.recent"), QStringLiteral("最近"),
                QStringLiteral("ribbon.group.data.common"), QStringLiteral("data.recent"),
                QStringLiteral("history"), QString(), 1);
    // 数据页"调试"组曾复用协议检查器（与协议页的 app.action.protocolInspector 同 id，
    // 两处声明导致同一功能重复、且按钮被甩到页面最右）。检查器统一归协议页，
    // 这里不再重复声明；data.debug 空组自动不渲染。

    // --- 脚本页：运行脚本（选 .lua/.py 文件 → script.run 意图）---
    addSkeleton(QStringLiteral("app.action.runScript"), QStringLiteral("运行脚本"),
                QStringLiteral("ribbon.group.script.run"), QStringLiteral("script.run"),
                QStringLiteral("play"), QStringLiteral("Ctrl+F5"), 0);

    // --- 视图页：面板开关（**每个 dock 页签一个按钮**，点击出现/再点消失）---
    // 动作 id = "view.toggleDock." + 面板类型 id（稳定 pageKey，不随标题改动失效）；
    // 标签与页签标题一致。曾按区做整组开关，按钮名称与实际效果不符（历史缺陷）。
    static const struct { const char *type; const char *label; const char *icon; } kDockPageActions[] = {
        {"dataset-tree",   "数据源",     "datasource"},    // 左栏
        {"script-files",   "脚本文件",   "script"},        // 左栏
        {"property-panel", "属性",       "property"},      // 右栏
        {"script-debug",   "调试",       "script_debug"},  // 右栏
        {"script-console", "终端",       "terminal"},      // 底部
        {"output-console", "输出",       "output"},        // 底部
        {"problems",       "问题",       "problems"},      // 底部
        {"debug-console",  "调试控制台", "debug_console"}, // 底部
        {"diagnostics",    "诊断",       "diagnostics"},   // 底部（归并页签）
    };
    for (int i = 0; i < (int)(sizeof(kDockPageActions) / sizeof(kDockPageActions[0])); ++i)
    {
        addSkeleton(QStringLiteral("view.toggleDock.") + QLatin1String(kDockPageActions[i].type),
                    // label 是 UTF-8 中文，必须用 fromUtf8；QLatin1String 会按 Latin-1
                    // 逐字节解析，把中文显示成 æ°æ®æº 之类的乱码（历史缺陷）。
                    QString::fromUtf8(kDockPageActions[i].label),
                    QStringLiteral("ribbon.group.view.panels"), QString(),
                    QLatin1String(kDockPageActions[i].icon), QString(), i);
    }

    // --- 绘图页四组：面板划分与顺序 ---
    // 走 view action，由 PlotWorkspace 集中处理（多画布/网格）。intentOp 留空：
    // dispatcher 先按 action id 查 view action，命中即执行，与脚本同路径。
    struct PlotToolDef { const char *area; const char *id; const char *label; const char *icon;
                         int order; const char *data; bool large; bool checkable; };
    // 组划分与按钮尺寸按 main 的 PlottingRibbon：
    //   页面   新建页(大) / 删除页(大)
    //   布局   网格 m×n(大缩略) + 添加·删除·左右·上下(小，两行)
    //   显示   重绘(大) / 网格线(大，下拉选 X/Y) / 边框 / 图例（小，两行）
    //   交互   标记(大) / AB(大) + 放大·缩小·移动·恢复（小，两行 2+2）
    //   导出   截图 / 导出 PNG / 导出 CSV
    static const PlotToolDef kPlotTools[] = {
        // 页面组
        {"ribbon.group.plot.figure",   "figure.new",           "新建页", "newpage", 0, "",        true,  false},
        {"ribbon.group.plot.figure",   "figure.closeCurrent",  "删除页", "trash",   1, "",        true,  false},
        // 布局组（网格是 m×n 选择器：带缩略图的大按钮，点击弹行列选择）
        {"ribbon.group.plot.layout", "figure.grid",          "2×1",   "grid",    0, "pick",    true,  false},
        {"ribbon.group.plot.layout", "figure.addAxes",     "添加",   "plus",    1, "",        false, false},
        {"ribbon.group.plot.layout", "figure.removeAxes",  "删除",   "trash",   2, "",        false, false},
        {"ribbon.group.plot.layout", "figure.splitHorizontal","左右",  "split_h", 3, "",        false, false},
        {"ribbon.group.plot.layout", "figure.splitVertical", "上下",   "split_v", 4, "",        false, false},
        // 显示组：网格做成一个大按钮 + 下拉菜单（X 网格线 / Y 网格线），
        // 下拉项由 MainWindow 的 menu provider 挂到按钮上（SARibbon 显示下拉箭头）。
        {"ribbon.group.plot.display","plot.grid.menu",     "网格",   "grid_lines", 1, "",     true,  false},
        {"ribbon.group.plot.display","plot.box",           "边框",   "box",     2, "",        false, true},
        // 图例由 PlotModule 贡献（display 组 order 3）
        // 交互组：两个测量/标记大按钮 + 四个工具小按钮（两行 2+2）
        // 「标记」= 在所有画布同一时刻画一条竖直标记线（原「书签」改名）。
        {"ribbon.group.plot.tool",   "plot.bookmark.add",  "标记",   "marker",  0, "",        true,  false},
        {"ribbon.group.plot.tool",   "plot.ab.toggle",     "AB",     "ruler",   1, "",        true,  true},
        // SARibbon 两行模式按**列**填充（先上后下、再下一列），所以 order 交错写，
        // 视觉上才是"上排 放大·缩小 / 下排 移动·恢复"。
        {"ribbon.group.plot.tool",   "plot.tool",          "放大",   "zoom_in", 2, "{\"tool\":1}", false, true},
        {"ribbon.group.plot.tool",   "plot.tool",          "移动",   "hand",    3, "{\"tool\":0}", false, true},
        {"ribbon.group.plot.tool",   "plot.tool",          "缩小",   "zoom_out",4, "{\"tool\":2}", false, true},
        {"ribbon.group.plot.tool",   "plot.autoscale",     "恢复",   "fit",     5, "",        false, false},
        // 导出组
        {"ribbon.group.plot.action", "plot.screenshot",    "截图",   "screenshot", 0, "",     false, false},
    };
    // 布局组大按钮：main 用 layoutThumb(r,c) 画分格缩略图、文字写 "r×c"，
    // 点选后回写实际分格。初始按当前页默认 1×1，选完由下面 dispatcher 更新。
    QString layoutVisual = QStringLiteral("layout:2x1");
    for (const PlotToolDef &d : kPlotTools)
    {
        UiAction a;
        a.id = QString::fromLatin1(d.id);
        a.label = QString::fromUtf8(d.label);
        a.area = QString::fromLatin1(d.area);
        a.icon = QString::fromLatin1(d.icon);
        a.data = QString::fromLatin1(d.data);
        a.order = d.order;
        a.large = d.large;
        a.checkable = d.checkable;
        if (a.id == QStringLiteral("figure.grid"))
            a.visual = layoutVisual;
        slotReg.bindLocal(a.area, a, QStringLiteral("app.skeleton"));
    }
    // --- 图型库 ---
    // 12 种图型分"常用/曲线/地图3D/仪表"四组。每项都是纯 UiAction，
    // 点击走 view action "plot.setType"（int/dispatcher 里路由），
    // 与脚本/回放同一语义。缩略图由 visual 字段交给 GUI 层绘制。
    const QString plotArea = QStringLiteral("ribbon.group.plot.type");
    // 分类与组内顺序：常用=时间序列/XY/散点/3D轨迹/GPS，
    // 曲线=柱状/直方/频谱，地图/3D=3D曲面，仪表=地平仪/罗盘/表盘。
    // 一项只归属一个 galleryGroup，按主分类去重，共 12 项。
    struct PlotDef { const char *kind; const char *label; const char *group; int order; };
    // order 是全局序：十位=分类（常用0x/曲线1x/地图2x/仪表3x），个位=组内序。
    // 这样 actionsIn 按 order 排序后，gallery 分类出现顺序与组内顺序都正确。
    static const PlotDef kPlots[] = {
        {"timeseries", "时间序列", "常用",      0},
        {"xy",         "XY 曲线",  "常用",      1},
        {"scatter",    "散点图",   "常用",      2},
        {"traj3d",     "3D 轨迹",  "常用",      3},
        {"map",        "GPS 地图", "常用",      4},
        {"bar",        "柱状图",   "曲线",     10},
        {"histogram",  "直方图",   "曲线",     11},
        {"fft",        "频谱 FFT", "曲线",     12},
        {"surface3d",  "3D 曲面",  "地图 / 3D",20},
        {"horizon",    "地平仪",   "仪表",     30},
        {"compass",    "罗盘",     "仪表",     31},
        {"gauge",      "仪表盘",   "仪表",     32},
    };
    for (const PlotDef &d : kPlots)
    {
        UiAction a;
        const QString kind = QString::fromLatin1(d.kind);
        a.id = QStringLiteral("plot.type.") + kind;
        a.label = QString::fromUtf8(d.label);
        a.area = plotArea;
        a.intentOp = QStringLiteral("plot.setType");
        a.visual = kind;
        a.galleryGroup = QString::fromUtf8(d.group);
        a.data = kind; // 图型种类随动作携带，dispatcher 转成 view action 参数
        a.order = d.order;
        slotReg.bindLocal(plotArea, a, QStringLiteral("app.skeleton"));
    }

    // --- 来源库 ---
    // 由工具箱里已注册的格式驱动动态生成：每个驱动一项，放"文件格式"组；
    // 实时源放"实时源"组。点击走 data.source.<id> → 按该格式直接弹文件对话框。
    const QString srcArea = QStringLiteral("ribbon.group.data.sources");
    // gallery 分组出现顺序按动作 order 聚合：常用(0..3) 必须排在文件格式(100+) 之前，
    // 分组顺序："常用"→"文件格式"→"实时源"。
    int fmtOrder = 100;
    for (nextpilot::plotscope::parser::IMessageParser *driver : m_appCtx.toolbox().drivers())
    {
        UiAction a;
        a.id = QStringLiteral("data.source.") + driver->id();
        a.label = driver->displayName();
        a.area = srcArea;
        a.visual = driver->id();
        a.galleryGroup = QStringLiteral("文件格式");
        a.icon = QStringLiteral("file_open");
        a.order = fmtOrder++;
        slotReg.bindLocal(srcArea, a, QStringLiteral("app.skeleton"));
    }
    // 常用组：把前四个格式驱动也放进去。
    {
        int n = 0;
        for (nextpilot::plotscope::parser::IMessageParser *driver : m_appCtx.toolbox().drivers())
        {
            if (n >= 4)
                break;
            UiAction a;
            a.id = QStringLiteral("data.source.common.") + driver->id();
            a.label = driver->displayName();
            a.area = srcArea;
            a.visual = driver->id();
            a.galleryGroup = QStringLiteral("常用");
            a.icon = QStringLiteral("file_open");
            a.order = n;
            slotReg.bindLocal(srcArea, a, QStringLiteral("app.skeleton"));
            ++n;
        }
    }

    // 诊断归外壳：数据/脚本/分析由各自模块声明，绘图不进 Activity Bar。
    ActivitySpec diagnostics;
    diagnostics.id = QStringLiteral("diagnostics");
    diagnostics.label = QStringLiteral("诊断");
    diagnostics.icon = QStringLiteral("diagnostics");
    diagnostics.order = 3;
    diagnostics.ribbonPage = QStringLiteral("ribbon.page.view");
    diagnostics.provider = QStringLiteral("app.skeleton");
    slotReg.declareActivity(diagnostics);
}

void MainWindow::build(RuntimeHost &host)
{
    m_host = &host;
    UiSlotHub &slotReg = host.uiSlotsMutable();

    // 绑定器需要插槽表，构造时给。
    m_binder = new ShellBinder(this, slotReg);
    // Ribbon 上下文页签：binder 不认识命令库，求值器由外壳注入。
    // 用当前上下文快照求值；上下文一改，updateCommandContext 会触发重算。
    m_binder->setWhenEvaluator([this](const QString &when) {
        if (!m_commands)
            return true;
        return CommandRegistry::evaluateWhen(when, m_commands->contextValues());
    });
    m_context = new GuiContext(this, slotReg, m_binder, m_appCtx, &host, this);

    m_binder->setActivityActivatedHandler([this](const ActivitySpec &spec) {
        // Activity 的侧栏切换由 ShellBinder 完成；这里只处理“要打开哪个面板”。
        if (!spec.centralPanel.isEmpty()
            && m_context->requestPanel(QStringLiteral("canvas.center"), spec.centralPanel))
        {
            // 请求成功即已置前；没有额外业务动作。
        }
        if (spec.id == QStringLiteral("diagnostics"))
        {
            m_context->requestPanel(QStringLiteral("dock.diagnostics"),
                                    QStringLiteral("diagnostics"));
        }
    });
    m_binder->setSettingsHandler([this]() {
        Intent intent;
        intent.op = QStringLiteral("preferences.open");
        (void)m_host->execute(intent);
    });

    // 报告导出同路径：脚本 app.call("report.export", {path,...}) 走
    // 与 Ribbon 按钮（report.workbench.export 特判）完全相同的 QPdfWriter 管线。
    // path 必填；title/subtitle/notes/templateId 可选（templateId 查工具箱 reports 表）。
    m_appCtx.interactions().registerViewAction(
        QStringLiteral("report.export"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            gui::ReportRequest req;
            req.path = params.value(QStringLiteral("path")).toString();
            req.title = params.value(QStringLiteral("title")).toString();
            req.subtitle = params.value(QStringLiteral("subtitle")).toString();
            req.notes = params.value(QStringLiteral("notes")).toString();
            req.templateId = params.value(QStringLiteral("template")).toString();
            const auto r = gui::exportReportPdf(
                m_appCtx, m_binder->centralWidgets(), m_binder->centralTitles(), req);
            if (!r)
                return Result<QVariant>(r.error());
            return Result<QVariant>(QVariant(req.path));
        });

    // 1) 骨架插槽
    declareStandardSlots();

    // 2) ribbon（SARibbon）——结构层交给第三方，绑定器只提供插槽数据
    //
    // SARibbonMainWindow 已经在构造时把 ribbon 条装进了 menuWidget 槽
    // （见 setRibbonBar），这里只把它的**样式**调成我们要的样子，
    // 并把同一个对象交给绑定器去填内容。
    m_binder->createRibbon(ribbonBar());

    // 2.0) 「网格」大按钮的下拉：内容由外壳给（绑定器只负责挂到 SARibbon 按钮上）。
    //      X/Y 网格线各自开关，展开前按当前画布状态刷新勾选。
    m_binder->setActionMenuProvider([this](const QString &actionId) -> QMenu * {
        if (actionId != QStringLiteral("plot.grid.menu"))
            return nullptr;
        if (!m_gridMenu)
        {
            m_gridMenu = new QMenu(this);
            QObject::connect(m_gridMenu, &QMenu::aboutToShow, this, [this] { rebuildGridMenu(); });
        }
        return m_gridMenu;
    });

    // 2.1) 主题管理（系统级 GUI 能力，生命周期归宿主）。
    //    ThemeManager 只产出 token/色/QSS 并发 themeChanged；
    //    ribbon 换肤由绑定器订阅后套用——守 ARCHITECTURE §7"绑定器是唯一知道
    //    SARibbon/ADS 的地方"。注册为系统级 handler，theme.* 意图走与按钮/脚本
    //    同一条执行路径；headless 无此 handler，会诚实报"无模块能处理意图"。
    m_theme = new ThemeManager(host.app().settings(), &host);
    host.registerSystemHandler(QStringLiteral("@system/theme"), m_theme);
    QObject::connect(m_theme, &ThemeManager::themeChanged, this,
                     [this](ThemeManager::Theme theme) {
                         if (m_binder)
                             m_binder->applyTheme(theme == ThemeManager::Theme::Dark,
                                                  m_theme->ribbonStyleSheet(),
                                                  m_theme->dockStyleSheet());
                     });
    m_theme->applyCurrent(); // 把 settings 里的初始值落到全局 QSS 与 ribbon

    // 2.2) 「文件 → 设置」的落地 = 全局偏好对话框（App 作用域，落盘）。
    //   偏好界面是 GUI 专属能力，注册成**系统级 handler**（与 theme.* 同法），
    //   模块层（modules/settings）只管存取、不认识 QWidget：
    //   · ribbon 与脚本都发 preferences.open 意图，同一条执行路径；
    //   · headless 没有这个 handler → "没有模块能处理意图"，不假装成功。
    //   · 生命周期随窗口：模态对话框在 handler 内现构造现销毁。
    m_preferencesHandler = new PreferencesIntentHandler(m_appCtx, host, this);
    host.registerSystemHandler(QStringLiteral("@system/preferences"), m_preferencesHandler);

    // 3) 装模块（GUI 模式：会调用 onAttach，模块可自带界面入口）
    host.activate(QStringList(), m_context);

    // 3.0) 属性面板占 dock.right（替换原 CAN 面板的默认占用）。
    // 外壳只用**部件类型字符串**请求，不 include 任何 views/ 头文件——
    // 具体 QWidget 由视图层自注册（PS_REGISTER_PANEL("property-panel")）。
    // 属性面板是 PropertySchema 契约的通用视图：选中什么、显示什么全由
    // AppContext 的 Selection + PropertyProvider 决定，外壳不认识业务。
    if (m_context->requestPanel(QStringLiteral("dock.right"), QStringLiteral("property-panel")))
        statusBar()->showMessage(QStringLiteral("属性面板已就绪"));

    // 3.0b) 底部面板条（VS Code 式）：终端 → 输出 → 问题 → 调试控制台 → 诊断。
    //
    // 顺序是按 requestPanel 的调用次序固定的（ADS 往同一 dock area 尾部追加页签）：
    //   · 「终端」由 ScriptModule 在 onAttach 时请求（模块自带界面入口），
    //     此时已在坞底占首位，故这里从「输出」开始；
    //   · 「诊断」原先挂在右栏、按需创建，现在并入底部组——它内部仍是
    //     diag.registries 的注册树全景视图，只是位置变了，取数通道不变。
    // 四个面板共用 AppContext 消息流：输出收全量、问题只留 warn/error、
    // 调试控制台只收 level=debug，互不重复实现取数。
    const struct { const char *type; const char *area; } kBottomPanels[] = {
        {"output-console", "dock.bottom"},   // 全量消息流
        {"problems", "dock.bottom"},         // 只留 warn/error
        {"debug-console", "dock.bottom"},    // 调试事件 + 求值
        {"diagnostics", "dock.diagnostics"}, // 并入底部组，取数仍是 diag.registries
    };
    for (const auto &panel : kBottomPanels)
        m_context->requestPanel(QString::fromLatin1(panel.area),
                                QString::fromLatin1(panel.type));

    // 底部默认停在「终端」：ADS 每加一个页签都会把新的置为当前，于是最后装配的
    // 「诊断」会抢到选中态——底部面板条默认显示的应该是终端（可输入的那一个）。
    // 走 re-request 复用（与调试暂停聚焦面板同一条语义），不新增外壳专用接口。
    if (QWidget *terminal = m_context->requestPanel(QStringLiteral("dock.bottom"),
                                                    QStringLiteral("script-console")))
        m_binder->raisePanel(terminal);

    // 3.0c) Activity badge：诊断 activity 汇总 warn/error 条数。
    connect(&m_appCtx, &AppContext::messageAppended, this,
            [this](const QString &, const QString &, const QString &, const QString &) {
                refreshActivityBadges();
            });
    refreshActivityBadges();

    // 3.1) ribbon 动作 -> 意图：外壳只把动作 id 交出去，不解释语义。
    //
    // 意图由宿主执行（RuntimeHost 实现了 IIntentExecutor），
    // 于是 GUI 点击与 CLI/脚本执行走**同一条意图路径**。
    m_binder->setIntentDispatcher([&host, this](const QString &actionId,
                                                const QString &intentOp, const QString &actionData) {
        // 少数动作需要 GUI 交互（选文件），外壳按 **id** 特判；
        // 其余动作一律用 UiAction 声明的 **intentOp** 走意图层——与 CLI/脚本同一条路径。
        // 注意两者不可混用：id 是 UI 元素标识，intentOp 才是要发的意图 op。
        if (actionId == QStringLiteral("app.action.openData"))
        {
            openDataFile(host);
            return;
        }
        if (actionId == QStringLiteral("app.action.protocolInspector"))
        {
            openProtocolInspector();
            return;
        }
        // 工作区预设：需要输入/选择预设名，外壳按 id 特判，落盘与重建仍走意图层。
        if (actionId == QStringLiteral("workspace.open"))
        {
            openWorkspacePreset(host);
            return;
        }
        if (actionId == QStringLiteral("workspace.save"))
        {
            saveWorkspacePreset(host);
            return;
        }
        if (actionId == QStringLiteral("app.action.protocolEditor")
            || actionId == QStringLiteral("protocol.new")
            || actionId == QStringLiteral("protocol.open")
            || actionId == QStringLiteral("protocol.validate")
            || actionId == QStringLiteral("protocol.editor"))
        {
            openProtocolEditor();
            return;
        }
        // 报告导出：选保存路径 → QPdfWriter 矢量管线（画布+元数据+结论）。
        // Ribbon 按钮与脚本 app.call("report.export") 最终都汇聚到 exportReportPdf。
        if (actionId == QStringLiteral("report.workbench.export"))
        {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("导出报告"), QStringLiteral("report.pdf"),
                QStringLiteral("PDF (*.pdf);;全部 (*)"));
            if (path.isEmpty())
                return;
            gui::ReportRequest req;
            req.path = path;
            req.title = QStringLiteral("PlotScope 报告");
            const auto r = gui::exportReportPdf(
                m_appCtx, m_binder->centralWidgets(), m_binder->centralTitles(), req);
            statusBar()->showMessage(r ? QStringLiteral("报告已导出: %1").arg(path)
                                       : QStringLiteral("导出失败: %1").arg(r.error().message));
            return;
        }
        // 视图页面板开关：动作 id = "view.toggleDock." + 面板类型 id（pageKey）。
        // 语义是"一个按钮管一个页签"：点击一次出现、再点一次消失，只影响
        // 自己，不动同区其它页签。所有底部页签（含诊断）在装配期就已创建，
        // 不存在"首次点击才 requestPanel"的特判。
        if (actionId.startsWith(QStringLiteral("view.toggleDock.")))
        {
            const QString pageKey = actionId.mid(QStringLiteral("view.toggleDock.").size());
            const bool nowVisible = !m_binder->isDockPageVisible(pageKey);
            if (m_binder->setDockPageVisible(pageKey, nowVisible))
                statusBar()->showMessage(QStringLiteral("面板 %1: %2")
                                             .arg(m_binder->dockPageTitle(pageKey),
                                                  nowVisible ? QStringLiteral("显示")
                                                             : QStringLiteral("隐藏")));
            else
                statusBar()->showMessage(QStringLiteral("面板 %1 尚未创建").arg(pageKey));
            return;
        }
        // 运行脚本：选 .lua/.py 文件 → script.run 意图（与 CLI --run 同一路径）。
        if (actionId == QStringLiteral("app.action.runScript"))
        {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("运行脚本"), QString(),
                QStringLiteral("脚本 (*.lua *.py);;全部 (*)"));
            if (path.isEmpty())
                return;
            Intent script;
            script.op = QStringLiteral("script.run");
            script.params[QStringLiteral("path")] = path;
            const auto sr = host.execute(script);
            if (!sr)
                statusBar()->showMessage(QStringLiteral("脚本失败: %1").arg(sr.error().message));
            else
                statusBar()->showMessage(QStringLiteral("脚本已运行: %1").arg(QFileInfo(path).fileName()));
            return;
        }
        // 截图：抓当前窗口存 PNG（file.screenshot）。
        if (actionId == QStringLiteral("file.screenshot")
            || actionId == QStringLiteral("plot.screenshot"))
        {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("截图"), QStringLiteral("plotscope.png"),
                QStringLiteral("PNG 图片 (*.png)"));
            if (!path.isEmpty())
                grab().save(path);
            return;
        }
        // 导出设置：把当前 SettingsStore 的键值导出为 JSON（file.exportSettings）。
        if (actionId == QStringLiteral("file.exportSettings"))
        {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("导出设置"), QStringLiteral("plotscope-settings.json"),
                QStringLiteral("JSON (*.json)"));
            if (path.isEmpty())
                return;
            QJsonObject root;
            const QStringList keys = m_appCtx.settings().keys();
            for (const QString &k : keys)
                root.insert(k, QJsonValue::fromVariant(m_appCtx.settings().value(k)));
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)
                && f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) > 0)
                statusBar()->showMessage(QStringLiteral("设置已导出: %1").arg(QFileInfo(path).fileName()));
            else
                statusBar()->showMessage(QStringLiteral("导出设置失败"));
            return;
        }
        // 关于：版本与用途说明（help.about）。
        if (actionId == QStringLiteral("help.about"))
        {
            QMessageBox::about(this, QStringLiteral("关于 PlotScope"),
                QStringLiteral("PlotScope — 数据 / 绘图 / 分析软件\n"
                               "架构底座：Dataset / Intent / Contribution / Module / Pack\n"
                               "绘图库：fastplot（Figure / Axes / Line / Artist）"));
            return;
        }
        // 保存/导入布局：窗口状态写入/读出 Workspace 作用域 settings（layout.save/import）。
        if (actionId == QStringLiteral("file.saveLayout")
            || actionId == QStringLiteral("layout.save"))
        {
            saveState();
            statusBar()->showMessage(QStringLiteral("布局已保存"));
            return;
        }
        if (actionId == QStringLiteral("file.importLayout")
            || actionId == QStringLiteral("layout.import"))
        {
            restoreState();
            statusBar()->showMessage(QStringLiteral("布局已导入"));
            return;
        }

        // 图型库项：plot.type.<kind> → 切中央视图到对应图型（view action）。
        // 与脚本 `view plot.setType {type:kind}` 同一语义。
        if (actionId.startsWith(QStringLiteral("plot.type.")))
        {
            const QString kind = actionId.mid(QStringLiteral("plot.type.").size());
            QVariantMap p;
            p.insert(QStringLiteral("type"), kind);
            QVariant vr;
            Result<QVariant> res(vr);
            bool handled = false;
            if (m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.setType"), p, res) && res)
                handled = res.value().toMap().value(QStringLiteral("handled")).toBool();
            if (!handled)
            {
                // 仪表/地图/3D：切中央视图到专用仪表视图（一路 gallery → 两种画布）。
                // kind 映射到 InstrumentView 已有的能力；未接入的明确提示，不假装可用。
                static const QHash<QString, QString> kInstrumentKind = {
                    {QStringLiteral("gauge"), QStringLiteral("gauge")},
                    {QStringLiteral("compass"), QStringLiteral("compass")},
                    {QStringLiteral("horizon"), QStringLiteral("attitude")},
                    {QStringLiteral("traj3d"), QStringLiteral("traj3d")},
                    {QStringLiteral("map"), QStringLiteral("track")},
                    // 柱状/直方/频谱：fastplot 专用 Instrument widget，与仪表同视图。
                    {QStringLiteral("bar"), QStringLiteral("bar")},
                    {QStringLiteral("histogram"), QStringLiteral("histogram")},
                    {QStringLiteral("fft"), QStringLiteral("fft")},
                };
                const QString instrKind = kInstrumentKind.value(kind);
                if (!instrKind.isEmpty() && m_context)
                {
                    QWidget *w = m_context->requestPanel(QStringLiteral("canvas.center"),
                                                         QStringLiteral("plot-instrument"));
                    if (w)
                    {
                        QVariantMap ip;
                        ip.insert(QStringLiteral("kind"), instrKind);
                        QVariant ivr;
                        Result<QVariant> ires(ivr);
                        m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.instrument.setKind"), ip, ires);
                        statusBar()->showMessage(QStringLiteral("图型: %1").arg(kind));
                        return;
                    }
                }
                statusBar()->showMessage(QStringLiteral("图型「%1」待接入").arg(kind));
                return;
            }
            statusBar()->showMessage(QStringLiteral("图型: %1").arg(kind));
            return;
        }

        // 来源库项：data.source.<id> → 按该格式直接弹文件对话框（跳过格式选择），
        // 点加载器即弹对应格式对话框；仍走 data.load 意图。
        if (actionId.startsWith(QStringLiteral("data.source.")))
        {
            // common.<id> 与 <id> 都归一到真实驱动 id。
            QString id = actionId.mid(QStringLiteral("data.source.").size());
            if (id.startsWith(QStringLiteral("common.")))
                id = id.mid(QStringLiteral("common.").size());
            openDataWithFormat(host, id);
            return;
        }

        // 视图动作（导出 / 视图状态 / 图例）：先查视图在 AppContext 登记的处理器，
        // 命中就交视图执行；未注册再走意图层（CLI/脚本同路径）。
        // 动作的 data 携带参数（JSON 对象；非 JSON 纯字符串按 "application" 传入）。
        // view action 与意图层共用这份解析，保证按钮/图库/脚本同一语义。
        QVariantMap params;
        if (!actionData.isEmpty())
        {
            QJsonParseError perr;
            const QJsonDocument doc = QJsonDocument::fromJson(actionData.toUtf8(), &perr);
            if (perr.error == QJsonParseError::NoError && doc.isObject())
            {
                const QJsonObject obj = doc.object();
                for (auto it = obj.begin(); it != obj.end(); ++it)
                    params.insert(it.key(), it.value().toVariant());
            }
            else
                params.insert(QStringLiteral("application"), actionData);
        }
        if (actionId == QStringLiteral("plot.export.png")
            || actionId == QStringLiteral("plot.export.csv"))
        {
            const bool png = actionId == QStringLiteral("plot.export.png");
            const QString path = QFileDialog::getSaveFileName(
                this, png ? QStringLiteral("导出 PNG") : QStringLiteral("导出 CSV"),
                QString(), png ? QStringLiteral("PNG 图片 (*.png)") : QStringLiteral("CSV 表格 (*.csv)"));
            if (path.isEmpty())
                return; // 用户取消
            params[QStringLiteral("path")] = path;
        }
        QVariant nullVariant;
        Result<QVariant> viewResult(nullVariant);
        // 「网格」按钮（id 仍为 figure.grid 以满足契约）：data="pick" 时弹 m×n 选择器，
        // 无 data（脚本/回放直接调）时保留原固定网格行为。
        QString dispatchId =
            (actionId == QStringLiteral("figure.grid") && actionData == QStringLiteral("pick"))
                ? QStringLiteral("figure.grid.pick") : actionId;
        // 「显示」组把总网格开关拆成 X / Y 两个（plot.grid.x / .y）：参数在 data 里，
        // 归一到一个 view action 上，脚本仍可直接调 plot.grid {"axis":"x"}。
        if (actionId.startsWith(QStringLiteral("plot.grid.")))
            dispatchId = QStringLiteral("plot.grid");
        if (m_appCtx.interactions().dispatchViewAction(dispatchId, params, viewResult))
        {
            // 「网格」大按钮：回写缩略图与 "r×c" 文字（对齐 main buildLayoutPicker）。
            if (actionId == QStringLiteral("figure.grid") && viewResult)
            {
                const QVariantMap g = viewResult.value().toMap();
                const int gr = g.value(QStringLiteral("rows")).toInt();
                const int gc = g.value(QStringLiteral("cols")).toInt();
                if (gr > 0 && gc > 0)
                    m_binder->updateActionStyle(QStringLiteral("figure.grid"),
                                                PlotThumbnails::layout(gr, gc),
                                                QStringLiteral("%1×%2").arg(gr).arg(gc));
            }
            if (!viewResult)
                statusBar()->showMessage(QStringLiteral("动作失败: %1").arg(viewResult.error().message));
            return;
        }

        if (intentOp.isEmpty()) // 纯占位动作：没有意图可发，明确提示而非发空 op
        {
            statusBar()->showMessage(QStringLiteral("动作未声明意图: %1").arg(actionId));
            return;
        }
        Intent intent;
        intent.op = intentOp;
        intent.params = params;
        const Result<IntentResult> r = host.execute(intent);
        if (!r)
            statusBar()->showMessage(QStringLiteral("动作失败: %1").arg(r.error().message));
    });

    // 4) ribbon 重建：把工具箱的 UI 贡献 + 模块的本地入口一起渲染出来
    m_binder->rebuildRibbon();

    // 5) 命令层：统一命令、命令面板与键位（依赖 ribbon 动作已建好）
    buildCommandLayer(host);

    // 6) 恢复上次的窗口状态（首次运行则安全跳过）
    restoreState();
}

// 「网格」下拉：X 网格线 / Y 网格线 各自独立开关，展开时按活动画布现状勾选。
void MainWindow::rebuildGridMenu()
{
    if (!m_gridMenu)
        return;
    m_gridMenu->clear();

    QVariantMap state;
    QVariant nullV;
    Result<QVariant> qr(nullV);
    if (m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.grid.query"), QVariantMap(), qr)
        && qr)
        state = qr.value().toMap();

    auto addToggle = [this, &state](const QString &label, const QString &axis) {
        const bool on = state.value(axis).toBool();
        QAction *a = m_gridMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(on);
        QObject::connect(a, &QAction::triggered, this, [this, axis, on] {
            QVariantMap p;
            p.insert(QStringLiteral("axis"), axis);
            p.insert(QStringLiteral("on"), !on);
            QVariant nullV;
            Result<QVariant> r(nullV);
            if (!m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.grid"), p, r))
                statusBar()->showMessage(QStringLiteral("网格线切换失败"));
        });
    };
    addToggle(QStringLiteral("X 网格线"), QStringLiteral("x"));
    addToggle(QStringLiteral("Y 网格线"), QStringLiteral("y"));
    m_gridMenu->addSeparator();
    QAction *off = m_gridMenu->addAction(QStringLiteral("全部关闭"));
    QObject::connect(off, &QAction::triggered, this, [this] {
        QVariantMap p;
        p.insert(QStringLiteral("on"), false);
        QVariant nullV;
        Result<QVariant> r(nullV);
        if (!m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.grid"), p, r))
            statusBar()->showMessage(QStringLiteral("网格线关闭失败"));
    });
}

void MainWindow::openDataFile(RuntimeHost &host)
{
    // 通用导入对话框（schema 驱动）：选格式 + 选文件 + 配置解析选项 + 预览。
    // 对话框只负责收集"怎么做"，真正加载仍走 data.load 意图——与 CLI/脚本同路径。
    const ImportDialog::Result picked = ImportDialog::run(m_appCtx.toolbox(), this);
    if (!picked.ok())
        return; // 用户取消

    Intent intent;
    intent.op = QStringLiteral("data.load");
    intent.params[QStringLiteral("path")] = picked.path;
    if (!picked.format.isEmpty())
        intent.params[QStringLiteral("format")] = picked.format;
    if (!picked.optionsJson.isEmpty())
        intent.params[QStringLiteral("options")] = QString::fromUtf8(picked.optionsJson);
    const Result<IntentResult> r = host.execute(intent);
    if (!r)
    {
        statusBar()->showMessage(QStringLiteral("载入失败: %1").arg(r.error().message));
        return;
    }
    const int dsId = r.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
    statusBar()->showMessage(QStringLiteral("已载入数据集 #%1").arg(dsId));
}

void MainWindow::saveWorkspacePreset(RuntimeHost &host)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("保存工作区"),
                                               QStringLiteral("预设名（源清单 + 画布布局）"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;

    Intent intent;
    intent.op = QStringLiteral("workspace.preset.save");
    intent.params[QStringLiteral("name")] = name;
    const Result<IntentResult> r = host.execute(intent);
    if (!r)
    {
        statusBar()->showMessage(QStringLiteral("保存失败: %1").arg(r.error().message));
        return;
    }
    const QVariantMap payload = r.value().payload.toMap();
    statusBar()->showMessage(QStringLiteral("已保存工作区 %1（源 %2，页 %3）")
                                 .arg(name)
                                 .arg(payload.value(QStringLiteral("sources")).toInt())
                                 .arg(payload.value(QStringLiteral("pages")).toInt()));
}

void MainWindow::openWorkspacePreset(RuntimeHost &host)
{
    // 先问有哪些预设（同一份目录由模块扫），再让用户选，最后按名整体打开。
    Intent listIntent;
    listIntent.op = QStringLiteral("workspace.preset.list");
    const Result<IntentResult> listed = host.execute(listIntent);
    if (!listed)
    {
        statusBar()->showMessage(QStringLiteral("读取预设列表失败: %1").arg(listed.error().message));
        return;
    }
    QStringList names;
    for (const QJsonValue &v : listed.value().payload.toJsonArray())
        names.append(v.toObject().value(QStringLiteral("name")).toString());
    if (names.isEmpty())
    {
        statusBar()->showMessage(QStringLiteral("还没有保存过工作区"));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getItem(this, QStringLiteral("打开工作区"),
                                               QStringLiteral("选择预设"), names, 0, false, &ok);
    if (!ok || name.isEmpty())
        return;

    Intent intent;
    intent.op = QStringLiteral("workspace.preset.open");
    intent.params[QStringLiteral("name")] = name;
    const Result<IntentResult> r = host.execute(intent);
    if (!r)
    {
        statusBar()->showMessage(QStringLiteral("打开失败: %1").arg(r.error().message));
        return;
    }
    statusBar()->showMessage(QStringLiteral("已打开工作区 %1").arg(name));
}

void MainWindow::openDataWithFormat(RuntimeHost &host, const QString &formatId)
{
    // 点图库里的加载器，直接弹该格式的文件对话框。
    auto *driver = m_appCtx.toolbox().driverForFormat(formatId);
    QString filter = QStringLiteral("全部 (*)");
    if (driver)
    {
        const QStringList suf = driver->suffixes();
        if (!suf.isEmpty())
        {
            QStringList patterns;
            for (const QString &s : suf)
                patterns << QStringLiteral("*.") + s;
            filter = driver->displayName() + QStringLiteral(" (") + patterns.join(QLatin1Char(' '))
                     + QLatin1Char(')') + QStringLiteral(";;全部 (*)");
        }
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("打开 %1").arg(driver ? driver->displayName() : formatId),
        QString(), filter);
    for (const QString &path : files)
    {
        Intent intent;
        intent.op = QStringLiteral("data.load");
        intent.params[QStringLiteral("path")] = path;
        intent.params[QStringLiteral("format")] = formatId;
        const Result<IntentResult> r = host.execute(intent);
        if (!r)
        {
            statusBar()->showMessage(QStringLiteral("载入失败: %1").arg(r.error().message));
            return;
        }
        const int dsId = r.value().payload.toMap().value(QStringLiteral("dataset")).toInt();
        statusBar()->showMessage(QStringLiteral("已载入数据集 #%1").arg(dsId));
    }
}

void MainWindow::openProtocolInspector()
{
    // 专用协议检查对话框：消费工具箱里 IMessageParser 的自述信息。
    // 与 openDataFile 的区别：这里产的是可读字段帧（检查用），不是 Dataset（画图用）。
    ProtocolInspectorDialog::run(m_appCtx.toolbox(), this);
}

void MainWindow::openProtocolEditor()
{
    // 协议编辑器：编辑 .psproto 模板 + 校验 + 试算。
    // 校验/试算走 protocol.* 意图（m_host 即 RuntimeHost，同时实现 IIntentExecutor），
    // 与脚本、CLI 用的是同一份实现；appCtx 供 checksum 变换在线调试。
    ProtocolEditorDialog::run(this, &m_appCtx, m_host);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    writeSessionSnapshot();
    QMainWindow::closeEvent(event);
}

void MainWindow::writeSessionSnapshot()
{
    // 快照 = config 段（源清单 + 画布布局，恢复数据现场的唯一依据）
    //      + session 段（各面板自述的运行态，hub 按登记序收集）。
    const QJsonObject collected = m_appCtx.states().collect();

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("kind"), QStringLiteral("session"));
    root.insert(QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QJsonObject config;
    WorkspaceDataConfig cfg = WorkspaceDataConfig::load(m_appCtx.settings());
    config.insert(QStringLiteral("sources"),
                  cfg.toJson().value(QStringLiteral("sources")).toArray());
    config.insert(QStringLiteral("plots"),
                  collected.value(QStringLiteral("document"))
                      .toObject()
                      .value(QStringLiteral("plot.document"))
                      .toObject());
    root.insert(QStringLiteral("config"), config);
    root.insert(QStringLiteral("session"), collected.value(QStringLiteral("session")).toObject());

    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .filePath(QStringLiteral("session"));
    QDir().mkpath(dir);
    // QSaveFile：写临时文件再原子替换，崩溃不会留下半截快照。
    QSaveFile file(QDir(dir).filePath(QStringLiteral("last-session")));
    if (!file.open(QIODevice::WriteOnly))
    {
        statusBar()->showMessage(QStringLiteral("会话快照写入失败: %1").arg(file.errorString()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        statusBar()->showMessage(QStringLiteral("会话快照提交失败: %1").arg(file.errorString()));
}

void MainWindow::restoreSessionSnapshot()
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .filePath(QStringLiteral("session"));
    QFile file(QDir(dir).filePath(QStringLiteral("last-session")));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (!doc.isObject())
    {
        // 快照损坏：整份丢弃照常启动——恢复是锦上添花，不能挡在启动路径上。
        statusBar()->showMessage(QStringLiteral("会话快照损坏，已跳过恢复"), 5000);
        return;
    }

    // 档位（app/startup.recovery，默认 layout）：
    //   off    什么都不恢复；
    //   layout 只还原源条目与画布，不读数据——启动不被大文件拖住；
    //   full   layout + 真正摄取（data.workspace.restore）。
    // 流式源在任何档位都只保留条目：连接要用户显式放行。
    const QString mode = m_appCtx.settings()
                             .value(QStringLiteral("app/startup.recovery"),
                                    QStringLiteral("layout"))
                             .toString();
    if (mode == QStringLiteral("off"))
        return;

    const QJsonObject root = doc.object();
    const QJsonObject config = root.value(QStringLiteral("config")).toObject();

    const QJsonArray sources = config.value(QStringLiteral("sources")).toArray();
    if (!sources.isEmpty())
    {
        QJsonObject sourcesDoc;
        sourcesDoc.insert(QStringLiteral("schemaVersion"), WorkspaceDataConfig::kSchemaVersion);
        sourcesDoc.insert(QStringLiteral("sources"), sources);
        WorkspaceDataConfig cfg = WorkspaceDataConfig::fromJson(sourcesDoc);
        QString err;
        if (cfg.save(m_appCtx.settings(), &err) && mode == QStringLiteral("full") && m_host != nullptr)
        {
            Intent restore;
            restore.op = QStringLiteral("data.workspace.restore");
            restore.params[QStringLiteral("includeLive")] = false;
            m_host->execute(restore);
        }
    }

    // 布局与面板会话态一起交给 hub：document 段只放 plot.document，其余在 session 段。
    QJsonObject document;
    document.insert(QStringLiteral("plot.document"), config.value(QStringLiteral("plots")).toObject());
    QJsonObject hubDoc;
    hubDoc.insert(QStringLiteral("document"), document);
    hubDoc.insert(QStringLiteral("session"),
                  root.value(QStringLiteral("session")).toObject());
    m_appCtx.states().restore(hubDoc);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    // 首次显示后按真实宽度重建 ribbon 内容。
    //
    // SARibbon 把面板按 ribbon 的当时宽度算几何：窗口显示前 ribbon 还没被
    // QMainWindow 布局，宽度是 QWidget 默认的 100px，所以内容全挤在
    // 左侧一小块里；窗口拉宽后 SARibbon 也不会自动重排（它只在窗口
    // **激活**时重排，而激活事件要么早于内容填充、要么在无显示环境里
    // 根本不存在）。
    //
    // 所以：内容先按"有什么"建好（插槽数据驱动），几何在窗口拿到真实
    // 宽度之后再重算一次。
    if (!m_ribbonLaidOut && m_binder)
    {
        m_ribbonLaidOut = true;
        m_binder->rebuildRibbon();
        // ribbon 页签：构造期只登记了目标页（那时页签栏没就位，切了也落空），
        // 这里窗口已显示、内容已按真实宽度重建，才真正切过去。
        m_binder->applyPendingRibbonPage();
        // ribbing 重排之后窗口才有真实宽度：同一帧末把 dock 比例定下来
        // （左右收窄、中央拿大头），之后不再干预以保留用户拖动。
        QTimer::singleShot(300, this, [this]() {
            if (!m_binder)
                return;
            // 有上次保存的布局就尊重它：adsLayout 已恢复，比例在里面；此时再套
            // 默认 72:28 会把用户上次拖动的比例抹掉。只有首次运行才用默认比例。
            if (!hasSavedState())
                m_binder->applyDockProportions();
            // 兜底再试一次 ribbon 页：首帧里页签栏偶尔还没完成布局，
            // 前面那次可能没切成（切成功则这里是空操作）。
            m_binder->applyPendingRibbonPage();
            // 初始面板显式指定为「数据源」：左栏两个页签谁先装配不该决定默认显示
            // （实测新增扩展包换了链接顺序后，启动落在「脚本文件」）。
            // 延时 300ms：ADS 的 tab 插入与布局收尾都在首帧之后，0ms 抢不过它。
            const bool ok = m_binder->focusPanelByTitle(QStringLiteral("dock.left"),
                                                        QStringLiteral("数据源"));
            std::fprintf(stderr, "[dock] initial panel focus(data)=%d\n", ok ? 1 : 0);
        });
    }
    // 首次显示后才恢复会话：此时各面板（含画布工作区）已创建，条目的 restore 才有落点。
    // 只跑一次——重新显示窗口不该二次恢复，把用户当前的操作覆盖回去。
    if (!m_sessionRestored)
    {
        m_sessionRestored = true;
        restoreSessionSnapshot();
    }
}

void MainWindow::applyWorkspaceUiConfig()
{
    if (!m_binder || !m_host)
        return;
    auto &settings = m_host->app().settings();

    const QVariant order = settings.scopedValue(SettingsScope::Workspace,
                                                QStringLiteral("ui.activityBar.order"));
    if (order.isValid())
        m_binder->setActivityOrder(order.toStringList());

    const QVariant activityVisible = settings.scopedValue(
        SettingsScope::Workspace, QStringLiteral("ui.activityBar.visible"));
    if (activityVisible.isValid())
        m_binder->setActivityBarVisible(activityVisible.toBool());

    const QVariant sideVisible = settings.scopedValue(
        SettingsScope::Workspace, QStringLiteral("ui.sideBar.visible"));
    if (sideVisible.isValid())
        m_binder->setSideBarVisible(sideVisible.toBool());

    const QVariant sideWidth = settings.scopedValue(
        SettingsScope::Workspace, QStringLiteral("ui.sideBar.width"));
    if (sideWidth.isValid())
        m_binder->setSideBarWidth(sideWidth.toInt());
}

void MainWindow::saveWorkspaceUiConfig()
{
    if (!m_binder || !m_host)
        return;
    auto &settings = m_host->app().settings();
    settings.setScopedValue(SettingsScope::Workspace, QStringLiteral("ui.activityBar.order"),
                            m_binder->activityIds());
    settings.setScopedValue(SettingsScope::Workspace, QStringLiteral("ui.activityBar.visible"),
                            m_binder->activityBarVisible());
    settings.setScopedValue(SettingsScope::Workspace, QStringLiteral("ui.sideBar.visible"),
                            m_binder->sideBarVisible());
    settings.setScopedValue(SettingsScope::Workspace, QStringLiteral("ui.sideBar.width"),
                            m_binder->sideBarWidth());
    saveKeybindingConfig();
}

void MainWindow::refreshActivityBadges()
{
    if (!m_binder)
        return;
    int problems = 0;
    for (const auto &message : m_appCtx.messages().all())
        if (message.level == QStringLiteral("warn") || message.level == QStringLiteral("error"))
            ++problems;
    m_binder->setActivityBadge(QStringLiteral("diagnostics"), problems);
}

void MainWindow::toggleSideBar()
{
    if (!m_binder)
        return;
    const bool visible = m_binder->toggleSideBar();
    statusBar()->showMessage(visible ? QStringLiteral("侧栏已显示")
                                    : QStringLiteral("侧栏已隐藏"));
}

void MainWindow::buildCommandLayer(RuntimeHost &host)
{
    Q_UNUSED(host);
    if (m_commands || !m_binder)
        return;

    m_commands = std::make_unique<CommandRegistry>();
    m_keybindings = std::make_unique<KeybindingRegistry>();

    m_commandPalette = new CommandPalette(this);
    m_commandPalette->setActivationHandler([this](const QString &) {
        // MRU 与关闭由 Palette 自己维护；这里留作后续埋点。
    });

    registerSlotCommands();
    registerShellCommands();

    struct DefaultBinding { const char *id; const char *sequence; };
    static const DefaultBinding kDefaults[] = {
        {"palette.show", "Ctrl+Shift+P"},
        {"quick.switch", "Ctrl+P"},
        {"view.toggleSideBar", "Ctrl+B"},
        {"view.toggleAuxZone", "Ctrl+Alt+B"},
        {"view.togglePanel", "Ctrl+J"},
        {"editor.next", "Ctrl+Tab"},
        {"editor.close", "Ctrl+W"},
    };
    for (const DefaultBinding &binding : kDefaults)
        m_keybindings->setDefault(QString::fromLatin1(binding.id),
                                  QString::fromLatin1(binding.sequence));

    const QStringList activities = m_binder->activityIds();
    for (int i = 0; i < activities.size() && i < 9; ++i)
        m_keybindings->setDefault(QStringLiteral("activity.activate.%1").arg(activities.at(i)),
                                  QStringLiteral("Ctrl+%1").arg(i + 1));

    applyKeybindingConfig();
    rebuildKeybindingActions();
    qApp->installEventFilter(this);
}

void MainWindow::registerSlotCommands()
{
    QSet<QString> seen;
    int added = 0;
    for (const UiSlot &slot : m_context->slotHub().allSlots())
    {
        for (const BoundUiAction &bound : m_context->slotHub().actionsIn(slot.area))
        {
            const UiAction &ua = bound.action;
            if (ua.id.isEmpty() || seen.contains(ua.id))
                continue;
            seen.insert(ua.id);

            CommandDescriptor command;
            command.id = ua.id;
            command.title = ua.label;
            command.category = slot.label;
            command.icon = ua.icon;
            command.shortcut = ua.shortcut;
            command.provider = bound.provider;
            const QString actionId = ua.id;
            const QString intentOp = ua.intentOp;
            const QString data = ua.data;
            command.handler = [this, actionId, intentOp, data]() {
                if (!m_binder->dispatchAction(actionId, intentOp, data))
                    statusBar()->showMessage(QStringLiteral("命令无法执行: %1").arg(actionId));
            };
            if (m_commands->add(command))
                ++added;
        }
    }
    fprintf(stdout, "[cmd] 注册动作命令 %d 条\n", added);
}

void MainWindow::registerShellCommands()
{
    auto add = [this](const QString &id, const QString &title, const QString &category,
                      const QString &shortcut, const QString &when,
                      std::function<void()> handler) {
        CommandDescriptor command;
        command.id = id;
        command.title = title;
        command.category = category;
        command.shortcut = shortcut;
        command.when = when;
        command.provider = QStringLiteral("app.shell");
        command.handler = std::move(handler);
        return m_commands->add(command);
    };

    add(QStringLiteral("palette.show"), QStringLiteral("命令面板"), QStringLiteral("视图"),
        QStringLiteral("Ctrl+Shift+P"), QString(),
        [this]() { showCommandPalette(); });
    add(QStringLiteral("quick.switch"), QStringLiteral("快速切换"), QStringLiteral("导航"),
        QStringLiteral("Ctrl+P"), QString(),
        [this]() { showQuickSwitch(); });
    add(QStringLiteral("view.toggleSideBar"), QStringLiteral("显示/隐藏左栏"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+B"), QString(),
        [this]() { toggleSideBar(); });
    add(QStringLiteral("view.toggleAuxZone"), QStringLiteral("显示/隐藏辅助栏"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+Alt+B"), QString(),
        [this]() {
            if (m_binder->toggleAuxZone())
                statusBar()->showMessage(m_binder->auxZoneVisible()
                                             ? QStringLiteral("辅助栏已显示")
                                             : QStringLiteral("辅助栏已隐藏"));
        });
    add(QStringLiteral("view.togglePanel"), QStringLiteral("显示/隐藏底部面板"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+J"), QString(),
        [this]() {
            if (m_binder->toggleBottomPanel())
                statusBar()->showMessage(m_binder->bottomPanelVisible()
                                             ? QStringLiteral("底部面板已显示")
                                             : QStringLiteral("底部面板已隐藏"));
        });
    add(QStringLiteral("editor.next"), QStringLiteral("下一个编辑器"),
        QStringLiteral("导航"), QStringLiteral("Ctrl+Tab"), QString(),
        [this]() {
            const int count = m_binder->centralWidgets().size();
            if (count <= 0)
                return;
            const int current = qMax(0, m_binder->currentCentralIndex());
            m_binder->activateCentralIndex((current + 1) % count);
        });
    add(QStringLiteral("editor.close"), QStringLiteral("关闭当前编辑器"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+W"), QString(),
        [this]() {
            if (!m_binder->closeCurrentEditor())
                statusBar()->showMessage(QStringLiteral("当前页签不可关闭"));
        });
    add(QStringLiteral("plot.series.removeSelected"), QStringLiteral("删除曲线"),
        QStringLiteral("绘图"), QString(),
        QStringLiteral("hasSelection == true && selectionKind == \"series\""),
        [this]() {
            const Selection &selection = m_appCtx.selection();
            const QString key = selection.ids.value(QStringLiteral("seriesKey")).toString();
            if (selection.kind != SelectionKind::Series || key.isEmpty())
            {
                statusBar()->showMessage(QStringLiteral("未选中曲线"));
                return;
            }
            QVariantMap params;
            params.insert(QStringLiteral("key"), key);
            QVariant nullValue;
            Result<QVariant> result(nullValue);
            if (!m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.series.remove"),
                                                            params, result)
                || !result)
                statusBar()->showMessage(QStringLiteral("删除曲线失败"));
        });
    add(QStringLiteral("preferences.keybindings"), QStringLiteral("快捷键设置"),
        QStringLiteral("设置"), QString(), QString(),
        [this]() { openKeybindingDialog(); });

    const QStringList activities = m_binder->activityIds();
    for (const QString &activityId : activities)
    {
        const ActivitySpec *spec = m_context->slotHub().activity(activityId);
        const QString label = spec ? spec->label : activityId;
        add(QStringLiteral("activity.activate.%1").arg(activityId),
            QStringLiteral("转到 %1").arg(label), QStringLiteral("导航"), QString(), QString(),
            [this, activityId]() { m_binder->setActiveActivity(activityId); });
    }
}

void MainWindow::updateCommandContext()
{
    if (!m_commands)
        return;
    m_commands->setContextValue(QStringLiteral("activeActivity"), m_binder->activeActivity());
    m_commands->setContextValue(QStringLiteral("activeEditor"), m_binder->currentCentralTitle());
    m_commands->setContextValue(QStringLiteral("activeGroup"), QString());
    const Selection &selection = m_appCtx.selection();
    m_commands->setContextValue(QStringLiteral("selectionKind"), selectionKindName(selection.kind));
    m_commands->setContextValue(QStringLiteral("hasSelection"), !selection.empty());
    m_commands->setContextValue(QStringLiteral("hasData"), !m_appCtx.data().ids().isEmpty());
    m_commands->setContextValue(QStringLiteral("isScriptRunning"), false);
    m_commands->setContextValue(QStringLiteral("focusArea"), QStringLiteral("editor"));
    // 上下文变了，Ribbon 上下文页签要跟着收起/展开（只在可见集合变化时重建）。
    if (m_binder)
        m_binder->applyWhenVisibility();
}

QList<CommandPalette::Item> MainWindow::buildCommandItems()
{
    QList<CommandPalette::Item> items;
    if (!m_commands || !m_keybindings)
        return items;

    QHash<QString, QStringList> conflicts;
    for (const KeybindingConflict &conflict : m_keybindings->conflicts())
        for (const QString &id : conflict.commandIds)
            conflicts[id] << conflict.sequence;

    for (const CommandDescriptor &command : m_commands->commands())
    {
        CommandPalette::Item item;
        item.id = command.id;
        item.title = command.title;
        item.category = command.category;
        item.keywords = command.id;
        item.shortcut = m_keybindings->sequenceFor(command.id);
        if (conflicts.contains(command.id))
            item.shortcut += QStringLiteral(" (冲突)");

        if (!command.when.isEmpty())
        {
            bool ok = true;
            if (!CommandRegistry::evaluateWhen(command.when, m_commands->contextValues(), &ok) || !ok)
                item.reason = QStringLiteral("条件不满足：%1").arg(command.when);
        }
        if (item.reason.isEmpty() && command.disabledReason)
            item.reason = command.disabledReason();

        const QString id = command.id;
        item.action = [this, id]() { (void)executeCommand(id); };
        items.append(item);
    }
    return items;
}

QList<CommandPalette::Item> MainWindow::buildQuickItems() const
{
    QList<CommandPalette::Item> items;

    const QList<QWidget *> central = m_binder->centralWidgets();
    const QStringList centralTitles = m_binder->centralTitles();
    for (int i = 0; i < central.size(); ++i)
    {
        CommandPalette::Item item;
        item.id = QStringLiteral("quick.editor.%1").arg(i);
        item.title = centralTitles.value(i, QStringLiteral("编辑器 %1").arg(i + 1));
        item.category = QStringLiteral("编辑器");
        item.keywords = QStringLiteral("editor central %1").arg(i);
        const int index = i;
        item.action = [this, index]() { m_binder->activateCentralIndex(index); };
        items.append(item);
    }

    for (const QString &area : {QStringLiteral("dock.left"), QStringLiteral("dock.right"),
                                QStringLiteral("dock.bottom")})
    {
        const QStringList titles = m_binder->panelTitles(area);
        const QList<QWidget *> panels = m_binder->panelWidgets(area);
        const int count = qMin(titles.size(), panels.size());
        for (int i = 0; i < count; ++i)
        {
            CommandPalette::Item item;
            item.id = QStringLiteral("quick.panel.%1.%2").arg(area, QString::number(i));
            item.title = titles.at(i);
            item.category = area == QStringLiteral("dock.left")
                                ? QStringLiteral("左栏面板")
                                : area == QStringLiteral("dock.right")
                                      ? QStringLiteral("右侧面板")
                                      : QStringLiteral("底部面板");
            item.keywords = area;
            QWidget *panel = panels.at(i);
            item.action = [this, panel]() { m_binder->focusPanel(panel); };
            items.append(item);
        }
    }

    for (const QString &activityId : m_binder->activityIds())
    {
        const ActivitySpec *spec = m_context->slotHub().activity(activityId);
        CommandPalette::Item item;
        item.id = QStringLiteral("quick.activity.%1").arg(activityId);
        item.title = spec ? spec->label : activityId;
        item.category = QStringLiteral("Activity");
        item.keywords = activityId;
        item.action = [this, activityId]() { m_binder->setActiveActivity(activityId); };
        items.append(item);
    }
    return items;
}

void MainWindow::showCommandPalette()
{
    if (!m_commandPalette)
        return;
    updateCommandContext();
    m_commandPalette->openCommands(buildCommandItems());
}

void MainWindow::showQuickSwitch()
{
    if (!m_commandPalette)
        return;
    updateCommandContext();
    m_commandPalette->openQuickSwitch(buildQuickItems());
}

bool MainWindow::executeCommand(const QString &id, QString *error)
{
    if (!m_commands)
    {
        if (error)
            *error = QStringLiteral("命令层尚未初始化");
        return false;
    }
    updateCommandContext();
    QString reason;
    const bool ok = m_commands->execute(id, &reason);
    if (!ok && error)
        *error = reason;
    if (!ok && !reason.isEmpty())
        statusBar()->showMessage(reason);
    return ok;
}

void MainWindow::openKeybindingDialog()
{
    if (!m_commands || !m_keybindings)
        return;
    auto *dialog = new KeybindingDialog(*m_commands, *m_keybindings, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setSaveHandler([this]() {
        saveKeybindingConfig();
        rebuildKeybindingActions();
    });
    dialog->exec();
}

void MainWindow::applyKeybindingConfig()
{
    if (!m_keybindings || !m_host)
        return;
    const QVariant raw = m_host->app().settings().scopedValue(
        SettingsScope::Workspace, QStringLiteral("ui.keybindings"));
    QJsonObject object = QJsonDocument::fromJson(raw.toString().toUtf8()).object();
    QHash<QString, QString> overrides;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        overrides.insert(it.key(), it.value().toString());
    m_keybindings->setOverrides(overrides);
}

void MainWindow::saveKeybindingConfig()
{
    if (!m_keybindings || !m_host)
        return;
    QJsonObject object;
    const QHash<QString, QString> overrides = m_keybindings->overrides();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
        object.insert(it.key(), it.value());
    m_host->app().settings().setScopedValue(
        SettingsScope::Workspace, QStringLiteral("ui.keybindings"),
        QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void MainWindow::rebuildKeybindingActions()
{
    if (!m_keybindings)
        return;
    auto oldActions = m_keybindingActions;
    m_keybindingActions.clear();
    for (QAction *action : oldActions)
    {
        removeAction(action);
        action->deleteLater();
    }

    QSet<QString> used;
    for (QAction *action : findChildren<QAction *>())
    {
        if (oldActions.contains(action) || action->shortcut().isEmpty())
            continue;
        used.insert(action->shortcut().toString(QKeySequence::PortableText));
    }

    const QStringList ids = m_keybindings->commandIds();
    for (const QString &id : ids)
    {
        if (!m_commands->contains(id))
            continue;
        const QString sequence = m_keybindings->sequenceFor(id);
        if (sequence.isEmpty() || KeybindingRegistry::isChord(sequence))
            continue;
        // Ctrl+Tab / Ctrl+W 在脚本编辑器内有面板级快捷键，交给 eventFilter 分流。
        if (id == QStringLiteral("editor.next") || id == QStringLiteral("editor.close"))
            continue;
        if (used.contains(sequence))
            continue; // 冲突的第二个绑定不注册；Palette 会标“(冲突)”
        used.insert(sequence);

        auto *action = new QAction(this);
        action->setObjectName(QStringLiteral("command.action.%1").arg(id));
        action->setShortcut(QKeySequence(sequence));
        action->setShortcutContext(Qt::WindowShortcut);
        addAction(action);
        connect(action, &QAction::triggered, this, [this, id]() { (void)executeCommand(id); });
        m_keybindingActions.append(action);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if ((event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        || !m_keybindings)
        return QMainWindow::eventFilter(watched, event);

    auto *key = static_cast<QKeyEvent *>(event);
    const QString sequence = QKeySequence(key->keyCombination())
                                 .toString(QKeySequence::PortableText);
    if (sequence.isEmpty())
        return QMainWindow::eventFilter(watched, event);

    bool scriptTabs = false;
    for (QWidget *w = QApplication::focusWidget(); w; w = w->parentWidget())
    {
        if (!QString::fromLatin1(w->metaObject()->className())
                 .contains(QStringLiteral("ScriptEditorPanel")))
            continue;
        QTabWidget *tabs = w->findChild<QTabWidget *>();
        scriptTabs = tabs && tabs->count() > 1;
        break;
    }

    // 快捷键系统里仍有同键 QAction，先在 ShortcutOverride 阶段接管，避免歧义吞键。
    if (event->type() == QEvent::ShortcutOverride)
    {
        if (sequence == QStringLiteral("Ctrl+W")
            || (sequence == QStringLiteral("Ctrl+Tab") && !scriptTabs))
        {
            event->accept();
            return true;
        }
        return QMainWindow::eventFilter(watched, event);
    }

    // 脚本编辑器内有多个脚本页签时，Ctrl+Tab 交给面板循环；
    // 只有一个页签或焦点不在脚本编辑器时，切中央页签。Ctrl+W 统一关中央页签。
    if (sequence == QStringLiteral("Ctrl+Tab") || sequence == QStringLiteral("Ctrl+W"))
    {
        if (sequence == QStringLiteral("Ctrl+Tab") && scriptTabs)
            return QMainWindow::eventFilter(watched, event);
        const QString id = sequence == QStringLiteral("Ctrl+Tab")
                               ? QStringLiteral("editor.next")
                               : QStringLiteral("editor.close");
        if (m_commands->contains(id))
        {
            (void)executeCommand(id);
            return true;
        }
    }

    if (!m_pendingChordCommand.isEmpty())
    {
        const QString id = m_pendingChordCommand;
        const QStringList steps = KeybindingRegistry::chordSteps(m_keybindings->sequenceFor(id));
        m_pendingChordCommand.clear();
        ++m_chordToken;
        if (steps.size() == 2 && steps.at(1) == sequence)
        {
            (void)executeCommand(id);
            return true;
        }
    }

    const auto bindings = m_keybindings->bindings();
    for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it)
    {
        if (!KeybindingRegistry::isChord(it.value()))
            continue;
        const QStringList steps = KeybindingRegistry::chordSteps(it.value());
        if (steps.size() == 2 && steps.at(0) == sequence && m_commands->contains(it.key()))
        {
            m_pendingChordCommand = it.key();
            const int token = ++m_chordToken;
            QTimer::singleShot(1500, this, [this, token]() {
                if (token == m_chordToken)
                    m_pendingChordCommand.clear();
            });
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::saveState()
{
    if (!m_binder || !m_host)
        return;
    // 窗口状态属于 Workspace 作用域：跨会话可恢复，但不属于某个具体工作区。
    m_host->app().settings().setJson(SettingsScope::Workspace,
                                     QStringLiteral("window/state"), m_binder->captureState());
    saveWorkspaceUiConfig();
    m_host->app().settings().flush();
}

void MainWindow::restoreState()
{
    if (!m_binder || !m_host)
        return;
    const QJsonObject state = m_host->app().settings().json(SettingsScope::Workspace,
                                                            QStringLiteral("window/state"));
    if (!state.isEmpty())
        m_binder->applyState(state);
    // 首次运行也要应用 Workspace UI 配置的默认/显式值。
    applyWorkspaceUiConfig();
}

bool MainWindow::hasSavedState() const
{
    return m_host && !m_host->app().settings().json(SettingsScope::Workspace,
                                                     QStringLiteral("window/state"))
                        .isEmpty();
}

} // namespace nextpilot::plotscope::gui

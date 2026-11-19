#include "MainWindow.h"

#include "GuiContext.h"
#include "ImportDialog.h"
#include "ProtocolInspectorDialog.h"
#include "ProtocolEditorDialog.h"
#include "ShellBinder.h"
#include "ThemeManager.h"

#include "AppContext.h"
#include "RuntimeHost.h"
#include "SkeletonSlots.h"
#include "Intent.h"
#include "Result.h"
#include "UiSlotRegistry.h"
#include "IMessageParser.h"
#include "IToolContribution.h"

#include "SARibbonMainWindow.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMenuBar>
#include <QMessageBox>
#include <QResource>
#include <QShowEvent>
#include <QStatusBar>

using namespace nextpilot::plotscope::core;
using namespace nextpilot::plotscope::host;

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

    setWindowTitle(QStringLiteral("PlotScope NG"));
    resize(1280, 800);

    // 菜单栏说明：SARibbonMainWindow 已经把 **ribbon 条**装进了 QMainWindow 的
    // 菜单槽（menuWidget）。QMainWindow 的 menuBar 与 menuWidget 是**同一个槽位**
    // —— 调 setMenuBar(nullptr) 会把 ribbon 一起清掉，所以这里不再另建 QMenuBar；
    // 真正的动作由器官/工具箱声明到 ribbon 分组里。
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
    UiSlotRegistry &slotReg = m_context->slotRegistry();
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

    // "打开数据"：系统级意图 data.load，放"开始"页常用组（最高频入口）。
    addSkeleton(QStringLiteral("app.action.openData"), QStringLiteral("打开数据"),
                QStringLiteral("ribbon.group.home.data"), QStringLiteral("data.load"),
                QStringLiteral("file_open"), QStringLiteral("Ctrl+O")); // 抄老 file.open
    // 文件页也放一个"打开文件"（同一意图，抄老 file 页"文件"组第一个）。
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

    // --- 文件页：抄老 ShellComponent 的四个组 + 组内顺序 ---
    // 文件组：打开(0) / 导入布局(1) / 设置(2)
    addSkeleton(QStringLiteral("file.importLayout"), QStringLiteral("导入布局"),
                QStringLiteral("ribbon.group.file.document"), QStringLiteral("layout.import"),
                QStringLiteral("import"), QString(), 1);
    addSkeleton(QStringLiteral("file.settings"), QStringLiteral("设置"),
                QStringLiteral("ribbon.group.file.document"), QStringLiteral("settings.flush"),
                QStringLiteral("settings"), QStringLiteral("Ctrl+,"), 2);
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

    // --- 数据页"常用"组前两项（抄老 DataSourceRibbon）：快速打开 / 最近 ---
    // 快速打开与"打开数据"同为 data.load（走同一对话框/意图），最近走 data.recent。
    // 其余 重载/导出/清缓存 由 DataModule 贡献（order 10+）。
    addSkeleton(QStringLiteral("data.action.quickOpen"), QStringLiteral("快速打开"),
                QStringLiteral("ribbon.group.data.common"), QStringLiteral("data.load"),
                QStringLiteral("open"), QString(), 0);
    addSkeleton(QStringLiteral("data.action.recent"), QStringLiteral("最近"),
                QStringLiteral("ribbon.group.data.common"), QStringLiteral("data.recent"),
                QStringLiteral("history"), QString(), 1);
    // 数据页"调试"组（老 DataSourceRibbon 的 protocol.inspect）：复用协议检查器特判。
    addSkeleton(QStringLiteral("app.action.protocolInspector"), QStringLiteral("协议检查"),
                QStringLiteral("ribbon.group.data.debug"), QString(),
                QStringLiteral("datasource"), QString(), 0);

    // --- 脚本页：运行脚本（选 .lua/.py 文件 → script.run 意图，抄老 CLI 路径）---
    addSkeleton(QStringLiteral("app.action.runScript"), QStringLiteral("运行脚本"),
                QStringLiteral("ribbon.group.script.run"), QStringLiteral("script.run"),
                QStringLiteral("play"), QStringLiteral("Ctrl+F5"), 0);

    // --- 视图页：面板开关（抄老 view.toggleDock，按 dock 顺序 left/right/bottom）---
    addSkeleton(QStringLiteral("view.toggleDock.left"), QStringLiteral("数据集"),
                QStringLiteral("ribbon.group.view.panels"), QString(), QStringLiteral("panel"),
                QString(), 0);
    addSkeleton(QStringLiteral("view.toggleDock.right"), QStringLiteral("属性"),
                QStringLiteral("ribbon.group.view.panels"), QString(), QStringLiteral("panel"),
                QString(), 1);
    addSkeleton(QStringLiteral("view.toggleDock.bottom"), QStringLiteral("日志"),
                QStringLiteral("ribbon.group.view.panels"), QString(), QStringLiteral("panel"),
                QString(), 2);

    // --- 绘图页四组：抄老 PlottingModule 的面板划分与顺序 ---
    // 走 view action，由 CanvasWorkspace 集中处理（多画布/网格）。intentOp 留空：
    // dispatcher 先按 action id 查 view action，命中即执行，与脚本同路径。
    struct PlotToolDef { const char *area; const char *id; const char *label; const char *icon;
                         int order; const char *data; };
    static const PlotToolDef kPlotTools[] = {
        // 页面组（老 buildPagePanel）：新建页
        {"ribbon.group.plot.page",   "page.new",            "新建页",  "newpage", 0, ""},
        // 布局组（老 buildLayoutPicker）：网格 + 添加/删除/左右/上下
        {"ribbon.group.plot.layout", "page.grid",           "网格",    "grid",    0, "{\"rows\":2,\"cols\":1}"},
        {"ribbon.group.plot.layout", "page.addCanvas",      "添加",    "plus",    1, ""},
        {"ribbon.group.plot.layout", "page.removeCanvas",   "删除",    "trash",   2, ""},
        {"ribbon.group.plot.layout", "page.splitHorizontal","左右",   "split_h", 3, ""},
        {"ribbon.group.plot.layout", "page.splitVertical",  "上下",    "split_v", 4, ""},
        // 显示组（老 buildDisplayPanel）：网格线 / 图例（边框 ng 无对应能力，不造空按钮）
        {"ribbon.group.plot.display","plot.grid",           "网格",    "grid",    0, ""},
        {"ribbon.group.plot.display","plot.legend.toggle",  "图例",    "legend",  1, ""},
        // 画布操作组（老 buildPanelsToolbar）：放大 / 缩小 / 自适应
        // （"移动"是工具模式开关，ng 无 pan 工具 view action，不造空按钮）
        {"ribbon.group.plot.tool",   "plot.zoom",           "放大",    "zoom_in", 0, "{\"factor\":0.5}"},
        {"ribbon.group.plot.tool",   "plot.zoom",           "缩小",    "zoom_out",1, "{\"factor\":2.0}"},
        {"ribbon.group.plot.tool",   "plot.autoscale",      "自适应",  "fit",     2, ""},
    };
    for (const PlotToolDef &d : kPlotTools)
    {
        UiAction a;
        a.id = QString::fromLatin1(d.id);
        a.label = QString::fromUtf8(d.label);
        a.area = QString::fromLatin1(d.area);
        a.icon = QString::fromLatin1(d.icon);
        a.data = QString::fromLatin1(d.data);
        a.order = d.order;
        slotReg.bindLocal(a.area, a, QStringLiteral("app.skeleton"));
    }

    // --- 图型库（抄老 PlottingRibbon::buildPlotGallery 的体验）---
    // 12 种图型分"常用/曲线/地图3D/仪表"四组。每项都是纯 UiAction，
    // 点击走 view action "plot.setType"（int/dispatcher 里路由），
    // 与脚本/回放同一语义。缩略图由 visual 字段交给 GUI 层绘制。
    const QString plotArea = QStringLiteral("ribbon.group.plot.type");
    // 分类与组内顺序抄老 buildPlotGallery：常用=时间序列/XY/散点/3D轨迹/GPS，
    // 曲线=柱状/直方/频谱，地图/3D=3D曲面，仪表=地平仪/罗盘/表盘。
    // 老代码把 timeseries/xy/scatter/traj3d/map 同时放进"常用"与各自分类；
    // ng 一项只能归属一个 galleryGroup，故按主分类去重，共 12 项。
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

    // --- 来源库（抄老 DataSourceRibbon 的"来源"gallery）---
    // 由工具箱里已注册的格式驱动动态生成：每个驱动一项，放"文件格式"组；
    // 实时源放"实时源"组。点击走 data.source.<id> → 按该格式直接弹文件对话框。
    const QString srcArea = QStringLiteral("ribbon.group.data.sources");
    // gallery 分组出现顺序按动作 order 聚合：常用(0..3) 必须排在文件格式(100+) 之前，
    // 抄老 DataSourceRibbon 的 addCategoryActions("常用")→("文件格式")→("实时源") 顺序。
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
    // 常用组：把前四个格式驱动也放进去（抄老代码"常用 = 前 4 个加载器"）。
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
}

void MainWindow::build(RuntimeHost &host)
{
    m_host = &host;
    UiSlotRegistry &slotReg = host.uiSlotsMutable();

    // 绑定器需要插槽表，构造时给。
    m_binder = new ShellBinder(this, slotReg);
    m_context = new GuiContext(this, slotReg, m_binder, m_appCtx, &host, this);

    // 1) 骨架插槽
    declareStandardSlots();

    // 2) ribbon（SARibbon）——结构层交给第三方，ng 只提供插槽数据
    //
    // SARibbonMainWindow 已经在构造时把 ribbon 条装进了 menuWidget 槽
    // （见 setRibbonBar），这里只把它的**样式**调成我们要的样子，
    // 并把同一个对象交给绑定器去填内容。
    m_binder->createRibbon(ribbonBar());

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
                                                  m_theme->ribbonStyleSheet());
                     });
    m_theme->applyCurrent(); // 把 settings 里的初始值落到全局 QSS 与 ribbon

    // 3) 装器官（GUI 模式：会调用 onAttach，器官可自带界面入口）
    host.activate(QStringList(), m_context);

    // 3.0) 属性面板占 dock.right（替换原 CAN 面板的默认占用）。
    // 外壳只用**部件类型字符串**请求，不 include 任何 views/ 头文件——
    // 具体 QWidget 由视图层自注册（NG_REGISTER_PANEL("property-panel")）。
    // 属性面板是 PropertySchema 契约的通用视图：选中什么、显示什么全由
    // AppContext 的 Selection + PropertyProvider 决定，外壳不认识业务。
    if (m_context->requestPanel(QStringLiteral("dock.right"), QStringLiteral("property-panel")))
        statusBar()->showMessage(QStringLiteral("属性面板已就绪"));

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
        if (actionId == QStringLiteral("app.action.protocolEditor")
            || actionId == QStringLiteral("protocol.new")
            || actionId == QStringLiteral("protocol.open")
            || actionId == QStringLiteral("protocol.validate")
            || actionId == QStringLiteral("protocol.editor"))
        {
            openProtocolEditor();
            return;
        }
        // 视图页面板开关：抄老 view.toggleDock，按 dock id 切换停靠面板显隐。
        if (actionId.startsWith(QStringLiteral("view.toggleDock.")))
        {
            const QString area = QStringLiteral("dock.")
                               + actionId.mid(QStringLiteral("view.toggleDock.").size());
            const bool nowVisible = !m_binder->isPanelVisible(area);
            m_binder->setPanelVisible(area, nowVisible);
            statusBar()->showMessage(QStringLiteral("面板 %1: %2")
                                         .arg(area, nowVisible ? QStringLiteral("显示") : QStringLiteral("隐藏")));
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
        // 截图：抓当前窗口存 PNG（抄老 file.screenshot）。
        if (actionId == QStringLiteral("file.screenshot"))
        {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("截图"), QStringLiteral("plotscope.png"),
                QStringLiteral("PNG 图片 (*.png)"));
            if (!path.isEmpty())
                grab().save(path);
            return;
        }
        // 导出设置：把当前 SettingsStore 的键值导出为 JSON（抄老 file.exportSettings）。
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
        // 关于：版本与用途说明（抄老 help.about）。
        if (actionId == QStringLiteral("help.about"))
        {
            QMessageBox::about(this, QStringLiteral("关于 PlotScope NG"),
                QStringLiteral("PlotScope NG — 数据 / 绘图 / 分析软件\n"
                               "架构底座：Dataset / Intent / Contribution / Module / ToolPack\n"
                               "绘图库：fastplot（Figure / Axes / Line / Artist）"));
            return;
        }
        // 保存/导入布局：窗口状态写入/读出 Workspace 作用域 settings（抄老 layout.save/import）。
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
        // 抄老 DataSourceRibbon 点加载器即弹对应 filter 的体验；仍走 data.load 意图。
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
        if (m_appCtx.interactions().dispatchViewAction(actionId, params, viewResult))
        {
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

    // 4) ribbon 重建：把工具箱的 UI 贡献 + 器官的本地入口一起渲染出来
    m_binder->rebuildRibbon();

    // 5) 恢复上次的窗口状态（首次运行则安全跳过）
    restoreState();
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

void MainWindow::openDataWithFormat(RuntimeHost &host, const QString &formatId)
{
    // 抄老 DataSourceRibbon 的体验：点图库里的加载器，直接弹该格式的文件对话框。
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
    // 协议编辑器：编辑 .psproto 模板 + 校验 + 试算，逻辑来自 ng/libs/protocol。
    // 传入 appCtx 以支持 checksum 变换（算子/表达式/Lua）的在线调试。
    ProtocolEditorDialog::run(this, &m_appCtx);
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
    }
}

void MainWindow::saveState()
{
    if (!m_binder || !m_host)
        return;
    // 窗口状态属于 Workspace 作用域：跨会话可恢复，但不属于某个具体工作区。
    m_host->app().settings().setJson(SettingsScope::Workspace,
                                     QStringLiteral("window/state"), m_binder->captureState());
    m_host->app().settings().flush();
}

void MainWindow::restoreState()
{
    if (!m_binder || !m_host)
        return;
    const QJsonObject state = m_host->app().settings().json(SettingsScope::Workspace,
                                                            QStringLiteral("window/state"));
    if (state.isEmpty())
        return; // 首次运行：无状态可恢复，正常降级
    m_binder->applyState(state);
}

bool MainWindow::hasSavedState() const
{
    return m_host && !m_host->app().settings().json(SettingsScope::Workspace,
                                                     QStringLiteral("window/state"))
                        .isEmpty();
}

} // namespace nextpilot::plotscope::gui

#include "MainWindow.h"

#include "GuiContext.h"
#include "ImportDialog.h"
#include "ProtocolInspectorDialog.h"
#include "ProtocolEditorDialog.h"
#include "ShellBinder.h"
#include "ThemeManager.h"

#include "AppContext.h"
#include "ModuleHost.h"
#include "SkeletonSlots.h"
#include "Intent.h"
#include "Result.h"
#include "UiSlotRegistry.h"
#include "IFormatDriver.h"
#include "IToolContribution.h"

#include "SARibbonMainWindow.h"

#include <QFileDialog>
#include <QMenuBar>
#include <QResource>
#include <QShowEvent>
#include <QStatusBar>

using namespace ng::core;
using namespace ng::host;

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

namespace ng::gui
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
    ng::host::declareSkeletonSlots(slotReg);

    // 骨架自带的**标准动作**：往标准插槽里放内容。
    //
    // "打开数据"是 app 骨架级能力（data.load 本来就是系统级意图，
    // 不归属任何可卸载模块），所以由骨架提供；模块与工具箱往别的
    // 分组里放自己的入口，两者互不干扰。
    UiAction openAction;
    openAction.id = QStringLiteral("app.action.openData");
    openAction.label = QStringLiteral("打开数据");
    openAction.area = QStringLiteral("ribbon.group.home.data");
    openAction.intentOp = QStringLiteral("data.load");
    openAction.icon = QStringLiteral("file_open");
    openAction.shortcut = QStringLiteral("Ctrl+O"); // 抄老 file.open
    slotReg.bindLocal(openAction.area, openAction, QStringLiteral("app.skeleton"));

    // "协议检查器"：骨架级 GUI 能力，消费 IMessageParser 自述逐帧查看协议字段。
    // 纯检查展示（产 DecodedFrame，不入 Dataset），没有对应意图 op，按 id 特判弹窗。
    UiAction inspectAction;
    inspectAction.id = QStringLiteral("app.action.protocolInspector");
    inspectAction.label = QStringLiteral("协议检查器");
    inspectAction.area = QStringLiteral("ribbon.group.home.data");
    inspectAction.icon = QStringLiteral("file_open");
    slotReg.bindLocal(inspectAction.area, inspectAction, QStringLiteral("app.skeleton"));

    // "协议编辑器"：创建/编辑/校验顺序段协议模板（.psproto），试算单帧。
    // 纯 GUI 工具，按 id 特判弹窗；编辑好的协议可被"自定义协议"数据格式直接加载。
    UiAction editorAction;
    editorAction.id = QStringLiteral("app.action.protocolEditor");
    editorAction.label = QStringLiteral("协议编辑器");
    editorAction.area = QStringLiteral("ribbon.group.home.data");
    editorAction.icon = QStringLiteral("file_open");
    slotReg.bindLocal(editorAction.area, editorAction, QStringLiteral("app.skeleton"));

    // "切换主题"是 app 骨架级能力（纯 GUI 展示态，非任何可卸载模块）。
    // 它只声明 intentOp=theme.toggle，真正干活的是 GUI 装配期注册的 ThemeManager——
    // 于是按钮、脚本、回放都走同一条 theme.toggle 意图路径。
    UiAction themeAction;
    themeAction.id = QStringLiteral("app.action.toggleTheme");
    themeAction.label = QStringLiteral("切换主题");
    themeAction.area = QStringLiteral("ribbon.group.home.appearance");
    themeAction.intentOp = QStringLiteral("theme.toggle");
    themeAction.icon = QStringLiteral("theme_toggle");
    themeAction.shortcut = QStringLiteral("Ctrl+T"); // 抄老 Ctrl+T 习惯
    slotReg.bindLocal(themeAction.area, themeAction, QStringLiteral("app.skeleton"));

    // --- 图型库（抄老 PlottingRibbon::buildPlotGallery 的体验）---
    // 12 种图型分"常用/曲线/地图3D/仪表"四组。每项都是纯 UiAction，
    // 点击走 view action "plot.setType"（int/dispatcher 里路由），
    // 与脚本/回放同一语义。缩略图由 visual 字段交给 GUI 层绘制。
    const QString plotArea = QStringLiteral("ribbon.group.home.plottype");
    struct PlotDef { const char *kind; const char *label; const char *group; };
    static const PlotDef kPlots[] = {
        {"timeseries", "时间序列", "常用"}, {"xy", "XY 曲线", "常用"},
        {"scatter", "散点图", "常用"},       {"gauge", "仪表盘", "常用"},
        {"bar", "柱状图", "曲线"},           {"histogram", "直方图", "曲线"},
        {"fft", "频谱 FFT", "曲线"},
        {"traj3d", "3D 轨迹", "地图 / 3D"},  {"map", "GPS 地图", "地图 / 3D"},
        {"surface3d", "3D 曲面", "地图 / 3D"},
        {"horizon", "地平仪", "仪表"},       {"compass", "罗盘", "仪表"},
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
        slotReg.bindLocal(plotArea, a, QStringLiteral("app.skeleton"));
    }

    // --- 来源库（抄老 DataSourceRibbon 的"来源"gallery）---
    // 由工具箱里已注册的格式驱动动态生成：每个驱动一项，放"文件格式"组；
    // 实时源放"实时源"组。点击走 data.source.<id> → 按该格式直接弹文件对话框。
    const QString srcArea = QStringLiteral("ribbon.group.home.sources");
    for (ng::core::IToolContribution *c : m_appCtx.toolbox().contributions(
             ng::core::ContributionKind::Format))
    {
        auto *driver = dynamic_cast<ng::core::IFormatDriver *>(c);
        if (!driver)
            continue;
        UiAction a;
        a.id = QStringLiteral("data.source.") + driver->id();
        a.label = driver->displayName();
        a.area = srcArea;
        a.visual = driver->id();
        a.galleryGroup = QStringLiteral("文件格式");
        a.icon = QStringLiteral("file_open");
        slotReg.bindLocal(srcArea, a, QStringLiteral("app.skeleton"));
    }
    // 常用组：把前四个格式驱动也放进去（抄老代码"常用 = 前 4 个加载器"）。
    {
        int n = 0;
        for (ng::core::IToolContribution *c : m_appCtx.toolbox().contributions(
                 ng::core::ContributionKind::Format))
        {
            auto *driver = dynamic_cast<ng::core::IFormatDriver *>(c);
            if (!driver || n++ >= 4)
                continue;
            UiAction a;
            a.id = QStringLiteral("data.source.common.") + driver->id();
            a.label = driver->displayName();
            a.area = srcArea;
            a.visual = driver->id();
            a.galleryGroup = QStringLiteral("常用");
            a.icon = QStringLiteral("file_open");
            slotReg.bindLocal(srcArea, a, QStringLiteral("app.skeleton"));
        }
    }
}

void MainWindow::build(ModuleHost &host)
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

    // 3.1) ribbon 动作 -> 意图：外壳只把动作 id 交出去，不解释语义。
    //
    // 意图由宿主执行（ModuleHost 实现了 IIntentExecutor），
    // 于是 GUI 点击与 CLI/脚本执行走**同一条意图路径**。
    m_binder->setIntentDispatcher([&host, this](const QString &actionId,
                                                const QString &intentOp) {
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
        if (actionId == QStringLiteral("app.action.protocolEditor"))
        {
            openProtocolEditor();
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
            if (m_appCtx.dispatchViewAction(QStringLiteral("plot.setType"), p, res) && res)
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
                        m_appCtx.dispatchViewAction(QStringLiteral("plot.instrument.setKind"), ip, ires);
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
        QVariantMap params;
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
        if (m_appCtx.dispatchViewAction(actionId, params, viewResult))
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
        const Result<IntentResult> r = host.execute(intent);
        if (!r)
            statusBar()->showMessage(QStringLiteral("动作失败: %1").arg(r.error().message));
    });

    // 4) ribbon 重建：把工具箱的 UI 贡献 + 器官的本地入口一起渲染出来
    m_binder->rebuildRibbon();

    // 5) 恢复上次的窗口状态（首次运行则安全跳过）
    restoreState();
}

void MainWindow::openDataFile(ModuleHost &host)
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

void MainWindow::openDataWithFormat(ModuleHost &host, const QString &formatId)
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
    ProtocolEditorDialog::run(this);
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

} // namespace ng::gui

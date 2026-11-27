#include "MainWindow.h"

#include <cstdio>

#include "GuiContext.h"
#include "CommandPalette.h"
#include "command/CommandRegistry.h"
#include "KeybindingDialog.h"
#include "command/KeybindingRegistry.h"
#include "PreferencesIntentHandler.h"
#include "ShellBinder.h"
#include "theme/IconRegistry.h" // A2c：缩略图绘制器归位 modules/plot/gui，壳只查 IconRegistry
#include "ThemeManager.h"

#include "AppContext.h"
#include "RuntimeHost.h"
#include "InstantiationService.h"
#include "IPanelsService.h"
#include "IFormatHints.h"
#include "IThemeService.h"
#include "ShellContributions.h"
#include "Intent.h"
#include "Result.h"
#include "ErrorCodes.h"
#include "UiSlotTypes.h"
#include "ISlotsService.h"
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
#include <QLabel>
#include <algorithm>
#include "WorkspaceDataConfig.h"

#include <QCloseEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QSet>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QResource>
#include <QSaveFile>
#include <QShowEvent>
#include <QTimer>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
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

namespace {

// 轻量意图处理器适配器：shell UI 命令的 op → MainWindow::runShellAction。
// canHandle 用精确 op 集合（含 activity.activate.* 前缀），不与模块业务意图冲突。
class ShellIntentHandler final : public QObject, public intent::IIntentHandler
{
public:
    using Action = std::function<bool(const QString &op, QString *error)>;

    explicit ShellIntentHandler(Action action, QObject *parent = nullptr) : QObject(parent), m_action(std::move(action))
    {
    }

    bool canHandle(const intent::Intent &i) const override
    {
        const QString &op = i.op;
        static const QSet<QString> kShellOps = {
            QStringLiteral("palette.show"), QStringLiteral("quick.switch"),
            QStringLiteral("view.toggleSideBar"), QStringLiteral("view.toggleAuxZone"),
            QStringLiteral("view.togglePanel"), QStringLiteral("editor.next"),
            QStringLiteral("editor.close"), QStringLiteral("plot.series.removeSelected"),
            QStringLiteral("preferences.keybindings"),
        };
        return kShellOps.contains(op) || op.startsWith(QStringLiteral("activity.activate."));
    }

    foundation::Result<intent::IntentResult> execute(const intent::Intent &i) override
    {
        QString error;
        if (m_action(i.op, &error))
        {
            intent::IntentResult r;
            return foundation::Result<intent::IntentResult>(r);
        }
        return foundation::fail(err::Unsupported, error.isEmpty()
                                    ? QStringLiteral("shell 命令执行失败: %1").arg(i.op)
                                    : error);
    }

private:
    Action m_action;
};

} // namespace

MainWindow::MainWindow(AppContext &appCtx, const registry::ProductSpec &spec, QWidget *parent)
    : SARibbonMainWindow(parent,
                         // UseRibbonFrame    = 无边框（framelesshelper 接管拖拽/缩放/贴边，
                         //                     并由 SARibbon 自绘窗口按钮）
                         // UseRibbonMenuBar = 用 ribbon 条取代系统菜单栏
                         SARibbonMainWindowStyleFlag::UseRibbonFrame
                             | SARibbonMainWindowStyleFlag::UseRibbonMenuBar),
      m_appCtx(appCtx),
      m_spec(spec)
{
    initSaribbonResources();

    // 窗口标题来自产品 spec；壳内不写死任何产品名（契约 §19/§20）。
    setWindowTitle(m_spec.displayName);
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
    // 必须撤掉命令接缝：注入的 lambda 捕获 this 与 m_commands（都是窗口的成员）。
    // AppContext 的生命周期比窗口长（测试/多窗口场景会先于窗口存在），
    // 留着悬空捕获会让窗口销毁后的求值/执行直接崩。
    m_appCtx.setCommandHooks(nullptr, nullptr);
}

void MainWindow::declareStandardSlots()
{
    // app 骨架的壳级 Ribbon 页/组，声明清单在 host 层 ShellContributions（不写死在 core）。
    // 这里只做 GUI 侧的**幂等确认**（宿主已声明过，重复声明不会产生副本），
    // 保证即便脱离宿主单独使用 MainWindow，槽也齐备。
    nextpilot::plotscope::workbench::ISlotsService *slotReg = m_context->slotsService();
    if (!slotReg)
        return; // bare 壳未装配槽位服务：骨架声明由 host 层直连通道完成，壳侧跳过
    nextpilot::plotscope::host::declareShellContributions(*slotReg);

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
        a.slotId = area;
        a.intentOp = op;
        a.icon = icon;
        a.shortcut = shortcut;
        a.order = order;
        slotReg->bindLocal(area, a, QStringLiteral("app.skeleton"));
    };

    // 开始页 / 数据页 / 协议页 / 脚本页的业务入口已随各模块 onAttach 自声明
    // （D13）；壳这里只保留文件页与视图页的壳级能力。
    // 文件页也放一个"打开文件"（同一意图）。
    addSkeleton(QStringLiteral("file.open"), QStringLiteral("打开文件"),
                QStringLiteral("ribbon.group.file.document"), QStringLiteral("data.load"),
                QStringLiteral("file_open"), QStringLiteral("Ctrl+O"), 0);

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

    // 数据页常用组的快速打开/最近由 DataModule 贡献；协议检查器/编辑器归 ProtocolModule；
    // 运行脚本归 ScriptModule。壳不再预置业务页动作。

    // --- 视图页：面板开关（每个已声明的 dock 视图一个按钮）---
    // 壳不再持有业务 viewId 字面量：动作在 `declareViewToggleSlots()`（activate 之后、
    // rebuildRibbon 之前）按 IViewsService 的声明动态注册。这里只留一句说明。
    // 来源画廊条目由 DataModule 按已装载的格式驱动元数据派生（sourceGalleryActions），
    // 壳不遍历驱动、不保留格式常量表；这里只保留点击后的对话框分发入口。
}

// 视图页开关动作：按 IViewsService 的声明动态注册，壳内不出现任何业务 viewId 字面量。
// 动作 id = "view.toggleDock." + viewId；标签/图标取声明（名称空则回退 viewId）。
// 调用时机：host.activate(...) 之后、rebuildRibbon() 之前——声明此时才齐。
void MainWindow::declareViewToggleSlots()
{
    if (!m_viewsService || !m_slotsService)
        return; // bare 壳 / 未装配：无视图声明可枚举，保持空

    int order = 0;
    for (const QString &area : {QStringLiteral("sidebar"), QStringLiteral("auxiliary"), QStringLiteral("bottom")})
    {
        for (const auto &container : m_viewsService->containersIn(area))
        {
            for (const auto &view : m_viewsService->viewsInContainer(container.id))
            {
                nextpilot::plotscope::registry::UiAction a;
                a.id = QStringLiteral("view.toggleDock.") + view.id;
                a.label = view.name.isEmpty() ? view.id : view.name;
                a.slotId = QStringLiteral("ribbon.group.view.panels");
                a.icon = view.icon;
                a.order = order++;
                m_slotsService->bindLocal(a.slotId, a, QStringLiteral("core.workbench"));
            }
        }
    }
}

void MainWindow::build(RuntimeHost &host)
{
    m_host = &host;

    // 壳读回服务（裁定 52441633 §三 + 52442006 Step 4）：包面板声明/格式提示/
    // 扩展主题 token/槽位表都经 workbench 接口；实现由装配层注册进服务容器。
    // 未装配（bare 壳反证冒烟/极简装配）时保持 nullptr，各读回点退化为空。
    using namespace nextpilot::plotscope::instantiation;
    using namespace nextpilot::plotscope::workbench;
    InstantiationService *services = host.services();
    if (services)
    {
        m_panelsService = services->getService<IPanelsService>(QStringLiteral("panels.readback"));
        m_formatHints = services->getService<IFormatHints>(QStringLiteral("format.hints"));
        m_themeService = services->getService<IThemeService>(QStringLiteral("theme.readback"));
        m_slotsService = services->getService<ISlotsService>(QStringLiteral("slots.binding"));
        // 视图/编辑器打开服务（A2b 笔2b 复审必修 A）：openView/openEditor 的执行体，
        // 注入 GuiContext（壳只转调，不桥回旧工厂表）。未装配时保持 nullptr。
        m_viewsService = services->getService<IViewsService>(QStringLiteral("views.readback"));
        m_editorService = services->getService<IEditorService>(QStringLiteral("editor.readback"));
    }

    // 绑定器需要槽位服务，构造时给。
    m_binder = new ShellBinder(this, m_slotsService);
    // Ribbon 上下文页签：binder 不认识命令库，求值器由外壳注入。
    // 用当前上下文快照求值；上下文一改，updateCommandContext 会触发重算。
    m_binder->setWhenEvaluator([this](const QString &when) {
        if (!m_commands)
            return true;
        return CommandRegistry::evaluateWhen(when, m_commands->contextValues());
    });
    // 视图标题栏动作：工具箱声明（ViewTitleActionSpec）→ 外壳造 QAction → ADS 标题栏。
    // 三条约束写在这里，不给 binder 加业务知识：
    //   1) when 用命令层求值器判，不自己发明语法；
    //   2) 标签/图标取命令描述（同一个命令在 Ribbon、标题栏、命令面板显示一致）；
    //   3) 点击走 m_commands->execute，和按钮/快捷键同一条路径。
    m_binder->setViewTitleActionProvider([this](QWidget *parent, const QString &viewId) {
        QList<QAction *> actions;
        if (!m_commands)
            return actions;
        const QHash<QString, QVariant> ctx = m_commands->contextValues();
        // 标题动作声明经 IPanelsService 读回（裁定 52441633 §三），壳不直连扩展注册表。
        const QList<nextpilot::plotscope::registry::ViewTitleActionSpec> specs =
            m_panelsService ? m_panelsService->titleActions(viewId)
                            : QList<nextpilot::plotscope::registry::ViewTitleActionSpec>{};
        for (const ViewTitleActionSpec &spec : specs)
        {
            if (!spec.when.isEmpty() && !CommandRegistry::evaluateWhen(spec.when, ctx))
                continue;
            const CommandDescriptor *cmd = m_commands->find(spec.commandId);
            auto *action = new QAction(parent);
            action->setText(cmd ? cmd->title : spec.commandId);
            action->setToolTip(action->text());
            if (cmd && !cmd->icon.isEmpty())
                action->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(cmd->icon)));
            else
                action->setIcon(parent ? parent->style()->standardIcon(QStyle::SP_BrowserReload)
                                       : QIcon());
            const QString commandId = spec.commandId;
            connect(action, &QAction::triggered, this, [this, commandId]() {
                QString error;
                if (m_commands)
                    m_commands->execute(commandId, &error);
            });
            actions << action;
        }
        return actions;
    });
    m_context = new GuiContext(this, m_slotsService, m_binder, m_appCtx, &host, m_viewsService, m_editorService, this);
    // 状态栏：先接好信号，模块 onAttach（activate 内）声明状态项时即可排布（验收 7）。
    setupStatusBar();

    m_binder->setActivityActivatedHandler([this](const ActivitySpec &spec) {
        // Activity 的侧栏切换由 ShellBinder 完成；这里只处理"要打开哪个中央面板"。
        // 第①步只做接口声明与字段改名；调用点迁移（→ openEditor/openView）随各 owner 步进行。
        // A2c：centralEditor 走 openEditor——ResourceRef 由 owner 模块在
        // ActivitySpec.centralRef 构造（如 plot://workspace），壳只透传不解析 uri；
        // 同 ref 已开时 GuiContext 簿记置前复用。
        if (!spec.centralEditor.isEmpty() && m_context->openEditor(spec.centralEditor, spec.centralRef))
        {
            // 请求成功即已置前；没有额外业务动作。
        }
        if (spec.id == QStringLiteral("diagnostics"))
        {
            // A2b 笔2b 第②步：diagnostics 已迁 views 声明（owner core.workbench），
            // 走 openView——落区由 viewsContainers[].area 决定，壳不再指定。
            m_context->openView(QStringLiteral("diagnostics"));
        }
    });
    m_binder->setSettingsHandler([this]() {
        Intent intent;
        intent.op = QStringLiteral("preferences.open");
        (void)m_host->execute(intent);
    });

    // 报告导出的 viewAction "report.export" 由 report 模块呈现层注册（装配层
    // installReportExport）；外壳零业务 include。脚本与 Ribbon 按钮共用该入口。

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
    refreshThemeTokens();
    host.registerSystemHandler(QStringLiteral("@system/theme"), m_theme);
    QObject::connect(m_theme, &ThemeManager::themeChanged, this,
                     [this](ThemeManager::Theme theme) {
                         if (m_binder)
                             m_binder->applyTheme(theme == ThemeManager::Theme::Dark,
                                                  m_theme->ribbonStyleSheet(),
                                                  m_theme->dockStyleSheet());
                         // 换肤后把语义色重推一遍：绘制路径读的是这份表，
                         // 不重推就会停在旧主题的实色上（验收 8 的消费面）。
                         refreshThemeTokens();
                     });
    m_theme->applyCurrent(); // 把 settings 里的初始值落到全局 QSS 与 ribbon

    // 2.2) 「文件 → 设置」= 工作台内置 Settings editor 页签（非模态对话框）。
    //   preferences.open 处理器只转派到 workbench.action.openSettings，
    //   最终执行体唯一（IEditorService::openEditor）；壳不碰编辑器服务、不造对话框。
    m_preferencesHandler = new PreferencesIntentHandler(host, this);
    host.registerSystemHandler(QStringLiteral("@system/preferences"), m_preferencesHandler);

    // 3) 装模块（GUI 模式：会调用 onAttach，模块可自带界面入口）
    host.activate(QStringList(), m_context);

    // A8b-M2 裁定 52442006 Step 3：灌回已搬装配层（activate 内经 IContributionsInstaller），
    // 灌回后由壳重建一次 ribbon 与 ActivityBar——原 GuiContext::installContributions 尾部的
    // rebuild 调用随之搬家，壳在装配完成点统一重建（幂等）。
    declareViewToggleSlots(); // 视图页开关动作：按已声明视图动态注册（无业务字面量）
    m_binder->rebuildRibbon();
    m_binder->rebuildActivityBar();

    // 3.0a) 面板引用启动期校验（验收 4）：两源（A2d 第②步后：Activity panelTypes、
    // centralPanel / 产品 defaultPanels）必须命中声明面——views 声明
    // （viewsContainers/views，IViewsService 读回）∪ customEditors 声明
    // （IEditorService::customEditors() 读回；A2d 第⑤步起这是全部命中面）。
    // binder 只收集，这里统一进 AppContext error 诊断 + stderr，同一问题不打两遍；
    // 不 abort。缺失列表由 test_gui 断言为空。
    m_binder->setPanelReferenceContext(m_spec.defaultPanels, m_viewsService, m_editorService);
    m_panelReferenceIssues = m_binder->missingPanelTypes();
    for (const QString &entry : m_panelReferenceIssues)
    {
        m_appCtx.appendMessage(QStringLiteral("error"), QStringLiteral("panel-ref"),
                               QStringLiteral("[panelTypes] 未命中声明面: %1").arg(entry));
        fprintf(stderr, "[panelTypes] 未命中声明面: %s\n", entry.toUtf8().constData());
    }

    // 3.0-) 模块 onAttach 已完成声明，把模块贡献的语义色合并进主题（验收 8）。
    refreshThemeTokens();

    // 3.0) 默认面板布局：清单来自产品 spec（完整产品 / 裸壳各一份）。
    // A2c 收工：12 个业务面板注册点全部迁 views 声明（owner data/plot/script/
    // automotive），壳内置 5 点早已迁（A2b 第②步）——defaultPanels 全走 openView，
    // 落区由 viewsContainers[].area 决定（A2d：旧打开入口接口本体与旧工厂类型
    // 已随旧契约删除；placement.region 字段本卡删除）。
    for (const auto &placement : m_spec.defaultPanels)
    {
        if (placement.viewId.isEmpty())
            continue;
        QWidget *panel = m_context->openView(placement.viewId);
        if (panel && placement.raiseAfter)
            m_binder->raisePanel(panel);
    }

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
        // 打开数据对话框已归 data 模块呈现层（D18a）：外壳按 id 经启动器表触发，
        // 不 include、不构造业务对话框；未注册（裸壳/headless）时 no-op。
        if (actionId == QStringLiteral("app.action.openData") && m_dialogLauncher)
        {
            m_dialogLauncher(actionId);
            return;
        }
        // 协议对话框归模块呈现层（D18a）：外壳只把 id 交给启动器表，不 include、
        // 不构造任何业务对话框。未注册（裸壳/headless）时 launch 为 no-op。
        if ((actionId == QStringLiteral("app.action.protocolInspector") ||
             actionId == QStringLiteral("app.action.protocolEditor") || actionId == QStringLiteral("protocol.new") ||
             actionId == QStringLiteral("protocol.open") || actionId == QStringLiteral("protocol.validate") ||
             actionId == QStringLiteral("protocol.editor")) &&
            m_dialogLauncher)
        {
            m_dialogLauncher(actionId);
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
        // 报告导出 GUI 交互（选路径）归 report 模块呈现层（D18a）：按 id 触发启动器。
        if (actionId == QStringLiteral("report.workbench.export") && m_dialogLauncher)
        {
            m_dialogLauncher(actionId);
            return;
        }
        // 视图页面板开关：动作 id = "view.toggleDock." + 面板类型 id（pageKey）。
        // 语义是"一个按钮管一个页签"：点击一次出现、再点一次消失，只影响
        // 自己，不动同区其它页签。所有底部页签（含诊断）在装配期就已创建，
        // 不存在"首次点击才开面板"的特判。
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
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("运行脚本"), QString(),
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
                this, QStringLiteral("截图"), m_spec.id + QStringLiteral(".png"),
                QStringLiteral("PNG 图片 (*.png)"));
            if (!path.isEmpty())
                grab().save(path);
            return;
        }
        // 导出设置：把当前 SettingsStore 的键值导出为 JSON（file.exportSettings）。
        if (actionId == QStringLiteral("file.exportSettings"))
        {
            const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出设置"),
                                                              m_spec.id + QStringLiteral("-settings.json"),
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
            // 身份文案走 ProductSpec（19a/19b）：壳只拼装，不写死产品名或业务用途描述。
            const QString aboutName = m_spec.displayName;
            const QString aboutVersion = m_spec.version.isEmpty()
                ? QString() : QStringLiteral(" v%1").arg(m_spec.version);
            const QString purpose = m_spec.aboutText.isEmpty()
                ? QString() : QStringLiteral(" — ") + m_spec.aboutText;
            QMessageBox::about(this, tr("关于 %1").arg(aboutName),
                               aboutName + aboutVersion + purpose);
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
                // 仪表/地图/3D：切到专用仪表视图（plot.instrument editor）。
                // A2c 归位：gallery kind → 仪表 kind 的映射是 plot 业务，随 editor
                // 化挪进 plot 模块（plot.instrument.open 处理器，PlotModule::onAttach）；
                // 壳只派发动作 id + 原始 kind，不认识映射表，不构造 ref。
                QVariantMap ip;
                ip.insert(QStringLiteral("kind"), kind);
                QVariant ivr;
                Result<QVariant> ires(ivr);
                if (m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.instrument.open"), ip, ires) &&
                    ires && ires.value().toBool())
                {
                    statusBar()->showMessage(QStringLiteral("图型: %1").arg(kind));
                    return;
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
                    // A2c：缩略图按注册 id 查 IconRegistry（"plot/layout.RxC"，1..8
                    // 全组合已由 PlotThumbnails::registerIcons 注册）。
                    m_binder->updateActionStyle(
                        QStringLiteral("figure.grid"),
                        gui::IconRegistry::instance().icon(QStringLiteral("plot/layout.%1x%2").arg(gr).arg(gc)),
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
    // 点图库里的加载器，直接弹该格式的文件对话框。格式提示（显示名/过滤串）经
    // IFormatHints 读回（裁定 52441633 §三），壳不直连 IMessageParser。
    QString filter = QStringLiteral("全部 (*)");
    QString displayName;
    if (m_formatHints)
    {
        const QString hint = m_formatHints->fileFilter(formatId);
        if (!hint.isEmpty())
            filter = hint + QStringLiteral(";;全部 (*)");
        displayName = m_formatHints->displayName(formatId);
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("打开 %1").arg(displayName.isEmpty() ? formatId : displayName),
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
    config.insert(
        QStringLiteral("plots"),
        collected.value(QStringLiteral("document")).toObject().value(QStringLiteral("plot.document")).toObject());
    root.insert(QStringLiteral("config"), config);
    root.insert(QStringLiteral("session"), collected.value(QStringLiteral("session")).toObject());

    const QString dir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("session"));
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
    const QString dir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("session"));
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

    // 档位（startup.recovery，落盘 app/startup.recovery，默认 layout）：
    //   off    什么都不恢复；
    //   layout 只还原源条目与画布，不读数据——启动不被大文件拖住；
    //   full   layout + 真正摄取（data.workspace.restore）。
    // 流式源在任何档位都只保留条目：连接要用户显式放行。
    const QString mode = m_appCtx.settings()
                             .scopedValue(nextpilot::plotscope::context::SettingsScope::App,
                                          QStringLiteral("startup.recovery"), QStringLiteral("layout"))
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
            // A2c：调试 fprintf 残留已删——裸壳启动 3 秒无 stderr（A2c 判据）。
            m_binder->focusPanelByTitle(nextpilot::plotscope::registry::Region::PrimarySidebar,
                                        QStringLiteral("数据源"));
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

    const QVariant activityVisible =
        settings.scopedValue(SettingsScope::Workspace, QStringLiteral("ui.activityBar.visible"));
    if (activityVisible.isValid())
        m_binder->setActivityBarVisible(activityVisible.toBool());

    const QVariant sideVisible = settings.scopedValue(SettingsScope::Workspace, QStringLiteral("ui.sideBar.visible"));
    if (sideVisible.isValid())
        m_binder->setSideBarVisible(sideVisible.toBool());

    const QVariant sideWidth = settings.scopedValue(SettingsScope::Workspace, QStringLiteral("ui.sideBar.width"));
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

void MainWindow::refreshThemeTokens()
{
    if (!m_theme)
        return;
    // 扩展 token 全集（包 token 优先、模块 token 兜底、按 id 去重）经 IThemeService
    // 一次取齐（裁定 52441633 §三），壳不再自己合并两路来源。
    if (m_themeService)
        m_theme->setExtensionTokens(m_themeService->tokens());

    // 语义色的消费由**画布自己**完成（验收 8）：AxesView::applyPalette 里
    // 从 ThemeManager 取已声明 token 的实色，注入本画布的 Axes；
    // 主题切换会改 palette → PaletteChange → applyPalette，自动重推。
    // 壳不持有跨画布的全局色表（原先那份进程级表已撤），避免多窗口/多产品串色。
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
    if (m_commands || !m_binder)
        return;

    m_commands = std::make_unique<CommandRegistry>();

    // D7 收口：命令只带数据，执行交给注入的回调。回调先试本地 Ribbon 动作，
    // 再按 intentOp 走意图执行器（模块业务意图 + @system/shell 的 UI 意图）。
    m_commands->setExecutor(
        [this](const command::CommandDescriptor &command, QString *error) {
            return executeCommand(command, error);
        });

    // shell UI 命令 op 注册为系统级意图，与模块业务意图走同一条执行路径。
    host.registerSystemHandler(
        QStringLiteral("@system/shell"),
        new ShellIntentHandler([this](const QString &op, QString *error) { return runShellAction(op, error); }, this));

    // 命令接缝注入上下文：模块呈现层（右键/标题栏/设置页）没法反向依赖外壳，
    // 由外壳把"执行"与"when 求值"两个能力挂到 AppContext 上。
    // extra 键只参与本次求值：item.* 是 View 作用域，不进全局上下文。
    m_appCtx.setCommandHooks(
        [this](const QString &id, QString *error) {
            return m_commands ? m_commands->execute(id, error) : false;
        },
        [this](const QString &when, const QHash<QString, QVariant> &extra) {
            if (!m_commands)
                return false;
            QHash<QString, QVariant> values = m_commands->contextValues();
            for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
                values.insert(it.key(), it.value());
            return CommandRegistry::evaluateWhen(when, values);
        });
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

bool MainWindow::executeCommand(const command::CommandDescriptor &command, QString *error)
{
    // 1) 本地 Ribbon 动作：模块动作走 ShellBinder 的 trigger/意图双路径。
    if (!command.localActionId.isEmpty())
    {
        const QString data = command.data;
        const QString op = command.intentOp;
        // dispatchAction 内部先试 triggerAction，失败再发意图；这里保留其语义。
        if (m_binder->dispatchAction(command.localActionId, op, data))
            return true;
        // 本地动作存在但 intentOp 为空 → 无法进一步派发。
        if (op.isEmpty())
        {
            if (error)
                *error = QStringLiteral("命令无法执行: %1").arg(command.localActionId);
            return false;
        }
    }

    // 2) 意图 op：交给宿主执行器（模块业务意图 + @system/shell 的 UI 意图）。
    if (!command.intentOp.isEmpty())
    {
        Intent intent;
        intent.op = command.intentOp;
        if (!command.data.isEmpty())
            intent.params.insert(QStringLiteral("data"), command.data);
        if (m_host && m_host->execute(intent))
            return true;
        if (error)
            *error = QStringLiteral("命令无法执行: %1").arg(command.intentOp);
        return false;
    }

    if (error)
        *error = QStringLiteral("命令无可执行的载荷: %1").arg(command.id);
    return false;
}

bool MainWindow::runShellAction(const QString &op, QString *error)
{
    // shell 命令 op → GUI 动作的集中分派（D7：命令层无代码，这里是壳的 UI 实现）。
    if (op == QStringLiteral("palette.show"))         { showCommandPalette(); return true; }
    if (op == QStringLiteral("quick.switch"))         { showQuickSwitch(); return true; }
    if (op == QStringLiteral("view.toggleSideBar"))   { toggleSideBar(); return true; }
    if (op == QStringLiteral("view.toggleAuxZone"))
    {
        if (m_binder->toggleAuxZone())
            statusBar()->showMessage(m_binder->auxZoneVisible()
                                         ? QStringLiteral("辅助栏已显示")
                                         : QStringLiteral("辅助栏已隐藏"));
        return true;
    }
    if (op == QStringLiteral("view.togglePanel"))
    {
        if (m_binder->toggleBottomPanel())
            statusBar()->showMessage(m_binder->bottomPanelVisible()
                                         ? QStringLiteral("底部面板已显示")
                                         : QStringLiteral("底部面板已隐藏"));
        return true;
    }
    if (op == QStringLiteral("editor.next"))
    {
        const int count = m_binder->centralWidgets().size();
        if (count <= 0)
            return true;
        const int current = qMax(0, m_binder->currentCentralIndex());
        m_binder->activateCentralIndex((current + 1) % count);
        return true;
    }
    if (op == QStringLiteral("editor.close"))
    {
        if (!m_binder->closeCurrentEditor())
            statusBar()->showMessage(QStringLiteral("当前页签不可关闭"));
        return true;
    }
    if (op == QStringLiteral("plot.series.removeSelected"))
    {
        const Selection &selection = m_appCtx.selection();
        const QString key = selection.ids.value(QStringLiteral("seriesKey")).toString();
        if (selection.kind != SelectionKind::Series || key.isEmpty())
        {
            statusBar()->showMessage(QStringLiteral("未选中曲线"));
            return true;
        }
        QVariantMap params;
        params.insert(QStringLiteral("key"), key);
        QVariant nullValue;
        Result<QVariant> result(nullValue);
        if (!m_appCtx.interactions().dispatchViewAction(QStringLiteral("plot.series.remove"),
                                                        params, result) || !result)
            statusBar()->showMessage(QStringLiteral("删除曲线失败"));
        return true;
    }
    if (op == QStringLiteral("preferences.keybindings"))
    {
        openKeybindingDialog();
        return true;
    }
    if (op.startsWith(QStringLiteral("activity.activate.")))
    {
        const QString activityId = op.mid(QStringLiteral("activity.activate.").size());
        m_binder->setActiveActivity(activityId);
        return true;
    }
    if (error)
        *error = QStringLiteral("未知 shell 命令: %1").arg(op);
    return false;
}

void MainWindow::setupStatusBar()
{
    // 订阅模块声明与推送（D8 推模式）：声明建控件，文本变化只改 QLabel。
    connect(m_context, &GuiContext::statusSpecDeclared,
            this, &MainWindow::onStatusSpecDeclared);
    connect(m_context, &GuiContext::statusTextChanged,
            this, &MainWindow::onStatusTextChanged);
    connect(m_context, &GuiContext::statusFlashed,
            this, &MainWindow::onStatusFlashed);
    // 装配前模块可能已声明（取决于 activate 顺序），补建一次。
    for (const StatusSpec &spec : m_context->statusSpecs())
        onStatusSpecDeclared(spec);
    rebuildStatusBarLayout();
}

void MainWindow::onStatusSpecDeclared(const StatusSpec &spec)
{
    if (m_statusLabels.contains(spec.id))
        return;
    QLabel *label = new QLabel(this);
    label->setObjectName(QStringLiteral("status.item.%1").arg(spec.id));
    if (!spec.tooltip.isEmpty())
        label->setToolTip(spec.tooltip);
    label->setText(m_context->statusText(spec.id));
    m_statusLabels.insert(spec.id, label);
    rebuildStatusBarLayout();
}

void MainWindow::onStatusTextChanged(const QString &id, const QString &text)
{
    if (QLabel *label = m_statusLabels.value(id, nullptr))
        label->setText(text);
}

void MainWindow::onStatusFlashed(const QString &text)
{
    statusBar()->showMessage(text);
}

void MainWindow::rebuildStatusBarLayout()
{
    // 按 side 分组、priority 升序：左项永久部件（左侧），右项普通部件（右侧）。
    // 先移除全部再按序加回，项数很少（≈4），重建成本可忽略。
    for (QLabel *label : m_statusLabels)
        statusBar()->removeWidget(label);
    QList<QLabel *> left;
    QList<QLabel *> right;
    for (const StatusSpec &spec : m_context->statusSpecs())
    {
        QLabel *label = m_statusLabels.value(spec.id, nullptr);
        if (!label)
            continue;
        (spec.side == StatusSide::Left ? left : right).append(label);
    }
    auto priorityLess = [this](QLabel *a, QLabel *b) {
        int pa = 0, pb = 0;
        for (const StatusSpec &s : m_context->statusSpecs())
        {
            if (s.id == a->objectName().mid(QStringLiteral("status.item.").size())) pa = s.priority;
            if (s.id == b->objectName().mid(QStringLiteral("status.item.").size())) pb = s.priority;
        }
        return pa < pb;
    };
    std::sort(left.begin(), left.end(), priorityLess);
    std::sort(right.begin(), right.end(), priorityLess);
    for (QLabel *label : left)
        statusBar()->addWidget(label, 1);
    for (QLabel *label : right)
        statusBar()->addPermanentWidget(label, 0);
}

void MainWindow::registerSlotCommands()
{
    QSet<QString> seen;
    int added = 0;
    if (!m_context->slotsService())
        return;
    for (const UiSlot &slot : m_context->slotsService()->allSlots())
    {
        for (const BoundUiAction &bound : m_context->slotsService()->actionsIn(slot.slotId))
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
            // D7：命令只携带纯数据。本地 Ribbon 动作 id + 意图 op + 参数，
            // 执行时由 executeCommand 按此顺序派发（无 handler 回调）。
            command.localActionId = ua.id;
            command.intentOp = ua.intentOp;
            command.data = ua.data;
            if (m_commands->add(command))
                ++added;
        }
    }
    fprintf(stdout, "[cmd] 注册动作命令 %d 条\n", added);
}

void MainWindow::registerShellCommands()
{
    // D7：shell 命令只登记"id + 标题 + op"，不含 lambda。
    // op 即命令 id；执行时由 executeCommand → runShellAction 分派到 GUI 动作。
    auto add = [this](const QString &id, const QString &title, const QString &category,
                      const QString &shortcut, const QString &when) {
        CommandDescriptor command;
        command.id = id;
        command.title = title;
        command.category = category;
        command.shortcut = shortcut;
        command.when = when;
        command.provider = QStringLiteral("app.shell");
        command.intentOp = id;   // shell 命令的 op 就是它自己的 id
        return m_commands->add(command);
    };

    add(QStringLiteral("palette.show"), QStringLiteral("命令面板"), QStringLiteral("视图"),
        QStringLiteral("Ctrl+Shift+P"), QString());
    add(QStringLiteral("quick.switch"), QStringLiteral("快速切换"), QStringLiteral("导航"),
        QStringLiteral("Ctrl+P"), QString());
    add(QStringLiteral("view.toggleSideBar"), QStringLiteral("显示/隐藏左栏"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+B"), QString());
    add(QStringLiteral("view.toggleAuxZone"), QStringLiteral("显示/隐藏辅助栏"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+Alt+B"), QString());
    add(QStringLiteral("view.togglePanel"), QStringLiteral("显示/隐藏底部面板"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+J"), QString());
    add(QStringLiteral("editor.next"), QStringLiteral("下一个编辑器"),
        QStringLiteral("导航"), QStringLiteral("Ctrl+Tab"), QString());
    add(QStringLiteral("editor.close"), QStringLiteral("关闭当前编辑器"),
        QStringLiteral("视图"), QStringLiteral("Ctrl+W"), QString());
    add(QStringLiteral("plot.series.removeSelected"), QStringLiteral("删除曲线"),
        QStringLiteral("绘图"), QString(),
        QStringLiteral("selection.count > 0 && selection.kind == \"series\""));
    add(QStringLiteral("preferences.keybindings"), QStringLiteral("快捷键设置"),
        QStringLiteral("设置"), QString(), QString());

    // activity.activate.* 是数据驱动的动态命令，op 同样用命令 id。
    const QStringList activities = m_binder->activityIds();
    for (const QString &activityId : activities)
    {
        const ActivitySpec *spec =
            m_context->slotsService() ? m_context->slotsService()->activity(activityId) : nullptr;
        const QString label = spec ? spec->label : activityId;
        add(QStringLiteral("activity.activate.%1").arg(activityId),
            QStringLiteral("转到 %1").arg(label), QStringLiteral("导航"), QString(), QString());
    }
}

void MainWindow::updateCommandContext()
{
    if (!m_commands)
        return;
    // N3：when 键统一到契约点号表（WhenKeys），禁止裸驼峰键。
    m_commands->setContextValue(QStringLiteral("activity.active"), m_binder->activeActivity());
    m_commands->setContextValue(QStringLiteral("editor.active"), m_binder->currentCentralTitle());
    m_commands->setContextValue(QStringLiteral("group.active"), QString());
    const Selection &selection = m_appCtx.selection();
    m_commands->setContextValue(QStringLiteral("selection.kind"), selectionKindName(selection.kind));
    m_commands->setContextValue(QStringLiteral("selection.count"),
                                selection.empty() ? 0 : qMax(1, selection.ids.size()));
    m_commands->setContextValue(QStringLiteral("dataset.count"), m_appCtx.data().ids().size());
    m_commands->setContextValue(QStringLiteral("script.running"), false);
    m_commands->setContextValue(QStringLiteral("focus.area"), QStringLiteral("editor"));
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

    const struct { nextpilot::plotscope::registry::Region region; const char *category; } kPaletteAreas[] = {
        {nextpilot::plotscope::registry::Region::PrimarySidebar, "左栏面板"},
        {nextpilot::plotscope::registry::Region::SecondarySidebar, "右侧面板"},
        {nextpilot::plotscope::registry::Region::Panel, "底部面板"},
    };
    for (const auto &entry : kPaletteAreas)
    {
        const QStringList titles = m_binder->panelTitles(entry.region);
        const QList<QWidget *> panels = m_binder->panelWidgets(entry.region);
        const int count = qMin(titles.size(), panels.size());
        const QString regionKey = regionName(entry.region);
        for (int i = 0; i < count; ++i)
        {
            CommandPalette::Item item;
            item.id = QStringLiteral("quick.panel.%1.%2").arg(regionKey, QString::number(i));
            item.title = titles.at(i);
            item.category = QString::fromLatin1(entry.category);
            item.keywords = regionKey;
            QWidget *panel = panels.at(i);
            item.action = [this, panel]() { m_binder->focusPanel(panel); };
            items.append(item);
        }
    }

    for (const QString &activityId : m_binder->activityIds())
    {
        const ActivitySpec *spec =
            m_context->slotsService() ? m_context->slotsService()->activity(activityId) : nullptr;
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
    const QVariant raw =
        m_host->app().settings().scopedValue(SettingsScope::Workspace, QStringLiteral("ui.keybindings"));
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
    m_host->app().settings().setScopedValue(SettingsScope::Workspace, QStringLiteral("ui.keybindings"),
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
    const QString sequence = QKeySequence(key->keyCombination()).toString(QKeySequence::PortableText);
    if (sequence.isEmpty())
        return QMainWindow::eventFilter(watched, event);

    bool scriptTabs = false;
    for (QWidget *w = QApplication::focusWidget(); w; w = w->parentWidget())
    {
        if (!QString::fromLatin1(w->metaObject()->className()).contains(QStringLiteral("ScriptEditorPanel")))
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
    return m_host && !m_host->app().settings().json(SettingsScope::Workspace, QStringLiteral("window/state")).isEmpty();
}

} // namespace nextpilot::plotscope::gui

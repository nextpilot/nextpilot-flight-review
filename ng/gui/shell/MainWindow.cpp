#include "MainWindow.h"

#include "GuiContext.h"
#include "ShellBinder.h"

#include "AppContext.h"
#include "ModuleHost.h"
#include "UiSlotRegistry.h"

#include <QMenuBar>
#include <QStatusBar>

using namespace ng::core;
using namespace ng::host;

namespace ng::gui
{

MainWindow::MainWindow(AppContext &appCtx, QWidget *parent)
    : QMainWindow(parent), m_appCtx(appCtx)
{
    setWindowTitle(QStringLiteral("PlotScope NG"));
    resize(1280, 800);

    // 菜单栏（薄壳：真正的动作由器官/工具箱声明，这里只放容器）
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->setObjectName(QStringLiteral("menu.file"));

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
    // app 骨架提供的标准插槽。工具箱与器官往这些槽里填内容，
    // 骨架代码此后不再变动。
    UiSlotRegistry &slotReg = m_context->slotRegistry();
    slotReg.declareSlot(QStringLiteral("ribbon.page.home"), QStringLiteral("主页"),
                        QStringLiteral("app.skeleton"), SlotKind::RibbonPage);
    slotReg.declareSlot(QStringLiteral("ribbon.group.home.data"), QStringLiteral("数据"),
                        QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                        QStringLiteral("ribbon.page.home"));
    slotReg.declareSlot(QStringLiteral("ribbon.group.home.view"), QStringLiteral("视图"),
                        QStringLiteral("app.skeleton"), SlotKind::RibbonGroup,
                        QStringLiteral("ribbon.page.home"));
    slotReg.declareSlot(QStringLiteral("dock.left"), QStringLiteral("数据集"),
                        QStringLiteral("app.skeleton"), SlotKind::DockPanel);
    slotReg.declareSlot(QStringLiteral("dock.right"), QStringLiteral("属性"),
                        QStringLiteral("app.skeleton"), SlotKind::DockPanel);
    slotReg.declareSlot(QStringLiteral("canvas.center"), QStringLiteral("画布"),
                        QStringLiteral("app.skeleton"), SlotKind::Canvas);
    slotReg.declareSlot(QStringLiteral("menu.plot"), QStringLiteral("绘图"),
                        QStringLiteral("app.skeleton"), SlotKind::Menu);
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
    setMenuBar(nullptr); // 有 ribbon 就不留同名菜单栏，避免两排并排
    if (QWidget *ribbon = m_binder->createRibbon())
        setMenuWidget(ribbon); // SARibbon 占菜单栏位置，成为窗口顶部区

    // 3) 装器官（GUI 模式：会调用 onAttach，器官可自带界面入口）
    host.activate(QStringList(), m_context);

    // 4) ribbon 重建：把工具箱的 UI 贡献 + 器官的本地入口一起渲染出来
    m_binder->rebuildRibbon();

    // 5) 恢复上次的窗口状态（首次运行则安全跳过）
    restoreState();
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

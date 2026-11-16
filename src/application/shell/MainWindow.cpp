#include "MainWindow.h"
#include "ShellCommands.h"
#include "SettingsManager.h"
#include "ThemeManager.h"
#include "Component.h"

#include <QCloseEvent>
#include <QEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>

#include "ps_kernel.h"

#include "SARibbonToolButton.h"




MainWindow::~MainWindow() = default;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_logs(this), m_docks(this),
      m_ribbon(this, &m_commands, &m_extensions.registry(), this)
{
    setWindowTitle(QStringLiteral("PlotScope 0.1.0 — 绘图分析"));
    resize(1440, 880);

    ps::components::HostContext hostContext;
    hostContext.window = [this] { return static_cast<QWidget *>(this); };
    hostContext.log = [this](const QString &text) { log(text); };
    hostContext.configDir = [] {
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    };
    hostContext.setPlayhead = [this](double t) {
        ps::commands::CommandContext ctx;
        ctx.arg = QString::number(t);
        m_commands.invoke(QStringLiteral("app.playhead"), ctx);
    };
    hostContext.invoke = [this](const QString &id, const QString &arg) {
        ps::commands::CommandContext ctx;
        ctx.arg = arg;
        return m_commands.invoke(id, ctx);
    };
    m_extensions.activate(hostContext);
    m_session.setDockBridge(
        [this] { return m_docks.saveLayout(); },
        [this](const QByteArray &state) { return m_docks.restoreLayout(state); });
    shell::registerCommands(*this);
    m_docks.registerCommand(m_commands);
    SettingsManager::instance().registerCommand(m_commands, this);

    // Ribbon 构建期就要连页签信号，先建 QTabWidget，避免 connect nullptr 警告。
    m_pages = new QTabWidget(this);
    for (const auto &hook : m_extensions.registry().windowHooks())
        hook(this);

    setMenuWidget(m_ribbon.create());
    m_docks.createFromRegistry(m_extensions.registry());
    m_docks.applyTheme();
    m_docks.darkenExisting();
    if (auto *view = findChild<QPlainTextEdit *>(QStringLiteral("dock.log.view")))
        m_logs.setLogView(view);
    m_commands.invoke(QStringLiteral("app.ready"), {});
    if (m_session.layoutPath().isEmpty())
        m_session.setLayoutPath(SessionManager::defaultLayoutPath());
    m_session.restoreWindow(this);
    m_logs.setStatusBar(statusBar());
    for (const QString &error : m_extensions.errors())
        m_logs.warn(error);
    m_session.bindCommandState(m_commands, [this] { refreshTitle(); });

    // 内核连通性：状态栏直接读 ABI，证明 C ABI 已经打通
    const QString kernelInfo =
        QStringLiteral("内核 ps-abi %1 · ABI v%2 · selftest %3")
            .arg(QString::fromUtf8(ps_kernel_version()))
            .arg(ps_abi_version())
            .arg(ps_abi_selftest() == 0 ? QStringLiteral("通过") : QStringLiteral("失败"));
    m_kernelInfo = kernelInfo;
    statusBar()->showMessage(kernelInfo);
    m_commands.bindShortcuts(this);
    refreshTitle();
    ThemeManager::refreshWidgetStyles(this);

    log(QStringLiteral("启动完成：%1").arg(kernelInfo));
}

void MainWindow::refreshTitle()
{
    const QString base = QStringLiteral("PlotScope 0.1.0 — 绘图分析");
    setWindowTitle(m_session.isDirty() ? base + QStringLiteral(" *") : base);
}

void MainWindow::applyTheme()
{
    m_ribbon.applyTheme();
    m_docks.applyTheme();
    ThemeManager::refreshWidgetStyles(this);
    update();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { m_docks.repositionHandles(); });
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    const bool autoSave = SettingsManager::instance().restoreLayout();
    if (m_session.isDirty() && !autoSave)
    {
        const auto answer = QMessageBox::question(this, QStringLiteral("退出"),
            QStringLiteral("当前布局尚未保存，是否保存后退出？"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
        {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Save &&
            !m_commands.invoke(QStringLiteral("layout.save"),
                               ps::commands::CommandContext{this, m_session.layoutPath()}))
        {
            event->ignore();
            return;
        }
    }
    m_commands.invoke(QStringLiteral("app.beforeClose"), {});
    m_session.saveWindow(this);
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Ribbon 按钮的自定义 tooltip：开着它会在鼠标下方盖住分组标题，改走状态栏
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
    {
        if (qobject_cast<SARibbonToolButton *>(watched))
        {
            if (event->type() == QEvent::Enter)
            {
                const QString tip = watched->property("rbTip").toString();
                if (!tip.isEmpty())
                    statusBar()->showMessage(tip);
            }
            else
            {
                statusBar()->showMessage(m_kernelInfo);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}





void MainWindow::log(const QString &text)
{
    m_logs.info(text);
}

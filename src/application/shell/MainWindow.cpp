#include "MainWindow.h"
#include "SettingsDialog.h"
#include "ShellCommands.h"
#include "SettingsManager.h"
#include "Component.h"

#include <QAction>
#include <QtEndian>
#include <QTreeWidgetItemIterator>
#include <cstring>
#include <algorithm>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QToolButton>
#include <QButtonGroup>
#include <QScrollArea>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileDialog>
#include <QTimer>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QDir>
#include <QStandardPaths>
#include <QLabel>
#include <QMessageBox>
#include <QMenu>
#include <QWidgetAction>
#include <QMouseEvent>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QCloseEvent>
#include <QDateTime>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QResizeEvent>
#include <QStyle>
#include <QHash>
#include <QRegularExpression>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <QPixmap>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>

#include <QTreeWidget>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QLineEdit>
#include <QEvent>
#include <QDrag>
#include <QMimeData>
#include <QDropEvent>
#include <QListWidget>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ps_kernel.h"

#include "SARibbonToolButton.h"




MainWindow::~MainWindow() = default;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_feedback(this), m_docks(this),
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
        m_commands.invoke(QStringLiteral("plot.setPlayhead"), ctx);
    };
    m_extensions.activate(hostContext);
    m_feedback.attach(statusBar(), nullptr);
    shell::registerCommands(*this);

    // Ribbon 构建期就要连页签信号，先建 QTabWidget，避免 connect nullptr 警告。
    m_pages = new QTabWidget(this);
    for (const auto &hook : m_extensions.registry().windowHooks())
        hook(this);

    setMenuWidget(m_ribbon.create());
    m_docks.createFromRegistry(m_extensions.registry());
    m_docks.darkenExisting();
    m_commands.invoke(QStringLiteral("plot.refreshTree"), {});
    m_session.setLayoutPath(defaultLayoutPath());
    m_session.restoreWindow(this);
    m_feedback.attach(statusBar(), m_logView);
    for (const QString &error : m_extensions.errors())
        m_feedback.warn(error);
    shell::bindSession(*this);

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

    log(QStringLiteral("启动完成：%1").arg(kernelInfo));
}

void MainWindow::refreshTitle()
{
    const QString base = QStringLiteral("PlotScope 0.1.0 — 绘图分析");
    setWindowTitle(m_session.isDirty() ? base + QStringLiteral(" *") : base);
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
            !m_commands.invoke(QStringLiteral("layout.save"), ps::commands::CommandContext{this, {}}))
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





QString MainWindow::defaultLayoutPath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.plotscope");
    return dir + QStringLiteral("/last.psl");
}

void MainWindow::log(const QString &text)
{
    m_feedback.info(text);
}

void MainWindow::attachLogView(QPlainTextEdit *view)
{
    m_logView = view;
    m_feedback.setLogView(view);
}


void MainWindow::toggleDockMax(QDockWidget *dock)
{
    m_docks.toggleMax(dock);
}

void MainWindow::restoreDockMax()
{
    m_docks.restoreMax();
}

void MainWindow::dockWidgetToArea(QDockWidget *dock, Qt::DockWidgetArea area)
{
    m_docks.toArea(dock, area);
}

void MainWindow::collapseDockWidget(QDockWidget *dock)
{
    m_docks.collapse(dock);
}

void MainWindow::showDockMenu(QToolButton *btn, QMenu *menu)
{
    m_docks.showMenu(btn, menu);
}

QWidget *MainWindow::createDockTitleBar(QDockWidget *dock)
{
    return m_docks.createTitleBar(dock);
}

#include "MainWindow.h"
#include "SettingsDialog.h"
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
#include <QTableWidget>
#include <QToolBar>

static QString dockIconResource(const QDockWidget *d);
static void execDockMenu(QToolButton *btn, QMenu *menu);

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

namespace {

// Ribbon 里的图表选择卡：用缩略图表达图型，比一排纯文字按钮更接近 Office/MATLAB 的样式库
void compactValueTable(QTableWidget *t)
{
    if (!t)
        return;
    t->setAlternatingRowColors(false);
    t->setWordWrap(false);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(21);
    t->verticalHeader()->setMinimumSectionSize(21);
    t->horizontalHeader()->setFixedHeight(23);
    QFont f = t->font();
    f.setPixelSize(12);
    t->setFont(f);
}

} // namespace
#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonThemeManager.h"
#include "SARibbonToolButton.h"

static void darkenDocks(MainWindow *w);
namespace { void dockToArea(QDockWidget *dock, MainWindow *win, Qt::DockWidgetArea area); }

// 只重写 sizeHint 的标题栏：QDockWidget 按 sizeHint 预留高度，固定 height 不算数，
// 32px 的 bar 只留 24px 空间就会压住正文第一行。

class DockTitleBar : public QWidget
{
public:
    explicit DockTitleBar(MainWindow *win, QDockWidget *dock)
        : QWidget(dock), m_win(win), m_dock(dock)
    {
        setFixedHeight(32);
        setCursor(Qt::SizeAllCursor);
        setToolTip(QStringLiteral("拖动到窗口边缘可停靠，双击切换浮动"));
    }
    QSize sizeHint() const override { return QSize(QWidget::sizeHint().width(), 32); }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(e);
            return;
        }
        m_pressPos = e->globalPosition().toPoint();
        m_dragging = false;
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!(e->buttons() & Qt::LeftButton) || !m_dock)
        {
            QWidget::mouseMoveEvent(e);
            return;
        }
        const QPoint gp = e->globalPosition().toPoint();
        if (!m_dragging && (gp - m_pressPos).manhattanLength() < 6)
            return;
        if (!m_dragging)
        {
            m_dragging = true;
            m_offset = gp - m_dock->frameGeometry().topLeft();
            if (!m_dock->isFloating())
                m_dock->setFloating(true);
        }
        m_dock->move(gp - m_offset);
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || !m_dragging)
        {
            QWidget::mouseReleaseEvent(e);
            return;
        }
        m_dragging = false;
        if (!m_dock || !m_win)
            return;
        const QPoint gp = e->globalPosition().toPoint();
        const QRect winRect = m_win->frameGeometry();
        if (!winRect.adjusted(-24, -24, 24, 24).contains(gp))
            return; // 扔到窗口外：保持浮动
        const int dl = qAbs(gp.x() - winRect.left());
        const int dr = qAbs(gp.x() - winRect.right());
        const int db = qAbs(gp.y() - winRect.bottom());
        const int dt = qAbs(gp.y() - winRect.top());
        const int minD = std::min(std::min(dl, dr), std::min(db, dt));
        if (minD > 150)
            return; // 中间区域：保持浮动，不强行吸边
        Qt::DockWidgetArea area = dr <= dl ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea;
        if (db < std::min(dl, dr) && db <= dt)
            area = Qt::BottomDockWidgetArea;
        else if (dt < std::min(dl, dr) && dt < db)
            area = Qt::TopDockWidgetArea;
        dockToArea(m_dock, m_win, area);
        e->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_dock)
        {
            m_dock->setFloating(!m_dock->isFloating());
            e->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(e);
    }

private:
    MainWindow *m_win = nullptr;
    QDockWidget *m_dock = nullptr;
    QPoint m_pressPos;
    QPoint m_offset;
    bool m_dragging = false;
};

// Visual Studio 式自动隐藏页签：收成一条竖直窄条，标题竖排，点一下展开
class DockAutoHideTab : public QWidget
{
public:
    using Handler = std::function<void()>;

    DockAutoHideTab(QWidget *parent, const QString &title, Qt::DockWidgetArea area, Handler handler)
        : QWidget(parent), m_title(title), m_handler(std::move(handler))
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        setToolTip(QStringLiteral("展开 %1").arg(title));
        setArea(area);
    }

    void setArea(Qt::DockWidgetArea area)
    {
        m_area = area;
        const int h = std::clamp(48 + int(m_title.size()) * 13, 96, 200);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
            setFixedSize(26, h);
        else
            setFixedSize(h, 26);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(m_hover ? QColor("#1f2a33") : QColor("#161b20"));
        p.setPen(QPen(m_hover ? QColor("#f2b134") : QColor("#2d3841"), 1));
        p.drawRoundedRect(r, 2, 2);

        p.setPen(QColor("#f2b134"));
        QFont arrow = font();
        arrow.setPixelSize(12);
        p.setFont(arrow);
        const QString mark = (m_area == Qt::LeftDockWidgetArea) ? QStringLiteral("›")
                            : (m_area == Qt::RightDockWidgetArea) ? QStringLiteral("‹")
                            : (m_area == Qt::TopDockWidgetArea) ? QStringLiteral("▼") : QStringLiteral("▲");
        if (m_area == Qt::LeftDockWidgetArea || m_area == Qt::RightDockWidgetArea)
            p.drawText(QRectF(0, 2, width(), 16), Qt::AlignCenter, mark);
        else
            p.drawText(QRectF(2, 0, 16, height()), Qt::AlignCenter, mark);

        QFont f = font();
        f.setPixelSize(11);
        p.setFont(f);
        p.setPen(QColor("#c9d1d9"));
        p.save();
        if (m_area == Qt::LeftDockWidgetArea || m_area == Qt::RightDockWidgetArea)
        {
            p.translate(width() / 2.0, height() / 2.0);
            p.rotate(m_area == Qt::LeftDockWidgetArea ? 90 : -90);
            p.drawText(QRectF(-height() / 2.0 + 10, -width() / 2.0, height() - 16, width()),
                       Qt::AlignCenter, m_title);
        }
        else
        {
            p.drawText(QRectF(18, 0, width() - 22, height()), Qt::AlignCenter, m_title);
        }
        p.restore();
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_handler)
            m_handler();
    }

private:
    QString m_title;
    Handler m_handler;
    Qt::DockWidgetArea m_area = Qt::RightDockWidgetArea;
    bool m_hover = false;
};

namespace {

QHash<QDockWidget *, QWidget *> g_dockMinHandles;

void positionDockHandle(QDockWidget *dock, QWidget *btn, MainWindow *win, int index = 0, int total = 1)
{
    if (!dock || !btn || !win)
        return;
    const QRect cg = win->centralWidget()->geometry();
    const Qt::DockWidgetArea area = win->dockWidgetArea(dock);
    const int W = 26, GAP = 6;
    const int H = qMax(96, btn->height());
    if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea)
    {
        if (auto *tab = dynamic_cast<DockAutoHideTab *>(btn))
            tab->setArea(area);
        const int x = area == Qt::LeftDockWidgetArea ? cg.left() + 2 : cg.right() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int y = cg.center().y() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    else
    {
        if (auto *tab = dynamic_cast<DockAutoHideTab *>(btn))
            tab->setArea(area);
        const int y = area == Qt::TopDockWidgetArea ? cg.top() + 2 : cg.bottom() - W - 2;
        const int block = total * H + (total - 1) * GAP;
        const int x = cg.center().x() - block / 2 + index * (H + GAP);
        btn->move(x, y);
    }
    btn->raise();
}

void repositionDockHandles(MainWindow *win)
{
    if (!win)
        return;
    QHash<int, int> totals, seen;
    for (auto it = g_dockMinHandles.begin(); it != g_dockMinHandles.end(); ++it)
        if (it.value() && it.value()->isVisible())
            ++totals[static_cast<int>(win->dockWidgetArea(it.key()))];
    for (auto it = g_dockMinHandles.begin(); it != g_dockMinHandles.end(); ++it)
    {
        if (!it.value() || !it.value()->isVisible())
            continue;
        const int area = static_cast<int>(win->dockWidgetArea(it.key()));
        const int idx = seen[area]++;
        positionDockHandle(it.key(), it.value(), win, idx, qMax(1, totals.value(area, 1)));
    }
}

void collapseDock(QDockWidget *dock, MainWindow *win)
{
    if (!dock || !win)
        return;
    QWidget *btn = g_dockMinHandles.value(dock);
    if (!btn)
    {
        auto *tab = new DockAutoHideTab(win, dock->windowTitle(), win->dockWidgetArea(dock),
            [dock, win] {
                dock->show();
                dock->raise();
                if (auto *h = g_dockMinHandles.value(dock))
                    h->hide();
                repositionDockHandles(win);
            });
        QObject::connect(dock, &QDockWidget::visibilityChanged, tab, [tab](bool visible) {
            if (visible)
                tab->hide();
        });
        g_dockMinHandles.insert(dock, tab);
        btn = tab;
    }
    dock->hide();
    btn->show();
    repositionDockHandles(win);
}

void dockToArea(QDockWidget *dock, MainWindow *win, Qt::DockWidgetArea area)
{
    if (!dock || !win)
        return;
    const bool floating = dock->isFloating();
    if (floating)
        dock->setFloating(false);
    win->removeDockWidget(dock);
    win->addDockWidget(area, dock);
    dock->show();
    dock->raise();
}


// Word 插入表格式布局选择：悬停框选 m×n，点击即应用
// 页内 m×n 网格：QSplitter 只能做 n×1 / 1×n，这里补真正的二维布局


} // namespace

// 字段树拖拽：Item 里存的是内核通道指针，默认 QTreeWidget 的 MIME 带不过去，自己塞一条


// 源行名称按可用宽度省略中部，避免把右侧操作图标挤出去
class ElideLabel : public QLabel
{
public:
    ElideLabel(const QString &full, QWidget *parent) : QLabel(full, parent), m_full(full) {}
protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QLabel::resizeEvent(e);
        setText(fontMetrics().elidedText(m_full, Qt::ElideMiddle, width()));
    }
private:
    QString m_full;
};

MainWindow::~MainWindow() = default;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
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
    for (const QString &error : m_extensions.errors())
        qWarning("[extension] %s", qPrintable(error));
    registerCommands();

    // Ribbon 构建期就要连页签信号，先建 QTabWidget，避免 connect nullptr 警告。
    m_pages = new QTabWidget(this);
    for (const auto &hook : m_extensions.registry().windowHooks())
        hook(this);

    setMenuWidget(createRibbon());
    createDocks();
    darkenDocks(this);
    m_commands.invoke(QStringLiteral("plot.refreshTree"), {});

    // 内核连通性：状态栏直接读 ABI，证明 C ABI 已经打通
    const QString kernelInfo =
        QStringLiteral("内核 ps-abi %1 · ABI v%2 · selftest %3")
            .arg(QString::fromUtf8(ps_kernel_version()))
            .arg(ps_abi_version())
            .arg(ps_abi_selftest() == 0 ? QStringLiteral("通过") : QStringLiteral("失败"));
    m_kernelInfo = kernelInfo;
    statusBar()->showMessage(kernelInfo);

    // PJ 习惯：Ctrl+S 存当前布局，Ctrl+Shift+O 打开布局
    auto *saveSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this);
    connect(saveSc, &QShortcut::activated, this, [this] {
        m_commands.invoke(QStringLiteral("layout.save"), {});
    });
    auto *openSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this);
    connect(openSc, &QShortcut::activated, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty()) {
            ps::commands::CommandContext ctx;
            ctx.arg = f;
            m_commands.invoke(QStringLiteral("layout.import"), ctx);
        }
    });

    log(QStringLiteral("启动完成：%1").arg(kernelInfo));
}

void MainWindow::registerCommands()
{
    using Ctx = ps::commands::CommandContext;

    m_commands.add(QStringLiteral("file.open"), [this](const Ctx &) {
        const QStringList fs = QFileDialog::getOpenFileNames(this, QStringLiteral("打开文件"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        for (const QString &f : fs) {
            ps::commands::CommandContext ctx;
            ctx.arg = f;
            m_commands.invoke(QStringLiteral("data.openPath"), ctx);
        }
    });
    m_commands.add(QStringLiteral("file.importLayout"), [this](const Ctx &) {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty()) {
            ps::commands::CommandContext ctx;
            ctx.arg = f;
            m_commands.invoke(QStringLiteral("layout.import"), ctx);
        }
    });
    m_commands.add(QStringLiteral("file.saveLayout"), [this](const Ctx &) {
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty()) {
            ps::commands::CommandContext ctx;
            ctx.arg = f;
            m_commands.invoke(QStringLiteral("layout.save"), ctx);
        }
    });
    m_commands.add(QStringLiteral("file.exportSettings"), [this](const Ctx &) {
        log(QStringLiteral("导出设置：M1 接 YAML"));
    });
    m_commands.add(QStringLiteral("file.exportData"), [this](const Ctx &) {
        log(QStringLiteral("导出数据：M1 接 CSV/NDF"));
    });
    m_commands.add(QStringLiteral("file.screenshot"), [this](const Ctx &) {
        QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
        if (!page)
            return;
        const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("截图另存为"), {},
            QStringLiteral("PNG (*.png)"));
        if (f.isEmpty())
            return;
        if (page->grab().save(f))
            log(QStringLiteral("已截图：%1").arg(f));
    });

    m_commands.add(QStringLiteral("file.settings"), [this](const Ctx &) {
        SettingsDialog dlg(this);
        dlg.exec();
    });

    m_commands.add(QStringLiteral("view.toggleDock"), [this](const Ctx &ctx) {
        if (auto *d = findChild<QDockWidget *>(ctx.arg))
            d->setVisible(!d->isVisible());
    });
}

QWidget *MainWindow::createRibbon()
{
    // Ribbon 结构层交给 SARibbon（MIT，vendored 在 src/3dparty/saribbon）：
    // 页签溢出、键盘导航、面板布局都是它的事；我们只负责配色与命令。
    auto *ribbon = new SARibbonBar(this);
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleLooseSingleRow);
    // 有 Ribbon 就不要再留一条同名 QMenuBar，两排"文件/数据/绘图"并排是最刺眼的不协调
    if (auto *mb = menuBar())
        mb->hide();
    ribbon->setTitleVisible(false);       // 窗口标题在 titlebar，不占 ribbon 高度
    // 标题带直接压成 0：留 30px 空行只会把页签往下推，组标题也被挤掉
    ribbon->setTitleBarHeight(0, true);
    ribbon->setTabBarHeight(26);
    // 收缩：右下角 chevron + 双击页签，都能把面板收起只留页签行
    ribbon->showMinimumModeButton(true);
    ribbon->setTabDoubleClickToMinimumMode(true);
    ribbon->setPanelTitleHeight(14);
    // 分组标题：vendored 布局把标题画在面板底边内，category 高度必须把它算进去
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setPanelToolButtonIconSize(QSize(18, 18), QSize(26, 26));
    ribbon->setPanelSpacing(3);
    ribbon->setCategoryHeight(78, true); // 图标 23 + 内边距 + 分组标题 14，留出基线余量

    // 大按钮同样只留图标：分组标题已说明用途，图标下再叠一行字就是原型里最挤的部分
    auto addLarge = [](SARibbonPanel *panel, const QString &text, const QString &icon,
                       std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx);
        a->setToolTip(text);
        a->setProperty("rbIconOnly", true);
        if (!icon.isEmpty())
        {
            a->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(icon)));
            a->setText(QString());
        }
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addLargeAction(a);
    };
    // 小按钮：只有 1~2 个字才显示文字，其余只留图标 + tooltip（图标 16px 撑得住）
    auto addSmall = [](SARibbonPanel *panel, const QString &text, const QString &icon,
                       std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx);
        a->setToolTip(text);
        a->setProperty("rbIconOnly", true);
        if (!icon.isEmpty())
        {
            a->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(icon)));
            a->setText(QString());
        }
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addSmallAction(a);
    };

    auto run = [this](const QString &id, const QString &arg = QString()) {
        return [this, id, arg] { m_commands.invoke(id, ps::commands::CommandContext{this, arg}); };
    };

    // 组件贡献的菜单页：一个页面可拆多个面板；页内动作只写命令 ID。
    auto renderPage = [&](const QString &title, int order = 1000) -> bool {
        bool any = false;
        SARibbonCategory *cat = nullptr;
        for (const auto &page : m_extensions.registry().ribbon())
        {
            if (page.page != title || page.order > order)
                continue;
            if (!cat)
            {
                cat = ribbon->addCategoryPage(title);
                any = true;
            }
            const QVector<ps::components::RibbonPanel> panels =
                page.panels.isEmpty() ? QVector<ps::components::RibbonPanel>{
                    ps::components::RibbonPanel{page.panel, page.items}} : page.panels;
            for (const auto &panelSpec : panels)
            {
                auto *panel = cat->addPanel(panelSpec.title);
                if (panelSpec.build)
                {
                    panelSpec.build(panel);
                    continue;
                }
                for (const auto &item : panelSpec.items)
                {
                    auto fn = [this, action = item.action, arg = item.arg] {
                        if (!m_commands.invoke(action, ps::commands::CommandContext{this, arg}))
                            m_extensions.registry().invoke(action, ps::components::ActionContext{this});
                    };
                    if (item.large)
                        addLarge(panel, item.label, item.icon, fn, this);
                    else
                        addSmall(panel, item.label, item.icon, fn, this);
                }
            }
        }
        return any;
    };

    // 业务页顺序由 contribution 注册顺序决定，shell 不写死页名
    QVector<QPair<int, QString>> pages;
    for (const auto &page : m_extensions.registry().ribbon())
    {
        if (page.page == QLatin1String("视图"))
            continue;
        auto it = std::find_if(pages.begin(), pages.end(),
                               [&](const QPair<int, QString> &p) { return p.second == page.page; });
        if (it == pages.end())
            pages.append({page.order, page.page});
        else if (page.order < it->first)
            it->first = page.order;
    }
    std::sort(pages.begin(), pages.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.first != b.first ? a.first < b.first : a.second < b.second;
              });
    for (const auto &page : pages)
        renderPage(page.second, 1000000);

    // 视图：按 Dock 贡献表生成，shell 不写死任何 dock id
    if (SARibbonCategory *viewCat = ribbon->addCategoryPage(QStringLiteral("视图")))
    {
        auto *panel = viewCat->addPanel(QStringLiteral("面板"));
        for (const auto &dock : m_extensions.registry().docks())
            addSmall(panel, dock.title, {}, run(QStringLiteral("view.toggleDock"), dock.id), this);
    }

    // 右侧芯片位：只读状态摘要，跟随实际数据源刷新
    auto *rg = ribbon->rightButtonGroup();
    auto *chipSource = new QAction(QStringLiteral("无数据"), this);
    chipSource->setObjectName(QStringLiteral("ps.chip.source"));
    auto *chipLod = new QAction(QStringLiteral("LOD: Rust"), this);
    chipLod->setObjectName(QStringLiteral("ps.chip.lod"));
    for (QAction *a : {chipSource, chipLod})
    {
        a->setEnabled(false);
        rg->addAction(a);
    }

    // 自绘 SVG 图标：没有图标时 SARibbon 会在按钮左侧留空方块，观感直接崩
    {
        static const QHash<QString, QString> kIcon = {
            {QStringLiteral("打开文件"), QStringLiteral("open")},
            {QStringLiteral("导入布局"), QStringLiteral("import")},
            {QStringLiteral("设置"), QStringLiteral("settings")},
            {QStringLiteral("保存布局"), QStringLiteral("export")},
            {QStringLiteral("导出布局"), QStringLiteral("export")},
            {QStringLiteral("导出设置"), QStringLiteral("export_cfg")},
            {QStringLiteral("导出数据"), QStringLiteral("export_data")},
            {QStringLiteral("截图当前页"), QStringLiteral("screenshot")},
            {QStringLiteral("打开数据源"), QStringLiteral("datasource")},
            {QStringLiteral("全部开始"), QStringLiteral("play")},
            {QStringLiteral("全部暂停"), QStringLiteral("pause")},
            {QStringLiteral("自动 Y"), QStringLiteral("autoy")},
            {QStringLiteral("游标"), QStringLiteral("cursor")},
            {QStringLiteral("复位视图"), QStringLiteral("reset")},
            {QStringLiteral("新建 Loader"), QStringLiteral("loader")},
            {QStringLiteral("帧模板 YAML"), QStringLiteral("yaml")},
            {QStringLiteral("校验"), QStringLiteral("validate")},
            {QStringLiteral("数据源"), QStringLiteral("datasource")},
            {QStringLiteral("属性"), QStringLiteral("prop")},
            {QStringLiteral("脚本"), QStringLiteral("script")},
            {QStringLiteral("日志"), QStringLiteral("log")},
            {QStringLiteral("视频"), QStringLiteral("play")},
            {QStringLiteral("网格"), QStringLiteral("grid")},
            {QStringLiteral("边框"), QStringLiteral("box")},
            {QStringLiteral("图例"), QStringLiteral("legend")},
            {QStringLiteral("框选"), QStringLiteral("cursor")},
            {QStringLiteral("放大"), QStringLiteral("zoom_in")},
            {QStringLiteral("缩小"), QStringLiteral("zoom_out")},
            {QStringLiteral("最近"), QStringLiteral("import")},
            {QStringLiteral("导出快照"), QStringLiteral("export_data")},
            {QStringLiteral("重载"), QStringLiteral("reset")},
            {QStringLiteral("复位"), QStringLiteral("reset")},
            {QStringLiteral("\u25be"), QStringLiteral("arrow_more")},
        };
        // QAction 的 parent 是 this（不是 ribbon），按 ribbon 找会一个都拿不到
        for (auto *a : findChildren<QAction *>())
        {
            const auto it = kIcon.find(a->text());
            if (it != kIcon.end())
            {
                a->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(it.value())));
                if (a->property("rbIconOnly").toBool())
                    a->setText(QString());

            }
        }
    }

    if (auto *ab = ribbon->applicationButton())
        ab->hide(); // 左上角默认有个空按钮，留白很难看

    // 悬停提示改状态栏：QToolTip 会飘在按钮下方，正好盖住分组标题
    for (auto *b : ribbon->findChildren<SARibbonToolButton *>())
    {
        b->setToolTip(QString());
        if (auto *a = b->defaultAction())
        {
            b->setProperty("rbTip", a->toolTip());
            a->setToolTip(QString());
        }
        b->installEventFilter(this);
    }

    // 面板本体撑到 68 px：SARibbon 的 category 高度不会自动把 panel 拉满
    for (auto *p : ribbon->findChildren<SARibbonPanel *>())
    {
        p->setMinimumHeight(56);
        if (auto *l = p->layout())
            l->setContentsMargins(4, 4, 9, 4);
    }

    // 大按钮 54×54、小按钮单行 22：只用 QSS 分不开两种形态，先打属性标记
    for (auto *b : ribbon->findChildren<SARibbonToolButton *>())
    {
        b->setProperty("rbLarge", b->buttonType() == SARibbonToolButton::LargeButton);
    }

    // 静态库里资源要显式初始化，否则 dark 主题静默回退浅色 QSS
    Q_INIT_RESOURCE(SARibbonResource);
    SA::applyRibbonTheme(this, ribbon, SARibbonTheme::RibbonThemeDark);
    // 视觉对齐 design/prototype/PlotScope-工作区原型.html 的 ribbon 段：
    // 扁平、无边框、琥珀色选中，压掉 Office 味的蓝色和内边距
    ribbon->setStyleSheet(ribbon->styleSheet() + QStringLiteral(R"(
        SARibbonBar { background:#141a1f; border-bottom:1px solid #232b33; }
        SARibbonCategory { background:#141a1f; border:0; }
        SARibbonPanel { background:#141a1f; border:0; border-right:1px solid #1b232a; }
        SARibbonPanelLabel { color:#7d8b96; font-size:10px; background:transparent; padding-bottom:3px; }
        SARibbonToolButton {
            background:transparent; border:1px solid transparent; border-radius:0;
            color:#8494a0; font-size:12px; padding:0; margin:0;
        }
        SARibbonToolButton[rbLarge="true"] { min-width:52px; max-width:60px; min-height:48px; max-height:48px; }
        SARibbonToolButton[rbLarge="false"] { min-height:21px; max-height:22px; padding:2px 8px; }
        SARibbonToolButton:hover { background:#1b232a; color:#c8d2da; border-color:#222b33; }
        SARibbonToolButton:pressed { background:#212a31; }
        SARibbonToolButton:checked { background:#212a31; color:#f2b134; border-color:#222b33; }
        SARibbonToolButton:disabled { color:#4a545d; }
        SARibbonToolButton::menu-indicator { image:none; width:0; }
        SARibbonSeparatorWidget { background:#1b232a; }
        SARibbonPanelOptionButton { background:transparent; border:0; }
        SARibbonPanelOptionButton:hover { background:#1b232a; }
        #objSARibbonTabBar { background:#141a1f; border:0; }
        #objSARibbonTabBar::tab {
            background:transparent; color:#5d6c77; font-size:11px;
            padding:0 11px; margin:0 1px; border:0; border-bottom:2px solid transparent;
            min-width:0;
        }
        #objSARibbonTabBar::tab:hover { color:#c8d2da; background:#1a2127; }
        #objSARibbonTabBar::tab:selected { color:#f2b134; border-bottom:2px solid #f2b134; }
        #objSARibbonQuickAccessBar { background:#141a1f; }
    )"));
    return ribbon;
}

void MainWindow::createDocks()
{
    auto docks = m_extensions.registry().docks();
    std::sort(docks.begin(), docks.end(),
              [](const ps::components::DockContribution &a, const ps::components::DockContribution &b) {
                  return a.order < b.order;
              });
    for (const auto &spec : docks)
    {
        QWidget *created = spec.create ? spec.create(this) : nullptr;
        QDockWidget *dock = qobject_cast<QDockWidget *>(created);
        if (!dock && created)
        {
            dock = new QDockWidget(spec.title, this);
            dock->setWidget(created);
        }
        if (!dock)
            continue;
        if (!spec.id.isEmpty())
            dock->setObjectName(spec.id);
        if (!spec.title.isEmpty())
            dock->setWindowTitle(spec.title);
        addDockWidget(spec.area, dock);
        dock->setVisible(spec.visible);
    }

    QDockWidget *logDock = findChild<QDockWidget *>(QStringLiteral("dock.log"));
    if (logDock)
        resizeDocks({logDock}, {96}, Qt::Vertical);
    for (const auto &spec : m_extensions.registry().docks())
        if (spec.layout)
            spec.layout(this);
}

void MainWindow::toggleDockMax(QDockWidget *dock)
{
    if (!dock)
        return;
    if (m_maxDock == dock)
    {
        restoreDockMax();
        return;
    }
    restoreDockMax();
    if (dock->isFloating())
    {
        const Qt::DockWidgetArea area = dockWidgetArea(dock);
        dock->setFloating(false);
        addDockWidget(area, dock);
    }
    m_maxDockCentral = centralWidget();
    if (m_maxDockCentral)
        m_maxDockCentral->hide();
    m_maxDockHidden.clear();
    for (QDockWidget *o : findChildren<QDockWidget *>())
        if (o != dock && o->isVisible())
        {
            m_maxDockHidden.append(o);
            o->hide();
        }
    dock->show();
    dock->raise();
    m_maxDock = dock;
    log(QStringLiteral("最大化面板：%1").arg(dock->windowTitle()));
}

void MainWindow::restoreDockMax()
{
    if (!m_maxDock)
        return;
    for (QDockWidget *o : m_maxDockHidden)
        if (o)
            o->show();
    m_maxDockHidden.clear();
    if (m_maxDockCentral)
        m_maxDockCentral->show();
    m_maxDock = nullptr;
}

static QString dockIconResource(const QDockWidget *d)
{
    const QString icon = d->property("psIcon").toString();
    return icon.isEmpty() ? QStringLiteral(":/icons/panel.svg") : icon;
}

// 出现在窗口最右缘的 ▾ 菜单必须左对齐弹出，否则菜单会跑到屏幕外。
static void execDockMenu(QToolButton *btn, QMenu *menu)
{
    const QPoint below = btn->mapToGlobal(QPoint(btn->width(), btn->height()));
    const QPoint topLeft = btn->mapToGlobal(QPoint(0, 0));
    const int w = menu->sizeHint().width();
    const int h = menu->sizeHint().height();
    QRect avail = btn->screen() ? btn->screen()->availableGeometry()
                                : QRect(0, 0, 1920, 1080);
    QPoint pos(below.x() - w, below.y());
    pos.setX(qMax(avail.left() + 4, qMin(pos.x(), avail.right() - w - 4)));
    if (pos.y() + h > avail.bottom())
        pos.setY(topLeft.y() - h);
    menu->exec(pos);
}

// dock 标题栏在部分平台/主题下会画成浅灰，QSS 与 palette 都压不住：
// 直接换成自绘的深色标题条（26px，和原型 dock-h 一致），顺带把关闭按钮收进来
void darkenDocks(MainWindow *w)
{
    for (auto *d : w->findChildren<QDockWidget *>())
    {
        if (d->titleBarWidget())
            continue; // 数据源 dock 自带一行式标题栏，别盖一层

        auto *bar = new DockTitleBar(w, d);
        bar->setStyleSheet(QStringLiteral(
            "background:#26303a;border-bottom:1px solid #3a4854;"));
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(10, 0, 6, 0);
        lay->setSpacing(4);

        auto *icon = new QLabel(bar);
        icon->setPixmap(QIcon(dockIconResource(d)).pixmap(14, 14));
        icon->setStyleSheet(QStringLiteral("background:transparent;"));
        icon->setFixedSize(16, 16);
        lay->addWidget(icon);

        auto *title = new QLabel(d->windowTitle(), bar);
        title->setStyleSheet(QStringLiteral(
            "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
        lay->addWidget(title);
        lay->addStretch();

        // 所有停靠/浮靠/关闭集中到一个菜单，避免标题栏塞满小按钮
        auto *menuBtn = new QToolButton(bar);
        menuBtn->setText(QStringLiteral("▾"));
        menuBtn->setToolTip(QStringLiteral("最大化 / 最小化 / 浮动 / 关闭"));
        menuBtn->setCursor(Qt::PointingHandCursor);
        menuBtn->setFixedSize(32, 26);
        menuBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#1b232b;border:1px solid #2d3841;border-radius:2px;color:#c9d1d9;"
            "padding:0;font-size:17px;font-weight:700;}"
            "QToolButton:hover{border-color:#f2b134;color:#f2b134;background:#222b33;}"));
        auto *menu = new QMenu(menuBtn);
        QAction *maximize = menu->addAction(QStringLiteral("最大化 / 还原"));
        QAction *left = menu->addAction(QStringLiteral("停靠到左侧"));
        QAction *right = menu->addAction(QStringLiteral("停靠到右侧"));
        QAction *bottom = menu->addAction(QStringLiteral("停靠到底部"));
        menu->addSeparator();
        QAction *autoHide = menu->addAction(QStringLiteral("最小化（自动隐藏）"));
        QAction *floatAct = menu->addAction(QStringLiteral("浮动 / 取消浮动"));
        menu->addSeparator();
        QAction *close = menu->addAction(QStringLiteral("关闭面板"));
        QObject::connect(left, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::LeftDockWidgetArea); });
        QObject::connect(right, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::RightDockWidgetArea); });
        QObject::connect(bottom, &QAction::triggered, w, [w, d] { dockToArea(d, w, Qt::BottomDockWidgetArea); });
        QObject::connect(maximize, &QAction::triggered, w, [w, d] { w->toggleDockMax(d); });
        QObject::connect(autoHide, &QAction::triggered, w, [w, d] {
            w->restoreDockMax();
            collapseDock(d, w);
        });
        QObject::connect(floatAct, &QAction::triggered, w, [w, d] {
            w->restoreDockMax();
            d->setFloating(!d->isFloating());
        });
        QObject::connect(close, &QAction::triggered, w, [w, d] {
            w->restoreDockMax();
            d->close();
        });
        menuBtn->setMenu(menu);
        QObject::connect(menuBtn, &QToolButton::clicked, menuBtn, [menuBtn, menu] {
            execDockMenu(menuBtn, menu);
        });
        lay->addWidget(menuBtn);
        bar->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(bar, &QWidget::customContextMenuRequested, bar, [bar, menu](const QPoint &pos) {
            menu->exec(bar->mapToGlobal(pos));
        });

        d->setTitleBarWidget(bar);
    }
}

























void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QTimer::singleShot(0, this, [this] { repositionDockHandles(this); });
}


void MainWindow::closeEvent(QCloseEvent *event)
{
    m_commands.invoke(QStringLiteral("app.beforeClose"), {});
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
    if (!m_logView)
        return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendPlainText(QStringLiteral("[%1] %2").arg(ts, text));
    statusBar()->showMessage(text, 4000);
}

void MainWindow::attachLogView(QPlainTextEdit *view)
{
    m_logView = view;
    if (m_logView)
    {
        m_logView->setReadOnly(true);
        m_logView->setMaximumBlockCount(2000);
    }
}


void MainWindow::dockWidgetToArea(QDockWidget *dock, Qt::DockWidgetArea area)
{
    restoreDockMax();
    dockToArea(dock, this, area);
}

void MainWindow::collapseDockWidget(QDockWidget *dock)
{
    restoreDockMax();
    collapseDock(dock, this);
}

void MainWindow::showDockMenu(QToolButton *btn, QMenu *menu)
{
    execDockMenu(btn, menu);
}





QWidget *MainWindow::createDockTitleBar(QDockWidget *dock)
{
    return new DockTitleBar(this, dock);
}

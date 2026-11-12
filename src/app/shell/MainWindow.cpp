#include "shell/MainWindow.h"
#include "shell/CsvLoaderDialog.h"

#include <QAction>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFileDialog>
#include <QTimer>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QIcon>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QDateTime>

#include "plot/CanvasWidget.h"
#include "ps_kernel.h"

#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonThemeManager.h"
#include "SARibbonToolButton.h"

namespace {
const char *kStyle = R"(
QWidget { background:#0d1117; color:#c9d1d9; font-size:12px; }
QDockWidget { titlebar-close-icon:none; border:1px solid #1b232a; }
QDockWidget::title { background:#161b20; padding:5px 8px; color:#8b949e; border-bottom:1px solid #232b33; }
QMainWindow::separator { background:#0f1419; width:1px; height:1px; }
QMainWindow::separator:hover { background:#2d3841; }
QToolBar { background:#161b22; border:0; spacing:2px; padding:3px 6px; }
QToolButton { padding:5px 9px; border:1px solid transparent; border-radius:3px; }
QToolButton:hover { background:#21262d; border-color:#30363d; }
QTabBar::tab { background:#161b22; padding:5px 14px; margin-right:1px; }
QTabBar::tab:selected { background:#1f2632; color:#ffffff; border-bottom:2px solid #4ea1ff; }
QTabWidget::pane { border:0; }
QTabBar { border:0; background:#0d1117; }
QSplitter::handle { background:#1b232a; }
QSplitter::handle:horizontal { width:1px; }
QSplitter::handle:vertical { height:1px; }
QSplitter::handle:hover { background:#2d3841; }
QStatusBar { background:#161b20; color:#6e7681; border-top:1px solid #232b33; }
QStatusBar::item { border:0; }
QTreeWidget, QTableWidget, QPlainTextEdit { background:#0d1117; border:1px solid #21262d; }
QHeaderView::section { background:#161b22; border:0; padding:4px; color:#8b949e; }
)";
}

static void darkenDocks(MainWindow *w);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PlotScope 0.1.0 — 绘图分析"));
    resize(1440, 880);
    setStyleSheet(QString::fromLatin1(kStyle));

    setMenuWidget(createRibbon());
    createDocks();
    darkenDocks(this);
    createPages();

    // 内核连通性：状态栏直接读 ABI，证明 C ABI 已经打通
    const QString kernelInfo =
        QStringLiteral("内核 ps-abi %1 · ABI v%2 · selftest %3")
            .arg(QString::fromUtf8(ps_kernel_version()))
            .arg(ps_abi_version())
            .arg(ps_abi_selftest() == 0 ? QStringLiteral("通过") : QStringLiteral("失败"));
    statusBar()->showMessage(kernelInfo);

    log(QStringLiteral("启动完成：%1").arg(kernelInfo));
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
    ribbon->setTabBarHeight(26);
    ribbon->setPanelTitleHeight(10);
    // vendored 布局里标题几何会溢出 category 底边被裁掉半行，索性不显示（原型的分组标签改用 tooltip）
    ribbon->setEnableShowPanelTitle(false);
    ribbon->setPanelToolButtonIconSize(QSize(16, 16), QSize(23, 23));
    ribbon->setPanelSpacing(3);
    ribbon->setCategoryHeight(74, true); // 原型体部 74 px

    auto addLarge = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx);
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addLargeAction(a);
    };
    // 小按钮：只有 1~2 个字才显示文字，其余只留图标 + tooltip（图标 16px 撑得住）
    auto addSmall = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(ctx);
        a->setToolTip(text);
        if (text.size() <= 2)
            a->setText(text);
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addSmallAction(a);
    };

    // 文件
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("文件"));
        auto *pLayout = cat->addPanel(QStringLiteral("布局"));
        addLarge(pLayout, QStringLiteral("打开文件"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("打开文件"), {},
                QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
            if (!f.isEmpty())
                loadFile(f);
        }, this);
        addSmall(pLayout, QStringLiteral("导入布局"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
                QStringLiteral("PlotScope 布局 (*.psl)"));
            if (!f.isEmpty())
                log(QStringLiteral("导入布局：%1").arg(f));
        }, this);
        addSmall(pLayout, QStringLiteral("设置"), [this] {
            QMessageBox::information(this, QStringLiteral("设置"),
                QStringLiteral("M0 骨架：设置页在 M1 接 QSettings。"));
        }, this);

        auto *pExport = cat->addPanel(QStringLiteral("导出"));
        addLarge(pExport, QStringLiteral("导出布局"), [this] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出布局"), {},
                QStringLiteral("PlotScope 布局 (*.psl)"));
            if (!f.isEmpty())
                log(QStringLiteral("导出布局：%1").arg(f));
        }, this);
        addSmall(pExport, QStringLiteral("导出设置"), [this] { log(QStringLiteral("导出设置：M1 接 YAML")); }, this);
        addSmall(pExport, QStringLiteral("导出数据"), [this] { log(QStringLiteral("导出数据：M1 接 CSV/NDF")); }, this);

        auto *pShot = cat->addPanel(QStringLiteral("截图"));
        addLarge(pShot, QStringLiteral("截图当前页"), [this] {
            QWidget *page = m_pages ? m_pages->currentWidget() : nullptr;
            if (!page)
                return;
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("截图另存为"), {},
                QStringLiteral("PNG (*.png)"));
            if (f.isEmpty())
                return;
            if (page->grab().save(f))
                log(QStringLiteral("已截图：%1").arg(f));
        }, this);
    }

    // 数据
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("数据"));
        auto *pSrc = cat->addPanel(QStringLiteral("来源"));
        addLarge(pSrc, QStringLiteral("打开数据源"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("打开数据源"), {},
                QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
            if (!f.isEmpty())
                loadFile(f);
        }, this);
        auto *pRun = cat->addPanel(QStringLiteral("传输"));
        addLarge(pRun, QStringLiteral("全部开始"), [this] {
            m_runLabel->setText(QStringLiteral("运行中"));
            m_runLabel->setStyleSheet(QStringLiteral("color:#7ee787;"));
            log(QStringLiteral("所有数据源开始"));
        }, this);
        addSmall(pRun, QStringLiteral("全部暂停"), [this] {
            m_runLabel->setText(QStringLiteral("已暂停"));
            m_runLabel->setStyleSheet(QStringLiteral("color:#ffb454;"));
            log(QStringLiteral("所有数据源暂停"));
        }, this);
    }

    // 绘图
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("绘图"));
        auto *pPage = cat->addPanel(QStringLiteral("页"));
        addLarge(pPage, QStringLiteral("新建页"), [this] {
            auto *c = new CanvasWidget(this);
            c->setTitle(QStringLiteral("新页 · 画布"));
            connect(c, &CanvasWidget::activated, this, [this](const QString &t) {
                setPropRows(t, {{QStringLiteral("类型"), QStringLiteral("画布")},
                                {QStringLiteral("时间窗"), QStringLiteral("30.0 s")},
                                {QStringLiteral("降采样"), QStringLiteral("min/max 1:4")}});
            });
            m_pages->addTab(c, QStringLiteral("新页"));
            m_pages->setCurrentWidget(c);
            log(QStringLiteral("新建页"));
        }, this);
        auto *pView = cat->addPanel(QStringLiteral("视图"));
        addSmall(pView, QStringLiteral("自动 Y"), [this] { log(QStringLiteral("自动 Y：按可见曲线量程重算")); }, this);
        addSmall(pView, QStringLiteral("游标"), [this] { log(QStringLiteral("十字光标已启用")); }, this);
        addSmall(pView, QStringLiteral("复位视图"), [this] { log(QStringLiteral("双击画布也可复位")); }, this);
    }

    // 仪表
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("仪表"));
        auto *p = cat->addPanel(QStringLiteral("新建"));
        addLarge(p, QStringLiteral("地平仪"), [this] { log(QStringLiteral("添加地平仪（QPainter 自绘，M1 落地）")); }, this);
        addSmall(p, QStringLiteral("罗盘"), [this] { log(QStringLiteral("添加罗盘（QPainter 自绘，M1 落地）")); }, this);
        addSmall(p, QStringLiteral("表盘"), [this] { log(QStringLiteral("添加表盘（QPainter 自绘，M1 落地）")); }, this);
    }

    // 脚本
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("脚本"));
        auto *p = cat->addPanel(QStringLiteral("Lua"));
        addLarge(p, QStringLiteral("运行"), [this] {
            log(QStringLiteral("Lua 5.4 逐块执行（10 ms/块）：out = ps.movingAvg(ps.derivative(ps.ch(\"att.roll\")), 5)"));
        }, this);
        addSmall(p, QStringLiteral("格式化"), [this] { log(QStringLiteral("脚本格式化（stylua 规则，M2 接）")); }, this);
    }

    // 协议
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("协议"));
        auto *p = cat->addPanel(QStringLiteral("编辑器"));
        addLarge(p, QStringLiteral("新建 Loader"), [this] {
            log(QStringLiteral("新建 Loader：打开附属编辑器（QDialog，D13）"));
        }, this);
        addSmall(p, QStringLiteral("帧模板 YAML"), [this] { log(QStringLiteral("帧模板：YAML 热编译（D6）")); }, this);
        addSmall(p, QStringLiteral("校验"), [this] { log(QStringLiteral("校验模板：字段偏移/位域越界检查（M1）")); }, this);
    }

    // 视图
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("视图"));
        auto *p = cat->addPanel(QStringLiteral("面板"));
        auto toggle = [this](const char *obj) {
            if (auto *d = findChild<QDockWidget *>(QString::fromLatin1(obj)))
                d->setVisible(!d->isVisible());
        };
        addLarge(p, QStringLiteral("数据源"), [toggle] { toggle("dock.src"); }, this);
        addSmall(p, QStringLiteral("属性"), [toggle] { toggle("dock.prop"); }, this);
        addSmall(p, QStringLiteral("脚本"), [toggle] { toggle("dock.script"); }, this);
        addSmall(p, QStringLiteral("日志"), [toggle] { toggle("dock.log"); }, this);
    }

    // 右侧芯片位（原型里的 qchip）：状态摘要不做成按钮也得看得见，先用只读 action 占位
    auto *rg = ribbon->rightButtonGroup();
    for (const QString &t : {QStringLiteral("CSV · 3000×5"), QStringLiteral("LOD: Rust")})
    {
        auto *a = new QAction(t, this);
        a->setEnabled(false);
        rg->addAction(a);
    }

    // 自绘 SVG 图标：没有图标时 SARibbon 会在按钮左侧留空方块，观感直接崩
    {
        static const QHash<QString, QString> kIcon = {
            {QStringLiteral("打开文件"), QStringLiteral("open")},
            {QStringLiteral("导入布局"), QStringLiteral("import")},
            {QStringLiteral("设置"), QStringLiteral("settings")},
            {QStringLiteral("导出布局"), QStringLiteral("export")},
            {QStringLiteral("导出设置"), QStringLiteral("export_cfg")},
            {QStringLiteral("导出数据"), QStringLiteral("export_data")},
            {QStringLiteral("截图当前页"), QStringLiteral("screenshot")},
            {QStringLiteral("打开数据源"), QStringLiteral("datasource")},
            {QStringLiteral("全部开始"), QStringLiteral("play")},
            {QStringLiteral("全部暂停"), QStringLiteral("pause")},
            {QStringLiteral("新建页"), QStringLiteral("newpage")},
            {QStringLiteral("自动 Y"), QStringLiteral("autoy")},
            {QStringLiteral("游标"), QStringLiteral("cursor")},
            {QStringLiteral("复位视图"), QStringLiteral("reset")},
            {QStringLiteral("地平仪"), QStringLiteral("horizon")},
            {QStringLiteral("罗盘"), QStringLiteral("compass")},
            {QStringLiteral("表盘"), QStringLiteral("gauge")},
            {QStringLiteral("运行"), QStringLiteral("run")},
            {QStringLiteral("格式化"), QStringLiteral("format")},
            {QStringLiteral("新建 Loader"), QStringLiteral("loader")},
            {QStringLiteral("帧模板 YAML"), QStringLiteral("yaml")},
            {QStringLiteral("校验"), QStringLiteral("validate")},
            {QStringLiteral("数据源"), QStringLiteral("datasource")},
            {QStringLiteral("属性"), QStringLiteral("prop")},
            {QStringLiteral("脚本"), QStringLiteral("script")},
            {QStringLiteral("日志"), QStringLiteral("log")},
        };
        // QAction 的 parent 是 this（不是 ribbon），按 ribbon 找会一个都拿不到
        for (auto *a : findChildren<QAction *>())
        {
            const auto it = kIcon.find(a->text());
            if (it != kIcon.end())
            {
                a->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(it.value())));

            }
        }
    }

    if (auto *ab = ribbon->applicationButton())
        ab->hide(); // 左上角默认有个空按钮，留白很难看

    // 面板本体撑到 68 px：SARibbon 的 category 高度不会自动把 panel 拉满
    for (auto *p : ribbon->findChildren<SARibbonPanel *>())
    {
        p->setMinimumHeight(68);
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
        SARibbonPanelLabel { color:#5d6c77; font-size:9.5px; background:transparent; padding-bottom:3px; }
        SARibbonToolButton {
            background:transparent; border:1px solid transparent; border-radius:0;
            color:#8494a0; font-size:10.5px; padding:0; margin:0;
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
            background:transparent; color:#5d6c77; font-size:11.5px;
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
    // 左：数据源（一个数据源一棵子树，行内“配置”打开 Loader 附属编辑器）
    auto *srcDock = new QDockWidget(QStringLiteral("数据源 · Loader"), this);
    srcDock->setObjectName(QStringLiteral("dock.src"));
    auto *srcHost = new QWidget(srcDock);
    auto *srcLay = new QVBoxLayout(srcHost);
    srcLay->setContentsMargins(0, 0, 0, 0);
    srcLay->setSpacing(0);

    auto *srcBar = new QToolBar(srcHost);
    auto *runAct = srcBar->addAction(QStringLiteral("▶ 开始"));
    auto *pauseAct = srcBar->addAction(QStringLiteral("⏸ 暂停"));
    srcLay->addWidget(srcBar);

    m_runLabel = new QLabel(QStringLiteral("ulog 已加载 · can 已暂停 · mav 空闲"), srcHost);
    m_runLabel->setContentsMargins(8, 3, 8, 3);
    m_runLabel->setStyleSheet(QStringLiteral("color:#8b949e;"));
    srcLay->addWidget(m_runLabel);

    m_srcTree = new QTreeWidget(srcHost);
    m_srcTree->setHeaderHidden(true);
    m_srcTree->setRootIsDecorated(true);
    const struct { const char *name; const char *state; QStringList ch; } srcs[] = {
        {"ulog · flight_2026-11-12.ulg", "loaded", {"att.roll", "att.pitch", "engine_rpm"}},
        {"can · vehicle_bus", "paused", {"CAN.vehicle_speed", "CAN.engine_rpm"}},
        {"mav · 192.168.1.20:14550", "idle", {"attitude.yaw", "battery.voltage"}},
    };
    for (const auto &s : srcs)
    {
        auto *top = new QTreeWidgetItem(m_srcTree, {QStringLiteral("%1  [%2]").arg(QString::fromUtf8(s.name), QString::fromUtf8(s.state))});
        for (const QString &c : s.ch)
            new QTreeWidgetItem(top, {c});
        top->setExpanded(true);
    }
    srcLay->addWidget(m_srcTree, 1);

    // 双击通道 → 画到当前页第一格（后续换成字段拖拽，见 §8.10）
    connect(m_srcTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        const qulonglong raw = item->data(0, Qt::UserRole).toULongLong();
        const PsChannelInfo *ch = reinterpret_cast<const PsChannelInfo *>(raw);
        if (!ch)
            return;
        if (auto *c = currentCanvas())
        {
            c->setChannels({ch});
            c->setTitle(QString::fromUtf8(ch->name));
            log(QStringLiteral("画布加载通道：%1").arg(QString::fromUtf8(ch->name)));
        }
    });
    srcDock->setWidget(srcHost);
    addDockWidget(Qt::LeftDockWidgetArea, srcDock);

    connect(runAct, &QAction::triggered, this, [this] {
        m_runLabel->setText(QStringLiteral("运行中 · 背压策略：丢旧留新"));
        m_runLabel->setStyleSheet(QStringLiteral("color:#7ee787;"));
        log(QStringLiteral("数据源开始"));
    });
    connect(pauseAct, &QAction::triggered, this, [this] {
        m_runLabel->setText(QStringLiteral("已暂停 · 视图冻结，采集继续"));
        m_runLabel->setStyleSheet(QStringLiteral("color:#ffb454;"));
        log(QStringLiteral("数据源暂停"));
    });

    // 右：属性面板（命中什么对象就显示什么属性）
    auto *propDock = new QDockWidget(QStringLiteral("属性面板"), this);
    propDock->setObjectName(QStringLiteral("dock.prop"));
    auto *propHost = new QWidget(propDock);
    auto *propLay = new QVBoxLayout(propHost);
    propLay->setContentsMargins(0, 0, 0, 0);
    m_selLabel = new QLabel(QStringLiteral("当前对象：画布"), propHost);
    m_selLabel->setContentsMargins(8, 6, 8, 6);
    m_selLabel->setStyleSheet(QStringLiteral("color:#4ea1ff;"));
    m_propTable = new QTableWidget(0, 2, propHost);
    m_propTable->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_propTable->horizontalHeader()->setStretchLastSection(true);
    m_propTable->verticalHeader()->setVisible(false);
    m_propTable->setShowGrid(false);
    propLay->addWidget(m_selLabel);
    propLay->addWidget(m_propTable, 1);
    propDock->setWidget(propHost);
    addDockWidget(Qt::RightDockWidgetArea, propDock);
    setPropRows(QStringLiteral("画布"),
                {{QStringLiteral("时间窗"), QStringLiteral("30.0 s")},
                 {QStringLiteral("Y 轴"), QStringLiteral("线性 · -1.60 ~ 1.60")},
                 {QStringLiteral("曲线数"), QStringLiteral("3")}});

    // 右下：脚本
    auto *scriptDock = new QDockWidget(QStringLiteral("脚本 · Lua 5.4"), this);
    scriptDock->setObjectName(QStringLiteral("dock.script"));
    auto *scriptHost = new QWidget(scriptDock);
    auto *scriptLay = new QVBoxLayout(scriptHost);
    scriptLay->setContentsMargins(0, 0, 0, 0);
    m_script = new QPlainTextEdit(scriptHost);
    m_script->setPlainText(QStringLiteral(
        "-- 一页一组算子：整块进，向量算，禁止逐点循环\n"
        "out = ps.movingAvg(ps.derivative(ps.ch(\"att.roll\")), 5)\n"
        "ps.plot(\"yaw_rate\", out)\n"));
    auto *runBtn = new QPushButton(QStringLiteral("运行"), scriptHost);
    scriptLay->addWidget(m_script, 1);
    scriptLay->addWidget(runBtn);
    scriptDock->setWidget(scriptHost);
    addDockWidget(Qt::RightDockWidgetArea, scriptDock);
    connect(runBtn, &QPushButton::clicked, this, [this] {
        log(QStringLiteral("Lua 5.4 逐块执行（10 ms/块）→ yaw_rate 已物化"));
    });

    // 底部：日志
    auto *logDock = new QDockWidget(QStringLiteral("日志"), this);
    logDock->setObjectName(QStringLiteral("dock.log"));
    m_logView = new QPlainTextEdit(logDock);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    logDock->setWidget(m_logView);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    resizeDocks({logDock}, {150}, Qt::Vertical);
}

// dock 标题栏在部分平台/主题下会画成浅灰，QSS 与 palette 都压不住：
// 直接换成自绘的深色标题条（26px，和原型 dock-h 一致），顺带把关闭按钮收进来
void darkenDocks(MainWindow *w)
{
    for (auto *d : w->findChildren<QDockWidget *>())
    {
        auto *bar = new QWidget(d);
        bar->setFixedHeight(26);
        bar->setStyleSheet(QStringLiteral(
            "background:#161b20;border-bottom:1px solid #232b33;"));
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(8, 0, 6, 0);
        lay->setSpacing(4);

        auto *title = new QLabel(d->windowTitle(), bar);
        title->setStyleSheet(QStringLiteral(
            "color:#8494a0;font-size:11px;background:transparent;"));
        lay->addWidget(title);
        lay->addStretch();

        auto *close = new QToolButton(bar);
        close->setText(QStringLiteral("×"));
        close->setCursor(Qt::PointingHandCursor);
        close->setFixedSize(18, 18);
        close->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;color:#5d6c77;font-size:13px;}"
            "QToolButton:hover{color:#f2b134;}"));
        QObject::connect(close, &QToolButton::clicked, d, &QDockWidget::close);
        lay->addWidget(close);

        d->setTitleBarWidget(bar);
    }
}

void MainWindow::createPages()
{
    m_pages = new QTabWidget(this);
    m_pages->setDocumentMode(true);
    m_pages->setTabsClosable(true);
    m_pages->tabBar()->setExpanding(false);

    auto makeCanvas = [this](const QString &title) {
        auto *c = new CanvasWidget(this);
        c->setTitle(title);
        connect(c, &CanvasWidget::activated, this, [this, c](const QString &t) {
            setPropRows(t, {{QStringLiteral("类型"), QStringLiteral("画布")},
                            {QStringLiteral("时间窗"), QStringLiteral("%1 ~ %2 s")
                                                      .arg(c->t0(), 0, 'f', 2)
                                                      .arg(c->t1(), 0, 'f', 2)},
                            {QStringLiteral("数据来源"), c->kernelBacked() ? QStringLiteral("CSV 数据集（内核内存）")
                                                                          : QStringLiteral("演示合成数据")},
                            {QStringLiteral("Y 轴"), QStringLiteral("线性 · 滚轮缩 / 拖动平移 / 双击复位")}});
        });
        return c;
    };

    // 页 1：三 plot 竖排（QSplitter 树，D15）
    auto *page1 = new QSplitter(Qt::Vertical, this);
    page1->addWidget(makeCanvas(QStringLiteral("姿态角 · plot")));
    page1->addWidget(makeCanvas(QStringLiteral("振动 · plot")));
    page1->addWidget(makeCanvas(QStringLiteral("总线 · plot")));
    page1->setSizes({300, 220, 220});
    m_pages->addTab(page1, QStringLiteral("姿态与振动"));

    // 页 2：单 plot
    m_pages->addTab(makeCanvas(QStringLiteral("车辆总线 · plot")), QStringLiteral("车辆总线"));

    m_pages->addTab(new QLabel(QStringLiteral("页 4 · 仪表大屏：地平仪 / 罗盘 / 表盘（M1）"), this),
                    QStringLiteral("仪表大屏"));

    connect(m_pages, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        if (m_pages->count() > 1)
            m_pages->removeTab(idx);
    });

    setCentralWidget(m_pages);
}

QVector<CanvasWidget *> MainWindow::pageCanvases() const
{
    QVector<CanvasWidget *> out;
    if (!m_pages)
        return out;
    QWidget *page = m_pages->currentWidget();
    if (auto *c = qobject_cast<CanvasWidget *>(page))
    {
        out.append(c);
        return out;
    }
    if (auto *sp = qobject_cast<QSplitter *>(page))
    {
        for (int i = 0; i < sp->count(); ++i)
        {
            if (auto *c = qobject_cast<CanvasWidget *>(sp->widget(i)))
                out.append(c);
        }
    }
    return out;
}

CanvasWidget *MainWindow::currentCanvas() const
{
    return pageCanvases().value(0, nullptr);
}

bool MainWindow::loadFile(const QString &path)
{
    char err[256] = {0};
    PsDataset *ds = nullptr;
    const QFileInfo fi(path);
    // 分流：ulg → ULog，asc → Vector ASC，blf → Vector BLF，其余按 CSV
    const QString suffix = fi.suffix().toLower();
    const bool is_ulog = suffix == QLatin1String("ulg");
    const bool is_asc = suffix == QLatin1String("asc");
    const bool is_blf = suffix == QLatin1String("blf");
    const bool is_tree = is_ulog || is_asc || is_blf; // 有 topic/ID 层级，要分组挂树
    int32_t rc;
    if (is_ulog)
        rc = ps_ulog_load(path.toUtf8().constData(), &ds, err, sizeof(err));
    else if (is_asc)
        rc = ps_asc_load(path.toUtf8().constData(), &ds, err, sizeof(err));
    else if (is_blf)
        rc = ps_blf_load(path.toUtf8().constData(), &ds, err, sizeof(err));
    else
    {
        // CSV：先探测，由用户确认时间轴/分隔符/表头，再按选项解析
        CsvLoaderDialog dlg(path, this);
        if (dlg.exec() != QDialog::Accepted)
            return false;
        rc = ps_csv_load_opts(path.toUtf8().constData(), dlg.optionsJson().toUtf8().constData(),
                              &ds, err, sizeof(err));
    }
    if (rc != 0)
    {
        const QString msg = QString::fromUtf8(err);
        log(QStringLiteral("加载失败：%1").arg(msg));
        QMessageBox::warning(this, QStringLiteral("加载失败"), msg);
        return false;
    }

    // 先清空画布，再释放旧数据集——画布持有通道指针，顺序反了就是悬垂指针
    for (auto *c : findChildren<CanvasWidget *>())
    {
        if (c->kernelBacked())
            c->clearChannels();
    }
    if (m_dataset)
        ps_dataset_free(m_dataset);
    m_dataset = ds;

    const uint32_t n = ps_dataset_channel_count(ds);
    const QString fmt_tag = is_ulog ? QStringLiteral("ULog")
                          : is_asc  ? QStringLiteral("ASC · CAN")
                          : is_blf  ? QStringLiteral("BLF · CAN")
                                    : QStringLiteral("CSV");
    auto *top = new QTreeWidgetItem(m_srcTree,
        {QStringLiteral("%1  [%2 · %3 通道]").arg(fi.fileName(), fmt_tag).arg(n)});
    QVector<const PsChannelInfo *> channels;

    // ULog 通道名是 `topic.field`（多实例 `topic.NN.field`）：按 topic 分组，
    // 否则 1351 条通道平铺在一层，找不动。CSV 没有层级，保持一层。
    QMap<QString, QTreeWidgetItem *> groups;
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        if (!ch)
            continue;
        channels.append(ch);
        const QString name = QString::fromUtf8(ch->name);
        QString group;
        QString leaf = name;
        if (is_tree)
        {
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
            {
                group = name.left(dot);
                leaf = name.mid(dot + 1);
            }
        }
        QTreeWidgetItem *parent = top;
        if (!group.isEmpty())
        {
            auto it = groups.find(group);
            if (it == groups.end())
            {
                auto *g = new QTreeWidgetItem(top, {group});
                it = groups.insert(group, g);
            }
            parent = it.value();
        }
        auto *item = new QTreeWidgetItem(parent, {leaf});
        item->setToolTip(0, name);
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
        item->setData(0, Qt::UserRole + 1, name);
    }
    const bool few_groups = groups.size() <= 8;
    top->setExpanded(is_tree ? few_groups : true);
    for (auto *g : groups)
        g->setExpanded(few_groups && is_tree);
    if (is_tree && !few_groups)
        groups.first()->setExpanded(true);

    // 一格里一个通道：混量纲（deg / rpm / V）塞一条 Y 轴会把小信号压成直线
    const QVector<CanvasWidget *> canvases = pageCanvases();
    for (int i = 0; i < canvases.size() && i < channels.size(); ++i)
    {
        canvases[i]->setChannels({channels[i]});
        canvases[i]->setTitle(QStringLiteral("%1 · %2").arg(fi.completeBaseName(), QString::fromUtf8(channels[i]->name)));
    }

    if (const PsChannelInfo *first = ps_dataset_channel(ds, 0))
    {
        const double t0 = first->time[0];
        const double t1 = first->time[first->point_count - 1];
        log(QStringLiteral("已加载 %1（%2）：%3 通道 × %4 点，时间窗 %5 s")
                .arg(fi.fileName(), fmt_tag).arg(n).arg(first->point_count)
                .arg(t1 - t0, 0, 'f', 2));
    }
    statusBar()->showMessage(QStringLiteral("%1 · %2 通道 · %3 点").arg(fi.fileName()).arg(n)
                             .arg(ps_dataset_channel(ds, 0) ? ps_dataset_channel(ds, 0)->point_count : 0));
    return true;
}

void MainWindow::setPropRows(const QString &object, const QVector<QPair<QString, QString>> &rows)
{
    m_selLabel->setText(QStringLiteral("当前对象：%1").arg(object));
    m_propTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i)
    {
        auto *k = new QTableWidgetItem(rows[i].first);
        k->setFlags(Qt::ItemIsEnabled);
        auto *v = new QTableWidgetItem(rows[i].second);
        m_propTable->setItem(i, 0, k);
        m_propTable->setItem(i, 1, v);
    }
}

void MainWindow::log(const QString &text)
{
    if (!m_logView)
        return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logView->appendPlainText(QStringLiteral("[%1] %2").arg(ts, text));
    statusBar()->showMessage(text, 4000);
}

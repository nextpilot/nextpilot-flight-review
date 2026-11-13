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
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QIcon>
#include <QResizeEvent>
#include <QStyle>
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
#include <QLineEdit>
#include <QEvent>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDropEvent>
#include <QListWidget>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "plot/CanvasWidget.h"
#include "ps_kernel.h"

#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonThemeManager.h"
#include "SARibbonToolButton.h"

static void darkenDocks(MainWindow *w);

// 只重写 sizeHint 的标题栏：QDockWidget 按 sizeHint 预留高度，固定 height 不算数，
// 32px 的 bar 只留 24px 空间就会压住正文第一行。
class DockTitleBar : public QWidget
{
public:
    explicit DockTitleBar(QWidget *parent = nullptr) : QWidget(parent) { setFixedHeight(32); }
    QSize sizeHint() const override { return QSize(QWidget::sizeHint().width(), 32); }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

// 字段树拖拽：Item 里存的是内核通道指针，默认 QTreeWidget 的 MIME 带不过去，自己塞一条
class ChannelTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    void startDrag(Qt::DropActions) override
    {
        QTreeWidgetItem *item = currentItem();
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        auto *mime = new QMimeData;
        mime->setData(QStringLiteral("application/x-plotscope-channel"), QByteArray::number(ptr));
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

// 源行名称按可用宽度省略中部，避免把右侧"配置"挤出去
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PlotScope 0.1.0 — 绘图分析"));
    resize(1440, 880);


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
    m_kernelInfo = kernelInfo;
    statusBar()->showMessage(kernelInfo);

    // PJ 习惯：Ctrl+S 存当前布局，Ctrl+Shift+O 打开布局
    auto *saveSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this);
    connect(saveSc, &QShortcut::activated, this, [this] { saveLayoutTo(defaultLayoutPath()); });
    auto *openSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this);
    connect(openSc, &QShortcut::activated, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
            QStringLiteral("PlotScope 布局 (*.psl)"));
        if (!f.isEmpty())
            importLayoutFrom(f);
    });

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
    // 标题带直接压成 0：留 30px 空行只会把页签往下推，组标题也被挤掉
    ribbon->setTitleBarHeight(0, true);
    ribbon->setTabBarHeight(26);
    // 收缩：右下角 chevron + 双击页签，都能把面板收起只留页签行
    ribbon->showMinimumModeButton(true);
    ribbon->setTabDoubleClickToMinimumMode(true);
    ribbon->setPanelTitleHeight(12);
    // 分组标题：vendored 布局把标题画在面板底边内，category 高度必须把它算进去
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setPanelToolButtonIconSize(QSize(18, 18), QSize(26, 26));
    ribbon->setPanelSpacing(3);
    ribbon->setCategoryHeight(70, true); // 图标 23 + 内边距 + 分组标题 12

    // 大按钮同样只留图标：分组标题已说明用途，图标下再叠一行字就是原型里最挤的部分
    auto addLarge = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx); // 先用全名建 action：图标按 text 查表
        a->setToolTip(text);
        if (text.size() > 2)
            a->setProperty("rbIconOnly", true); // 图标查完后清空文字，只留 tooltip
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addLargeAction(a);
    };
    // 小按钮：只有 1~2 个字才显示文字，其余只留图标 + tooltip（图标 16px 撑得住）
    auto addSmall = [](SARibbonPanel *panel, const QString &text, std::function<void()> fn, QObject *ctx) {
        auto *a = new QAction(text, ctx);
        a->setToolTip(text);
        QObject::connect(a, &QAction::triggered, ctx, fn);
        panel->addSmallAction(a);
    };

    // 文件
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("文件"));
        auto *pLayout = cat->addPanel(QStringLiteral("文件"));
        addLarge(pLayout, QStringLiteral("打开文件"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("打开文件"), {},
                QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
            if (!f.isEmpty())
                loadFile(f);
        }, this);
        addLarge(pLayout, QStringLiteral("导入布局"), [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
                QStringLiteral("PlotScope 布局 (*.psl)"));
            if (!f.isEmpty())
                importLayoutFrom(f);
        }, this);
        addLarge(pLayout, QStringLiteral("设置"), [this] { openSettings(); }, this);

        auto *pExport = cat->addPanel(QStringLiteral("导出"));
        addLarge(pExport, QStringLiteral("保存布局"), [this] {
            const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出布局"), {},
                QStringLiteral("PlotScope 布局 (*.psl)"));
            if (!f.isEmpty())
                saveLayoutTo(f);
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
            m_srcCount->setText(QStringLiteral("3 源 · 运行中"));
            m_srcCount->setStyleSheet(QStringLiteral("color:#7ee787;font-size:10px;background:transparent;"));
            log(QStringLiteral("所有数据源开始"));
        }, this);
        addSmall(pRun, QStringLiteral("全部暂停"), [this] {
            m_srcCount->setText(QStringLiteral("3 源 · 已暂停"));
            m_srcCount->setStyleSheet(QStringLiteral("color:#ffb454;font-size:10px;background:transparent;"));
            log(QStringLiteral("所有数据源暂停"));
        }, this);
    }

    // 绘图
    {
        auto *cat = ribbon->addCategoryPage(QStringLiteral("绘图"));
        auto *pPage = cat->addPanel(QStringLiteral("页"));
        addLarge(pPage, QStringLiteral("新建页"), [this] { newPage(); }, this);
        addLarge(pPage, QStringLiteral("添加画布"), [this] { addCanvasToPage(); }, this);
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
            {QStringLiteral("添加画布"), QStringLiteral("plus")},
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
    // 左：数据源（对齐原型 d-src）：源列表 → 字段/参数/消息 三页签 → 内容
    auto *srcDock = new QDockWidget(QStringLiteral("数据源"), this);
    srcDock->setObjectName(QStringLiteral("dock.src"));

    // 标题栏：标题 + 汇总 + 全局起停 + 关闭，全部放一行，正文从源列表直接开始（不再叠第二行头部）
    auto *srcBar = new DockTitleBar(srcDock);
    srcBar->setStyleSheet(QStringLiteral(
        "background:#26303a;border-bottom:1px solid #3a4854;"));
    auto *barLay = new QHBoxLayout(srcBar);
    barLay->setContentsMargins(10, 0, 6, 0);
    barLay->setSpacing(6);
    auto *barTitle = new QLabel(QStringLiteral("数据源"), srcBar);
    barTitle->setStyleSheet(QStringLiteral(
        "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
    barLay->addWidget(barTitle);
    m_srcCount = new QLabel(QStringLiteral("3 源 · 2 已连接"), srcBar);
    m_srcCount->setStyleSheet(QStringLiteral("color:#8494a0;font-size:10px;background:transparent;"));
    barLay->addWidget(m_srcCount);
    barLay->addStretch();
    auto mkTiny = [&](const QString &icon, const QString &tip) {
        auto *b = new QToolButton(srcBar);
        b->setIcon(QIcon(QStringLiteral(":/icons/%1.svg").arg(icon)));
        b->setIconSize(QSize(14, 14));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(22, 22);
        b->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;color:#8494a0;padding:0;}"
            "QToolButton:hover{background:#222b33;}"));
        barLay->addWidget(b);
        return b;
    };
    auto *addSrcBtn = mkTiny(QStringLiteral("newpage"), QStringLiteral("添加数据源"));
    QObject::connect(addSrcBtn, &QToolButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("添加数据源"), {},
            QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
        if (!f.isEmpty())
            loadFile(f);
    });
    auto *runBtn = mkTiny(QStringLiteral("play"), QStringLiteral("全部开始"));
    auto *pauseBtn = mkTiny(QStringLiteral("pause"), QStringLiteral("全部暂停"));
    auto *barClose = new QToolButton(srcBar);
    barClose->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    barClose->setIconSize(QSize(13, 13));
    barClose->setToolTip(QStringLiteral("关闭面板"));
    barClose->setCursor(Qt::PointingHandCursor);
    barClose->setFixedSize(22, 22);
    barClose->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;color:#5d6c77;padding:0;}"
        "QToolButton:hover{color:#f85149;background:#222b33;}"));
    QObject::connect(barClose, &QToolButton::clicked, srcDock, &QDockWidget::close);
    barLay->addWidget(barClose);
    srcDock->setTitleBarWidget(srcBar);

    auto *srcHost = new QWidget(srcDock);
    auto *srcLay = new QVBoxLayout(srcHost);
    srcLay->setContentsMargins(0, 2, 0, 0);
    srcLay->setSpacing(0);

    // 源列表：每条=状态点 + 名称 + 速率/条目 + 配置
    auto *rowsHost = new QWidget(srcHost);
    m_srcRows = new QVBoxLayout(rowsHost);
    m_srcRows->setContentsMargins(0, 0, 0, 0);
    m_srcRows->setSpacing(0);
    addSourceRow(QStringLiteral("ulog · 示例会话"), QStringLiteral("示例 · 未接入"), QStringLiteral("#4a545d"));
    addSourceRow(QStringLiteral("can · vehicle_bus"), QStringLiteral("示例 · 未接入"), QStringLiteral("#4a545d"));
    addSourceRow(QStringLiteral("mav · 192.168.1.20:14550"), QStringLiteral("示例 · 未接入"), QStringLiteral("#4a545d"));
    srcLay->addWidget(rowsHost);

    // 搜索：字段树/参数表/消息表共用，按当前页签生效
    m_srcFilter = new QLineEdit(srcHost);
    m_srcFilter->setPlaceholderText(QStringLiteral("搜索字段 / 参数 / 消息"));
    m_srcFilter->setClearButtonEnabled(true);
    m_srcFilter->setFixedHeight(28);
    m_srcFilter->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#0f151a;border:0;border-top:1px solid #232b33;border-bottom:1px solid #232b33;"
        "color:#c9d1d9;font-size:11px;padding:0 8px;}"
        "QLineEdit:focus{border-bottom:1px solid #f2b134;}"));
    srcLay->addWidget(m_srcFilter);
    connect(m_srcFilter, &QLineEdit::textChanged, this, [this] { applySrcFilter(); });

    // 三页签：字段 / 参数 / 消息
    m_srcTabs = new QTabBar(srcHost);
    m_srcTabs->setExpanding(false);
    m_srcTabs->setDrawBase(false);
    m_srcTabs->addTab(QStringLiteral("字段 0"));
    m_srcTabs->addTab(QStringLiteral("参数 0"));
    m_srcTabs->addTab(QStringLiteral("消息 0"));
    m_srcTabs->setStyleSheet(QStringLiteral(
        "QTabBar{background:#161b20;border-bottom:1px solid #232b33;}"
        "QTabBar::tab{background:transparent;color:#5d6c77;font-size:11px;padding:5px 12px;"
        "border:0;border-bottom:2px solid transparent;}"
        "QTabBar::tab:hover{color:#c8d2da;}"
        "QTabBar::tab:selected{color:#f2b134;border-bottom:2px solid #f2b134;}"));
    srcLay->addWidget(m_srcTabs);
    connect(m_srcTabs, &QTabBar::currentChanged, this, [this] { applySrcFilter(); });

    m_srcStack = new QStackedWidget(srcHost);

    m_srcTree = new ChannelTree(m_srcStack);
    m_srcTree->setHeaderHidden(true);
    m_srcTree->setDragEnabled(true);
    m_srcTree->setDragDropMode(QAbstractItemView::DragOnly);
    m_srcTree->setDefaultDropAction(Qt::CopyAction);
    connect(m_srcTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (!item || item->childCount() > 0)
            return;
        const qulonglong ptr = item->data(0, Qt::UserRole).value<qulonglong>();
        if (!ptr)
            return;
        if (auto *c = currentCanvas())
        {
            c->addChannel(reinterpret_cast<const PsChannelInfo *>(ptr));
            log(QStringLiteral("双击加曲线：%1").arg(item->text(0)));
        }
    });
    m_srcTree->setRootIsDecorated(true);
    m_srcTree->setStyleSheet(QStringLiteral("QTreeWidget{border:0;background:#0d1117;font-size:12px;}"));
    m_srcStack->addWidget(m_srcTree);

    m_paramTable = new QTableWidget(0, 2, m_srcStack);
    m_paramTable->setHorizontalHeaderLabels({QStringLiteral("参数"), QStringLiteral("值")});
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->horizontalHeader()->setStretchLastSection(false);
    m_paramTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_paramTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_paramTable->setColumnWidth(1, 66);
    m_paramTable->setShowGrid(false);
    m_paramTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_srcStack->addWidget(m_paramTable);

    m_msgTable = new QTableWidget(0, 3, m_srcStack);
    m_msgTable->setHorizontalHeaderLabels(
        {QStringLiteral("消息"), QStringLiteral("速率"), QStringLiteral("字段")});
    m_msgTable->verticalHeader()->setVisible(false);
    m_msgTable->horizontalHeader()->setStretchLastSection(false);
    m_msgTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_msgTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_msgTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_msgTable->setColumnWidth(1, 62);
    m_msgTable->setColumnWidth(2, 40);
    m_msgTable->setShowGrid(false);
    m_msgTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_msgTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_srcStack->addWidget(m_msgTable);

    connect(m_srcTabs, &QTabBar::currentChanged, m_srcStack, &QStackedWidget::setCurrentIndex);
    srcLay->addWidget(m_srcStack, 1);

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

    connect(runBtn, &QToolButton::clicked, this, [this] {
        m_srcCount->setText(QStringLiteral("3 源 · 运行中"));
        m_srcCount->setStyleSheet(QStringLiteral("color:#7ee787;font-size:10px;background:transparent;"));
        log(QStringLiteral("数据源开始"));
    });
    connect(pauseBtn, &QToolButton::clicked, this, [this] {
        m_srcCount->setText(QStringLiteral("3 源 · 已暂停"));
        m_srcCount->setStyleSheet(QStringLiteral("color:#ffb454;font-size:10px;background:transparent;"));
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
                {{QStringLiteral("提示"), QStringLiteral("选中绘图页签后显示属性")},
                 {QStringLiteral("Y 轴"), QStringLiteral("线性 · 滚轮缩放 / 拖动平移 / 双击复位")}});

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
    auto *runScriptBtn = new QPushButton(QStringLiteral("运行"), scriptHost);
    scriptLay->addWidget(m_script, 1);
    scriptLay->addWidget(runScriptBtn);
    scriptDock->setWidget(scriptHost);
    addDockWidget(Qt::RightDockWidgetArea, scriptDock);
    connect(runScriptBtn, &QPushButton::clicked, this, [this] {
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
        if (d->titleBarWidget())
            continue; // 数据源 dock 自带一行式标题栏，别盖一层

        auto *bar = new DockTitleBar(d);
        bar->setStyleSheet(QStringLiteral(
            "background:#26303a;border-bottom:1px solid #3a4854;"));
        auto *lay = new QHBoxLayout(bar);
        lay->setContentsMargins(10, 0, 6, 0);
        lay->setSpacing(4);

        auto *title = new QLabel(d->windowTitle(), bar);
        title->setStyleSheet(QStringLiteral(
            "color:#e6edf3;font-size:13px;font-weight:600;background:transparent;"));
        lay->addWidget(title);
        lay->addStretch();

        auto *close = new QToolButton(bar);
        close->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
        close->setIconSize(QSize(13, 13));
        close->setCursor(Qt::PointingHandCursor);
        close->setFixedSize(22, 22);
        close->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;border:0;color:#5d6c77;padding:0;}"
            "QToolButton:hover{color:#f85149;background:#222b33;}"));
        QObject::connect(close, &QToolButton::clicked, d, &QDockWidget::close);
        lay->addWidget(close);

        d->setTitleBarWidget(bar);
    }
}

void MainWindow::showSourceMenu(QWidget *row, const QPoint &globalPos)
{
    if (!row)
        return;
    const bool backed = row->property("backed").toBool();
    const QString path = row->property("srcPath").toString();

    QMenu menu(this);
    QAction *toggle = menu.addAction(backed ? QStringLiteral("暂停数据源")
                                            : QStringLiteral("开始数据源"));
    toggle->setEnabled(backed);
    QAction *reload = menu.addAction(QStringLiteral("重新加载"));
    reload->setEnabled(!path.isEmpty());
    QAction *rename = menu.addAction(QStringLiteral("重命名"));
    menu.addSeparator();
    QAction *del = menu.addAction(QStringLiteral("移除数据源"));

    QAction *chosen = menu.exec(globalPos);
    if (chosen == toggle)
    {
        QList<QLabel *> labels = row->findChildren<QLabel *>();
        QLabel *meta = labels.value(2, nullptr);
        if (!meta)
            return;
        const QString cur = meta->text();
        if (cur.contains(QStringLiteral("已暂停")))
        {
            meta->setText(row->property("srcMeta").toString());
            log(QStringLiteral("开始数据源：%1").arg(row->property("srcName").toString()));
        }
        else
        {
            meta->setText(QStringLiteral("已暂停 · %1").arg(cur.section(QStringLiteral("·"), 1).trimmed()));
            log(QStringLiteral("暂停数据源：%1").arg(row->property("srcName").toString()));
        }
    }
    else if (chosen == reload)
    {
        log(QStringLiteral("重新加载：%1").arg(path));
        loadFile(path);
    }
    else if (chosen == rename)
    {
        QList<QLabel *> labels = row->findChildren<QLabel *>();
        QLabel *name = labels.value(1, nullptr);
        if (!name)
            return;
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("重命名数据源"),
                                                   QStringLiteral("名称"), QLineEdit::Normal,
                                                   name->text(), &ok);
        if (ok && !text.trimmed().isEmpty())
        {
            name->setText(text.trimmed());
            row->setProperty("srcName", text.trimmed());
            SourceData *src = sourceById(sourceIdForRow(row));
            if (src)
            {
                src->alias = text.trimmed();
                for (auto *c : findChildren<CanvasWidget *>())
                    c->renameSource(src->path, src->alias);
            }
            log(QStringLiteral("重命名数据源：%1").arg(text.trimmed()));
        }
    }
    else if (chosen == del)
    {
        removeSourceRow(row);
    }
}

void MainWindow::removeSourceRow(QWidget *row)
{
    if (!row || !m_srcRows)
        return;
    const QString name = row->property("srcName").toString();
    const int id = sourceIdForRow(row);
    SourceData *src = sourceById(id);
    if (src)
    {
        const QString path = src->path;
        for (auto *c : findChildren<CanvasWidget *>())
            c->removeChannelsFromSource(path);
        if (src->ds)
            ps_dataset_free(src->ds);
        if (m_activeSource == id)
        {
            m_activeSource = -1;
            m_viewBacked = false;
            m_srcChannels.clear();
            m_srcParams.clear();
            m_srcMsgs.clear();
        }
        int idx = -1;
        for (int i = 0; i < m_sources.size(); ++i)
            if (m_sources.at(i).id == id)
                idx = i;
        if (idx >= 0)
            m_sources.removeAt(idx);
        renderSourceData();
        // 删掉当前源后落到剩下的第一个可绘源
        for (const SourceData &other : m_sources)
        {
            if (other.ds && other.row)
            {
                activateSource(other.row);
                break;
            }
        }
    }
    m_srcRows->removeWidget(row);
    row->deleteLater();
    refreshSrcCount();
    log(QStringLiteral("移除数据源：%1").arg(name));
}

void MainWindow::renderSourceData()
{
    if (!m_srcTree || !m_srcTabs)
        return;
    m_srcTree->clear();
    const int n = m_viewBacked ? m_srcChannels.size() : 0;

    if (!m_viewBacked)
    {
        auto *it = new QTreeWidgetItem(m_srcTree, {QStringLiteral("该数据源暂未提供字段")});
        it->setFlags(Qt::NoItemFlags);
        m_srcTabs->setTabText(0, QStringLiteral("字段 0"));
        m_srcTabs->setTabText(1, QStringLiteral("参数 0"));
        m_srcTabs->setTabText(2, QStringLiteral("消息 0"));

        m_paramTable->setRowCount(1);
        auto *pk = new QTableWidgetItem(QStringLiteral("—"));
        auto *pv = new QTableWidgetItem(QStringLiteral("该数据源未接入，没有参数表"));
        pk->setFlags(Qt::ItemIsEnabled);
        pv->setFlags(Qt::ItemIsEnabled);
        m_paramTable->setItem(0, 0, pk);
        m_paramTable->setItem(0, 1, pv);

        m_msgTable->setRowCount(1);
        for (int c = 0; c < 3; ++c)
        {
            auto *mi = new QTableWidgetItem(c == 0 ? QStringLiteral("—") : QString());
            mi->setFlags(Qt::ItemIsEnabled);
            m_msgTable->setItem(0, c, mi);
        }
        return;
    }

    const bool is_tree = m_srcFmt == QStringLiteral("ULog");
    QMap<QString, QTreeWidgetItem *> groups;
    for (const PsChannelInfo *ch : m_srcChannels)
    {
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
        QTreeWidgetItem *parent = nullptr;
        if (!group.isEmpty())
        {
            auto it = groups.find(group);
            if (it == groups.end())
            {
                auto *g = new QTreeWidgetItem(m_srcTree, {group});
                it = groups.insert(group, g);
            }
            parent = it.value();
        }
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent, {leaf})
                                       : new QTreeWidgetItem(m_srcTree, {leaf});
        item->setToolTip(0, name);
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
        item->setData(0, Qt::UserRole + 1, name);
    }
    const bool few_groups = groups.size() <= 8;
    for (auto *g : groups)
        g->setExpanded(few_groups && is_tree);
    if (is_tree && !few_groups && !groups.isEmpty())
        groups.first()->setExpanded(true);

    m_srcTabs->setTabText(0, QStringLiteral("字段 %1").arg(n));
    m_srcTabs->setTabText(1, QStringLiteral("参数 %1").arg(m_srcParams.size()));
    m_srcTabs->setTabText(2, QStringLiteral("消息 %1").arg(m_srcMsgs.size()));

    m_paramTable->setRowCount(m_srcParams.size());
    for (int i = 0; i < m_srcParams.size(); ++i)
    {
        auto *k = new QTableWidgetItem(m_srcParams[i].first);
        auto *v = new QTableWidgetItem(m_srcParams[i].second);
        k->setFlags(Qt::ItemIsEnabled);
        v->setFlags(Qt::ItemIsEnabled);
        m_paramTable->setItem(i, 0, k);
        m_paramTable->setItem(i, 1, v);
    }
    if (m_srcParams.isEmpty())
    {
        m_paramTable->setRowCount(1);
        auto *k = new QTableWidgetItem(QStringLiteral("—"));
        auto *v = new QTableWidgetItem(QStringLiteral("%1 未提供参数表（仅 ULog 带 P/Q 参数消息）").arg(m_srcFmt));
        k->setFlags(Qt::ItemIsEnabled);
        v->setFlags(Qt::ItemIsEnabled);
        m_paramTable->setItem(0, 0, k);
        m_paramTable->setItem(0, 1, v);
    }

    m_msgTable->setRowCount(m_srcMsgs.size());
    for (int i = 0; i < m_srcMsgs.size(); ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            auto *mi = new QTableWidgetItem(m_srcMsgs[i].value(c));
            mi->setFlags(Qt::ItemIsEnabled);
            if (c > 0)
                mi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(i, c, mi);
        }
    }
    if (m_srcMsgs.isEmpty())
    {
        m_msgTable->setRowCount(1);
        for (int c = 0; c < 3; ++c)
        {
            auto *mi = new QTableWidgetItem(c == 0 ? QStringLiteral("—") : QStringLiteral("—"));
            mi->setFlags(Qt::ItemIsEnabled);
            if (c > 0)
                mi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(0, c, mi);
        }
    }
}

int MainWindow::sourceIdForRow(QWidget *row) const
{
    return row ? row->property("srcId").toInt() : -1;
}

SourceData *MainWindow::sourceById(int id)
{
    for (SourceData &src : m_sources)
        if (src.id == id)
            return &src;
    return nullptr;
}

void MainWindow::switchSource(int id)
{
    SourceData *src = sourceById(id);
    if (!src || !src->ds)
        return;
    m_activeSource = id;
    m_viewBacked = true;
    m_srcFmt = src->fmt;
    m_srcChannels = src->channels;
    m_srcParams = src->params;
    m_srcMsgs = src->msgs;
    for (auto *c : findChildren<CanvasWidget *>())
        c->setInputSource(src->id, src->path, src->alias);
    renderSourceData();
}

void MainWindow::activateSource(QWidget *row)
{
    if (!row || !m_srcRows)
        return;
    for (int i = 0; i < m_srcRows->count(); ++i)
    {
        QWidget *w = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
        if (!w)
            continue;
        const bool on = (w == row);
        if (w->property("active").toBool() != on)
        {
            w->setProperty("active", on);
            w->style()->unpolish(w);
            w->style()->polish(w);
        }
    }
    if (row->property("backed").toBool() && sourceById(sourceIdForRow(row)))
    {
        switchSource(sourceIdForRow(row));
        return;
    }
    m_viewBacked = false;
    m_srcChannels.clear();
    m_srcParams.clear();
    m_srcMsgs.clear();
    renderSourceData();
}

QWidget *MainWindow::addSourceRow(const QString &name, const QString &meta, const QString &dotColor, bool backed,
                              const QString &path, int srcId)
{
    if (!m_srcRows)
        return nullptr;
    QWidget *rowsHost = m_srcRows->parentWidget();
    auto *row = new QWidget(rowsHost);
    row->setFixedHeight(40);
    row->setProperty("active", false);
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    row->setProperty("srcRow", true);
    row->setProperty("backed", backed);
    row->setProperty("srcPath", path);
    row->setProperty("srcMeta", meta);
    row->setProperty("srcName", name);
    row->setProperty("srcId", srcId);
    row->installEventFilter(this);
    row->setStyleSheet(QStringLiteral(
        "QWidget{background:transparent;border-left:2px solid transparent;border-bottom:1px solid #171f26;}"
        "QWidget[active=true]{background:#1a2127;border-left-color:#f2b134;}"
        "QWidget:hover{background:#1d242a;}"));
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 2, 6, 2);
    lay->setSpacing(6);

    auto *dot = new QLabel(row);
    dot->setAttribute(Qt::WA_TransparentForMouseEvents);
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dotColor));
    lay->addWidget(dot);

    auto *col = new QWidget(row);
    col->setAttribute(Qt::WA_TransparentForMouseEvents);
    col->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *colLay = new QVBoxLayout(col);
    colLay->setContentsMargins(0, 0, 0, 0);
    colLay->setSpacing(0);
    auto *nameLb = new ElideLabel(name, col);
    nameLb->setStyleSheet(QStringLiteral("color:#c9d1d9;font-size:12px;background:transparent;"));
    auto *metaLb = new QLabel(meta, col);
    metaLb->setStyleSheet(QStringLiteral("color:#5d6c77;font-size:10px;background:transparent;"));
    colLay->addWidget(nameLb);
    colLay->addWidget(metaLb);
    lay->addWidget(col, 1);

    auto *cfg = new QToolButton(row);
    cfg->setText(QStringLiteral("配置"));
    cfg->setCursor(Qt::PointingHandCursor);
    cfg->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;color:#5d6c77;font-size:10px;}"
        "QToolButton:hover{color:#f2b134;}"));
    lay->addWidget(cfg);
    connect(cfg, &QToolButton::clicked, this, [this] {
        QMessageBox::information(this, QStringLiteral("配置"),
            QStringLiteral("Loader 附属编辑器（D13）：M1 接字段映射/帧模板。"));
    });

    auto *del = new QToolButton(row);
    del->setIcon(QIcon(QStringLiteral(":/icons/close.svg")));
    del->setIconSize(QSize(11, 11));
    del->setToolTip(QStringLiteral("移除该数据源"));
    del->setCursor(Qt::PointingHandCursor);
    del->setFixedSize(20, 20);
    del->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;color:#4a545d;padding:0;}"
        "QToolButton:hover{color:#f85149;background:#222b33;}"));
    lay->addWidget(del);
    connect(del, &QToolButton::clicked, this, [this, row] {
        removeSourceRow(row);
    });

    m_srcRows->addWidget(row);
    for (SourceData &src : m_sources)
        if (src.id == srcId)
            src.row = row;
    refreshSrcCount();
    if (backed)
        activateSource(row);
    return row;
}

void MainWindow::refreshSrcCount()
{
    if (!m_srcCount || !m_srcRows)
        return;
    int total = 0;
    int connected = 0;
    for (int i = 0; i < m_srcRows->count(); ++i)
    {
        QWidget *row = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
        if (!row)
            continue;
        ++total;
        if (row->property("connected").toBool())
            ++connected;
    }
    m_srcCount->setText(QStringLiteral("%1 源 · %2 已连接").arg(total).arg(connected));
    m_srcCount->setStyleSheet(QStringLiteral("color:#8494a0;font-size:10px;background:transparent;"));
}

void MainWindow::applySrcFilter()
{
    if (!m_srcFilter || !m_srcTabs)
        return;
    const QString q = m_srcFilter->text().trimmed();
    const int tab = m_srcTabs->currentIndex();
    if (tab == 0 && m_srcTree)
    {
        for (int i = 0; i < m_srcTree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem *topic = m_srcTree->topLevelItem(i);
            const bool topicHit = q.isEmpty() || topic->text(0).contains(q, Qt::CaseInsensitive);
            int childHit = 0;
            for (int j = 0; j < topic->childCount(); ++j)
            {
                QTreeWidgetItem *leaf = topic->child(j);
                const bool hit = topicHit || leaf->text(0).contains(q, Qt::CaseInsensitive);
                leaf->setHidden(!hit);
                if (hit)
                    ++childHit;
            }
            topic->setHidden(q.isEmpty() ? false : (!topicHit && childHit == 0));
            if (!q.isEmpty() && childHit > 0)
                topic->setExpanded(true);
        }
    }
    else if (tab == 1 && m_paramTable)
    {
        for (int r = 0; r < m_paramTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_paramTable->columnCount() && !hit; ++c)
                hit = m_paramTable->item(r, c) && m_paramTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_paramTable->setRowHidden(r, !hit);
        }
    }
    else if (tab == 2 && m_msgTable)
    {
        for (int r = 0; r < m_msgTable->rowCount(); ++r)
        {
            bool hit = q.isEmpty();
            for (int c = 0; c < m_msgTable->columnCount() && !hit; ++c)
                hit = m_msgTable->item(r, c) && m_msgTable->item(r, c)->text().contains(q, Qt::CaseInsensitive);
            m_msgTable->setRowHidden(r, !hit);
        }
    }
}

void MainWindow::newPage()
{
    auto *c = new CanvasWidget(this);
    c->clearChannels();
    c->setTitle(QStringLiteral("新页 · 画布"));
    setupCanvas(c);
    const int plusIdx = m_pages->count() - 1;
    m_pages->insertTab(plusIdx, c, QStringLiteral("新页"));
    m_pages->setCurrentWidget(c);
    log(QStringLiteral("新建页"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"))
            .value(QStringLiteral("restoreLayout"), false).toBool())
        saveLayoutTo(defaultLayoutPath());
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 数据源行：点行切换该源自己的字段/参数/消息
    if (event->type() == QEvent::MouseButtonRelease && watched->property("srcRow").toBool())
    {
        auto *me = static_cast<QMouseEvent *>(event);
        QWidget *row = qobject_cast<QWidget *>(watched);
        activateSource(row);
        if (me->button() == Qt::RightButton)
            showSourceMenu(row, me->globalPosition().toPoint());
        return false;
    }
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

void MainWindow::createPages()
{
    m_pages = new QTabWidget(this);
    m_pages->setDocumentMode(true);
    m_pages->setTabsClosable(true);
    m_pages->tabBar()->setExpanding(false);

    auto makeCanvas = [this](const QString &title) {
        auto *c = new CanvasWidget(this);
        c->setTitle(title);
        setupCanvas(c);
        return c;
    };

    // 页 1：三 plot 竖排（QSplitter 树，D15）
    auto *page1 = new QSplitter(Qt::Vertical, this);
    page1->setHandleWidth(6);
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
        if (m_pages->count() > 2 && !m_pages->tabText(idx).isEmpty())
            m_pages->removeTab(idx);
    });

    setCentralWidget(m_pages);
    // 末尾 “+” 页签：浏览器式新建入口，避免挂角落控件被右侧 dock 挤住
    const int plusIdx = m_pages->count();
    m_pages->addTab(new QWidget(m_pages), QStringLiteral("＋"));
    m_pages->tabBar()->setTabToolTip(plusIdx, QStringLiteral("新建页"));
    m_pages->tabBar()->setTabButton(plusIdx, QTabBar::RightSide, nullptr);
    connect(m_pages, &QTabWidget::currentChanged, this, [this](int i) {
        if (i >= 0 && i == m_pages->count() - 1 && m_pages->tabText(i) == QStringLiteral("＋"))
            newPage();
    });

}

void MainWindow::setupCanvas(CanvasWidget *c)
{
    connect(c, &CanvasWidget::activated, this, [this, c](const QString &t) {
        setPropRows(t, {{QStringLiteral("类型"), QStringLiteral("画布")},
                        {QStringLiteral("时间窗"), QStringLiteral("%1 ~ %2 s")
                                                  .arg(c->t0(), 0, 'f', 2)
                                                  .arg(c->t1(), 0, 'f', 2)},
                        {QStringLiteral("数据来源"), c->kernelBacked() ? QStringLiteral("数据集（内核内存）")
                                                                      : QStringLiteral("演示合成数据")},
                        {QStringLiteral("Y 轴"), QStringLiteral("线性 · Ctrl+滚轮缩 / 拖动平移 / 右键复位")}});
    });
    // 同页 X 轴联动：任一画布缩放或平移，其余画布跟着走
    connect(c, &CanvasWidget::timeRangeChanged, this, [this, c](double t0, double t1) {
        if (auto *sp = qobject_cast<QSplitter *>(c->parentWidget()))
        {
            for (int i = 0; i < sp->count(); ++i)
                if (auto *o = qobject_cast<CanvasWidget *>(sp->widget(i)))
                    if (o != c)
                        o->setTimeRange(t0, t1);
        }
    });
    connect(c, &CanvasWidget::addRequested, this, [this] { addCanvasToPage(); });
    connect(c, &CanvasWidget::addChannelRequested, this, [this, c] {
        if (m_srcChannels.isEmpty())
        {
            log(QStringLiteral("先加载数据文件，再添加曲线"));
            return;
        }
        QStringList names;
        for (const PsChannelInfo *ch : m_srcChannels)
        {
            const QString n = QString::fromUtf8(ch->name);
            names.append(ch->unit ? QStringLiteral("%1 [%2]").arg(n, QString::fromUtf8(ch->unit)) : n);
        }
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, QStringLiteral("添加曲线"),
                                                   QStringLiteral("通道"), names, 0, false, &ok);
        if (ok && !pick.isEmpty())
        {
            c->addChannel(m_srcChannels.value(names.indexOf(pick)));
            log(QStringLiteral("添加曲线：%1").arg(pick));
        }
    });
    connect(c, &CanvasWidget::removeRequested, this, [this, c] { removeCanvas(c); });
}

void MainWindow::addCanvasToPage()
{
    QWidget *page = m_pages->currentWidget();
    const int idx = m_pages->currentIndex();
    const QString tabText = m_pages->tabText(idx);
    if (qobject_cast<QLabel *>(page))
    {
        log(QStringLiteral("该页不是绘图页，先切到绘图页再加画布"));
        return;
    }

    auto *c = new CanvasWidget(m_pages);
    c->clearChannels();
    c->setTitle(QStringLiteral("新画布 · plot"));
    setupCanvas(c);

    if (auto *sp = qobject_cast<QSplitter *>(page))
    {
        QList<int> sizes = sp->sizes();
        int total = 0;
        for (int v : sizes)
            total += v;
        sizes.append(total > 0 ? total / (sizes.size() + 1) : 200);
        sp->addWidget(c);
        sp->setSizes(sizes);
        const double t0 = pageCanvases().value(0, nullptr) ? pageCanvases().first()->t0() : 0.0;
        const double t1 = pageCanvases().value(0, nullptr) ? pageCanvases().first()->t1() : 30.0;
        c->setTimeRange(t0, t1);
    }
    else if (auto *old = qobject_cast<CanvasWidget *>(page))
    {
        auto *sp = new QSplitter(Qt::Vertical, m_pages);
        sp->setHandleWidth(6);
        m_pages->removeTab(idx);
        m_pages->insertTab(idx, sp, tabText);
        sp->addWidget(old);
        sp->addWidget(c);
        sp->setSizes({1, 1});
        m_pages->setCurrentIndex(idx);
    }
    log(QStringLiteral("添加画布"));
}

void MainWindow::removeCanvas(CanvasWidget *c)
{
    if (!c)
        return;
    if (auto *sp = qobject_cast<QSplitter *>(c->parentWidget()))
    {
        if (sp->count() <= 1)
        {
            log(QStringLiteral("页内至少保留一个画布"));
            return;
        }
        c->deleteLater();
        log(QStringLiteral("删除画布"));
        return;
    }
    log(QStringLiteral("页内至少保留一个画布"));
}

QString MainWindow::defaultLayoutPath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.plotscope");
    return dir + QStringLiteral("/last.psl");
}

QJsonObject MainWindow::pageState(QWidget *page) const
{
    QJsonObject o;
    if (auto *c = qobject_cast<CanvasWidget *>(page))
    {
        o = c->saveState();
        o[QStringLiteral("type")] = QStringLiteral("canvas");
    }
    else if (auto *sp = qobject_cast<QSplitter *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("splitter");
        o[QStringLiteral("vertical")] = sp->orientation() == Qt::Vertical;
        QJsonArray sizes;
        for (int v : sp->sizes())
            sizes.append(v);
        o[QStringLiteral("sizes")] = sizes;
        QJsonArray kids;
        for (int i = 0; i < sp->count(); ++i)
            kids.append(pageState(sp->widget(i)));
        o[QStringLiteral("children")] = kids;
    }
    else if (auto *lb = qobject_cast<QLabel *>(page))
    {
        o[QStringLiteral("type")] = QStringLiteral("label");
        o[QStringLiteral("text")] = lb->text();
    }
    return o;
}

QWidget *MainWindow::buildPage(const QJsonObject &o)
{
    const QString type = o.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("canvas"))
    {
        auto *c = new CanvasWidget(m_pages);
        c->clearChannels();
        QHash<QString, const PsChannelInfo *> pool;
        for (const SourceData &src : m_sources)
        {
            for (const PsChannelInfo *ch : src.channels)
            {
                const QString name = QString::fromUtf8(ch->name);
                pool.insert(src.path + QLatin1Char('|') + name, ch);
                if (!pool.contains(name))
                    pool.insert(name, ch);
            }
        }
        c->restoreState(o, pool);
        setupCanvas(c);
        return c;
    }
    if (type == QLatin1String("splitter"))
    {
        auto *sp = new QSplitter(o.value(QStringLiteral("vertical")).toBool(true) ? Qt::Vertical
                                                                                 : Qt::Horizontal,
                                 m_pages);
        sp->setHandleWidth(6);
        for (const QJsonValue &v : o.value(QStringLiteral("children")).toArray())
            sp->addWidget(buildPage(v.toObject()));
        QList<int> sizes;
        for (const QJsonValue &v : o.value(QStringLiteral("sizes")).toArray())
            sizes.append(v.toInt());
        if (!sizes.isEmpty())
            sp->setSizes(sizes);
        return sp;
    }
    return new QLabel(o.value(QStringLiteral("text")).toString(), m_pages);
}

void MainWindow::saveLayoutTo(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("PlotScope");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("geometry")] = QString::fromLatin1(saveGeometry().toBase64());
    root[QStringLiteral("docks")] = QString::fromLatin1(saveState().toBase64());
    root[QStringLiteral("current")] = m_pages->currentIndex();

    QJsonArray pages;
    QJsonArray titles;
    const int last = m_pages->count() - 1;
    for (int i = 0; i < m_pages->count(); ++i)
    {
        if (i == last && m_pages->tabText(i) == QStringLiteral("＋"))
            continue;
        pages.append(pageState(m_pages->widget(i)));
        titles.append(m_pages->tabText(i));
    }
    root[QStringLiteral("pages")] = pages;
    root[QStringLiteral("titles")] = titles;

    // 数据源与布局一起保存：重启后先重新加载日志，再回填画布通道
    // 数据源与布局一起保存：重启后按顺序重载全部源，再回填画布通道
    QJsonArray sources;
    for (const SourceData &src : m_sources)
    {
        if (src.path.isEmpty())
            continue;
        QJsonObject so;
        so[QStringLiteral("path")] = src.path;
        so[QStringLiteral("name")] = src.name;
        so[QStringLiteral("alias")] = src.alias;
        sources.append(so);
    }
    root[QStringLiteral("sources")] = sources;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        log(QStringLiteral("保存布局失败：%1").arg(f.errorString()));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope")).setValue(QStringLiteral("lastLayout"), path);
    log(QStringLiteral("已保存布局：%1").arg(path));
}

void MainWindow::importLayoutFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        log(QStringLiteral("导入布局失败：%1").arg(f.errorString()));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("PlotScope"))
    {
        log(QStringLiteral("不是 PlotScope 布局文件"));
        return;
    }

    // 先恢复数据源，buildPage 才能按通道名取到内核指针
    for (const QJsonValue &v : root.value(QStringLiteral("sources")).toArray())
    {
        const QString path = v.toObject().value(QStringLiteral("path")).toString();
        if (path.isEmpty() || !QFile::exists(path))
            continue;
        bool loaded = false;
        for (int i = 0; m_srcRows && i < m_srcRows->count(); ++i)
        {
            QWidget *row = m_srcRows->itemAt(i) ? m_srcRows->itemAt(i)->widget() : nullptr;
            if (row && row->property("srcPath").toString() == path)
            {
                loaded = true;
                break;
            }
        }
        if (!loaded)
            loadFile(path);
        const QString alias = v.toObject().value(QStringLiteral("alias")).toString();
        if (!alias.isEmpty())
        {
            for (SourceData &src : m_sources)
                if (src.path == path)
                    src.alias = alias;
        }
    }

    // 清空页签时屏蔽 currentChanged：末尾的 ＋ 触发 newPage 会和删除循环互踢
    m_pages->blockSignals(true);
    while (m_pages->count() > 0)
        m_pages->removeTab(0);
    m_pages->blockSignals(false);

    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    const QJsonArray titles = root.value(QStringLiteral("titles")).toArray();
    for (int i = 0; i < pages.size(); ++i)
        m_pages->addTab(buildPage(pages[i].toObject()),
                        titles.at(i).toString(QStringLiteral("页 %1").arg(i + 1)));

    const int plusIdx = m_pages->count();
    m_pages->addTab(new QWidget(m_pages), QStringLiteral("＋"));
    m_pages->tabBar()->setTabToolTip(plusIdx, QStringLiteral("新建页"));
    m_pages->tabBar()->setTabButton(plusIdx, QTabBar::RightSide, nullptr);

    const QByteArray geo = QByteArray::fromBase64(root.value(QStringLiteral("geometry")).toString().toLatin1());
    if (!geo.isEmpty())
        restoreGeometry(geo);
    const QByteArray docks = QByteArray::fromBase64(root.value(QStringLiteral("docks")).toString().toLatin1());
    if (!docks.isEmpty())
        restoreState(docks);

    if (m_activeSource >= 0)
        switchSource(m_activeSource);

    const int cur = root.value(QStringLiteral("current")).toInt(0);
    if (cur >= 0 && cur < m_pages->count() - 1)
        m_pages->setCurrentIndex(cur);

    QSettings(QStringLiteral("NextPilot"), QStringLiteral("PlotScope")).setValue(QStringLiteral("lastLayout"), path);
    log(QStringLiteral("已导入布局：%1").arg(path));
}

void MainWindow::openSettings()
{
    QSettings st(QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("设置"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *restore = new QCheckBox(QStringLiteral("启动时恢复上次布局"), &dlg);
    restore->setChecked(st.value(QStringLiteral("restoreLayout"), false).toBool());
    auto *grid = new QCheckBox(QStringLiteral("新建画布默认显示网格"), &dlg);
    grid->setChecked(st.value(QStringLiteral("gridDefault"), true).toBool());
    auto *legend = new QCheckBox(QStringLiteral("新建画布默认显示图例"), &dlg);
    legend->setChecked(st.value(QStringLiteral("legendDefault"), true).toBool());
    lay->addWidget(restore);
    lay->addWidget(grid);
    lay->addWidget(legend);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted)
        return;
    st.setValue(QStringLiteral("restoreLayout"), restore->isChecked());
    st.setValue(QStringLiteral("gridDefault"), grid->isChecked());
    st.setValue(QStringLiteral("legendDefault"), legend->isChecked());
    log(QStringLiteral("设置已保存"));
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


    const uint32_t n = ps_dataset_channel_count(ds);
    const QString fmt_tag = is_ulog ? QStringLiteral("ULog")
                          : is_asc  ? QStringLiteral("ASC · CAN")
                          : is_blf  ? QStringLiteral("BLF · CAN")
                                    : QStringLiteral("CSV");
    SourceData src;
    for (const SourceData &old : m_sources)
        src.id = qMax(src.id, old.id + 1);
    src.name = fi.fileName();
    src.alias = fi.completeBaseName();
    for (const SourceData &old : m_sources)
        if (old.alias == src.alias)
            src.alias += QStringLiteral(" #%1").arg(src.id + 1);
    src.path = fi.absoluteFilePath();
    src.fmt = fmt_tag;
    src.ds = ds;
    for (uint32_t i = 0; i < n; ++i)
        if (const PsChannelInfo *ch = ps_dataset_channel(ds, i))
            src.channels.append(ch);

    // 参数：ULog 走 ps_ulog_params（P/Q 消息），其它格式暂无参数表
    if (fi.suffix().compare(QStringLiteral("ulg"), Qt::CaseInsensitive) == 0)
    {
        QByteArray buf(96 * 1024, '\0');
        char err[256] = {0};
        const int prc = ps_ulog_params(fi.absoluteFilePath().toUtf8().constData(),
                                      buf.data(), static_cast<uint32_t>(buf.size()), err, sizeof err);
        while (!buf.isEmpty() && buf.at(buf.size() - 1) == '\0')
            buf.chop(1);
        if (prc == 0)
        {
            for (const QJsonValue &v : QJsonDocument::fromJson(buf).array())
            {
                const QJsonObject o = v.toObject();
                src.params.append({o.value(QStringLiteral("name")).toString(),
                                    o.value(QStringLiteral("value")).toString()});
            }
        }
        else
        {
            log(QStringLiteral("ps_ulog_params rc=%1 err=%2").arg(prc).arg(QString::fromUtf8(err)));
        }
    }

    // 消息：按 topic 聚合（ULog 才有 `topic.field` 层级）
    if (is_tree)
    {
        QMap<QString, QVector<const PsChannelInfo *>> byTopic;
        for (const PsChannelInfo *ch : src.channels)
        {
            const QString name = QString::fromUtf8(ch->name);
            const int dot = name.lastIndexOf(QLatin1Char('.'));
            if (dot > 0)
                byTopic[name.left(dot)].append(ch);
        }
        for (auto it = byTopic.constBegin(); it != byTopic.constEnd(); ++it)
        {
            QString hz = QStringLiteral("—");
            const PsChannelInfo *ch = it.value().isEmpty() ? nullptr : it.value().first();
            if (ch && ch->point_count > 1)
            {
                const double span = ch->time[ch->point_count - 1] - ch->time[0];
                if (span > 0)
                    hz = QStringLiteral("%1 Hz").arg((ch->point_count - 1) / span, 0, 'f',
                                                     (ch->point_count - 1) / span >= 10 ? 0 : 1);
            }
            src.msgs.append({it.key(), hz, QString::number(it.value().size())});
        }
    }

    m_sources.append(src);
    addSourceRow(src.name, QStringLiteral("已加载 · %1 通道").arg(n), QStringLiteral("#7ee787"), true, src.path,
                 src.id);

    // 一格里一个通道：混量纲（deg / rpm / V）塞一条 Y 轴会把小信号压成直线
    const QVector<CanvasWidget *> canvases = pageCanvases();
    const bool firstSource = m_sources.size() == 1;
    for (int i = 0; firstSource && i < canvases.size() && i < m_srcChannels.size(); ++i)
    {
        canvases[i]->setChannels({m_srcChannels[i]});
        canvases[i]->setTitle(QStringLiteral("%1 · %2").arg(fi.completeBaseName(), QString::fromUtf8(m_srcChannels[i]->name)));
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

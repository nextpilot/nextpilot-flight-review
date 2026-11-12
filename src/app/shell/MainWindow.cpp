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
        if (text.size() > 2)
            a->setProperty("rbIconOnly", true);
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
    addSourceRow(QStringLiteral("ulog · flight_2026-11-12.ulg"), QStringLiteral("已加载 · 13 通道"), QStringLiteral("#7ee787"));
    addSourceRow(QStringLiteral("can · vehicle_bus"), QStringLiteral("已暂停 · 2 通道"), QStringLiteral("#ffb454"));
    addSourceRow(QStringLiteral("mav · 192.168.1.20:14550"), QStringLiteral("空闲 · 未连接"), QStringLiteral("#4a545d"));
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

void MainWindow::addSourceRow(const QString &name, const QString &meta, const QString &dotColor)
{
    if (!m_srcRows)
        return;
    QWidget *rowsHost = m_srcRows->parentWidget();
    auto *row = new QWidget(rowsHost);
    row->setFixedHeight(40);
    row->setProperty("active", m_srcRows->count() == 0);
    row->setProperty("connected", dotColor != QStringLiteral("#4a545d"));
    row->setStyleSheet(QStringLiteral(
        "QWidget{background:transparent;border-left:2px solid transparent;border-bottom:1px solid #171f26;}"
        "QWidget[active=true]{background:#1a2127;border-left-color:#f2b134;}"
        "QWidget:hover{background:#1d242a;}"));
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 2, 6, 2);
    lay->setSpacing(6);

    auto *dot = new QLabel(row);
    dot->setFixedSize(6, 6);
    dot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(dotColor));
    lay->addWidget(dot);

    auto *col = new QWidget(row);
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
    connect(del, &QToolButton::clicked, this, [this, row, name] {
        m_srcRows->removeWidget(row);
        row->deleteLater();
        refreshSrcCount();
        log(QStringLiteral("移除数据源：%1").arg(name));
    });

    m_srcRows->addWidget(row);
    refreshSrcCount();
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
    connect(c, &CanvasWidget::activated, this, [this, c](const QString &t) {
        setPropRows(t, {{QStringLiteral("类型"), QStringLiteral("画布")},
                        {QStringLiteral("时间窗"), QStringLiteral("%1 ~ %2 s")
                                                  .arg(c->t0(), 0, 'f', 2)
                                                  .arg(c->t1(), 0, 'f', 2)},
                        {QStringLiteral("数据来源"), c->kernelBacked() ? QStringLiteral("数据集（内核内存）")
                                                                      : QStringLiteral("演示合成数据")},
                        {QStringLiteral("Y 轴"), QStringLiteral("线性 · 滚轮缩 / 拖动平移 / 双击复位")}});
    });
    const int plusIdx = m_pages->count() - 1;
    m_pages->insertTab(plusIdx, c, QStringLiteral("新页"));
    m_pages->setCurrentWidget(c);
    log(QStringLiteral("新建页"));
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
    // 源列表已在上面，字段页只放通道树：清空重填，按 topic 分组
    m_srcTree->clear();
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

    // 页签计数 + 参数/消息两页（消息按 topic 聚合成“速率/字段数/点数”）
    m_srcTabs->setTabText(0, QStringLiteral("字段 %1").arg(n));
    m_srcTabs->setTabText(1, QStringLiteral("参数 0"));
    m_srcTabs->setTabText(2, QStringLiteral("消息 %1").arg(groups.size()));

    // 参数页：ULog 走 ps_ulog_params（P/Q 消息），其它格式暂无参数表
    int nparam = 0;
    m_paramTable->setRowCount(0);
    if (fi.suffix().compare(QStringLiteral("ulg"), Qt::CaseInsensitive) == 0)
    {
        QByteArray buf(96 * 1024, '\0');
        char err[256] = {0};
        const int prc = ps_ulog_params(fi.absoluteFilePath().toUtf8().constData(),
                                      buf.data(), static_cast<uint32_t>(buf.size()), err, sizeof err);
        // 缓冲预填 0，解析前必须削掉尾部 NUL，否则 Qt 的 JSON 解析器会当作非法尾字符
        while (!buf.isEmpty() && buf.at(buf.size() - 1) == '\0')
            buf.chop(1);
        if (prc == 0)
        {
            const QJsonArray arr = QJsonDocument::fromJson(buf).array();
            nparam = arr.size();
            m_paramTable->setRowCount(nparam);
            for (int i = 0; i < nparam; ++i)
            {
                const QJsonObject o = arr[i].toObject();
                auto *k = new QTableWidgetItem(o.value(QStringLiteral("name")).toString());
                auto *v = new QTableWidgetItem(o.value(QStringLiteral("value")).toString());
                k->setFlags(Qt::ItemIsEnabled);
                v->setFlags(Qt::ItemIsEnabled);
                m_paramTable->setItem(i, 0, k);
                m_paramTable->setItem(i, 1, v);
            }
        }
        else
        {
            log(QStringLiteral("ps_ulog_params rc=%1 err=%2").arg(prc).arg(QString::fromUtf8(err)));
            m_paramTable->insertRow(0);
            m_paramTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("—")));
            m_paramTable->setItem(0, 1, new QTableWidgetItem(QStringLiteral("参数表读取失败：%1").arg(QString::fromUtf8(err))));
        }
    }
    if (m_paramTable->rowCount() == 0)
    {
        m_paramTable->insertRow(0);
        m_paramTable->setItem(0, 0, new QTableWidgetItem(QStringLiteral("—")));
        m_paramTable->setItem(0, 1, new QTableWidgetItem(
            QStringLiteral("%1 未提供参数表（仅 ULog 带 P/Q 参数消息）").arg(fmt_tag)));
    }
    m_srcTabs->setTabText(1, QStringLiteral("参数 %1").arg(nparam));

    // 消息页：topic + 字段数 + 采样率（由该 topic 首通道的时间戳差分算出）
    m_msgTable->setRowCount(0);
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it)
    {
        auto *g = it.value();
        QString hz = QStringLiteral("—");
        if (g->childCount() > 0)
        {
            auto *leaf = g->child(0);
            const PsChannelInfo *ch = reinterpret_cast<const PsChannelInfo *>(
                leaf->data(0, Qt::UserRole).value<qulonglong>());
            if (ch && ch->point_count > 1)
            {
                const double span = ch->time[ch->point_count - 1] - ch->time[0];
                if (span > 0)
                    hz = QStringLiteral("%1 Hz").arg((ch->point_count - 1) / span, 0, 'f',
                                                     (ch->point_count - 1) / span >= 10 ? 0 : 1);
            }
            const int row = m_msgTable->rowCount();
            m_msgTable->insertRow(row);
            auto *name = new QTableWidgetItem(it.key());
            name->setToolTip(QStringLiteral("%1 · %2 点").arg(it.key()).arg(ch ? ch->point_count : 0));
            name->setFlags(Qt::ItemIsEnabled);
            auto *rate = new QTableWidgetItem(hz);
            rate->setFlags(Qt::ItemIsEnabled);
            rate->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto *nf = new QTableWidgetItem(QString::number(g->childCount()));
            nf->setFlags(Qt::ItemIsEnabled);
            nf->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(row, 0, name);
            m_msgTable->setItem(row, 1, rate);
            m_msgTable->setItem(row, 2, nf);
        }
    }
    if (m_msgTable->rowCount() == 0)
    {
        m_msgTable->insertRow(0);
        auto *name = new QTableWidgetItem(QStringLiteral("—"));
        name->setFlags(Qt::ItemIsEnabled);
        name->setToolTip(QStringLiteral("%1 · 无 topic 分组（平表）").arg(fmt_tag));
        m_msgTable->setItem(0, 0, name);
        for (int c = 1; c < 3; ++c)
        {
            auto *it2 = new QTableWidgetItem(QStringLiteral("—"));
            it2->setFlags(Qt::ItemIsEnabled);
            it2->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_msgTable->setItem(0, c, it2);
        }
    }

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
    addSourceRow(fi.fileName(), QStringLiteral("已加载 · %1 通道").arg(n), QStringLiteral("#7ee787"));
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

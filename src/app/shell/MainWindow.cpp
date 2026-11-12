#include "shell/MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
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

namespace {
const char *kStyle = R"(
QWidget { background:#0d1117; color:#c9d1d9; font-size:12px; }
QDockWidget { titlebar-close-icon:none; }
QDockWidget::title { background:#161b22; padding:5px 8px; color:#8b949e; }
QToolBar { background:#161b22; border:0; spacing:2px; padding:3px 6px; }
QToolButton { padding:5px 9px; border:1px solid transparent; border-radius:3px; }
QToolButton:hover { background:#21262d; border-color:#30363d; }
QTabBar::tab { background:#161b22; padding:5px 14px; margin-right:1px; }
QTabBar::tab:selected { background:#1f2632; color:#ffffff; border-bottom:2px solid #4ea1ff; }
QTabWidget::pane { border:0; }
QTreeWidget, QTableWidget, QPlainTextEdit { background:#0d1117; border:1px solid #21262d; }
QHeaderView::section { background:#161b22; border:0; padding:4px; color:#8b949e; }
)";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PlotScope 0.1.0 — 绘图分析"));
    resize(1440, 880);
    setStyleSheet(QString::fromLatin1(kStyle));

    setMenuWidget(createRibbon());
    createDocks();
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
    auto *wrap = new QWidget(this);
    auto *lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *tabs = new QTabBar(wrap);
    tabs->setExpanding(false);
    tabs->setDrawBase(false);
    auto *stack = new QStackedWidget(wrap);

    struct TabDef
    {
        const char *name;
        QList<QPair<QString, std::function<void()>>> actions;
    };

    const QStringList groups = {
        QStringLiteral("文件"), QStringLiteral("数据"), QStringLiteral("绘图"),
        QStringLiteral("仪表"), QStringLiteral("脚本"), QStringLiteral("协议"), QStringLiteral("视图")};

    for (const QString &g : groups)
    {
        auto *bar = new QToolBar(wrap);
        bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        if (g == QStringLiteral("文件"))
        {
            auto add = [&](const QString &text, auto fn) {
                auto *a = bar->addAction(text);
                connect(a, &QAction::triggered, this, fn);
            };
            add(QStringLiteral("打开文件"), [this] {
                const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("打开文件"), {},
                    QStringLiteral("飞行日志 (*.ulg *.bin *.csv *.asc *.blf);;全部 (*)"));
                if (!f.isEmpty())
                    loadFile(f);
            });
            add(QStringLiteral("导入 .psl"), [this] {
                const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("导入布局"), {},
                    QStringLiteral("PlotScope 布局 (*.psl)"));
                if (!f.isEmpty())
                    log(QStringLiteral("导入布局：%1").arg(f));
            });
            add(QStringLiteral("导出 .psl"), [this] {
                const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("导出布局"), {},
                    QStringLiteral("PlotScope 布局 (*.psl)"));
                if (!f.isEmpty())
                    log(QStringLiteral("导出布局：%1").arg(f));
            });
            add(QStringLiteral("截图当前页"), [this] {
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
            add(QStringLiteral("设置"), [this] {
                QMessageBox::information(this, QStringLiteral("设置"),
                    QStringLiteral("M0 骨架：设置页在 M1 接 QSettings。"));
            });
        }
        else if (g == QStringLiteral("数据"))
        {
            auto add = [&](const QString &text, auto fn) {
                auto *a = bar->addAction(text);
                connect(a, &QAction::triggered, this, fn);
            };
            add(QStringLiteral("打开数据源"), [this] { log(QStringLiteral("数据源对话框：M1 接 Loader 选择器")); });
            add(QStringLiteral("全部开始"), [this] {
                m_runLabel->setText(QStringLiteral("运行中"));
                m_runLabel->setStyleSheet(QStringLiteral("color:#7ee787;"));
                log(QStringLiteral("所有数据源开始"));
            });
            add(QStringLiteral("全部暂停"), [this] {
                m_runLabel->setText(QStringLiteral("已暂停"));
                m_runLabel->setStyleSheet(QStringLiteral("color:#ffb454;"));
                log(QStringLiteral("所有数据源暂停"));
            });
        }
        else if (g == QStringLiteral("绘图"))
        {
            auto add = [&](const QString &text, auto fn) {
                auto *a = bar->addAction(text);
                connect(a, &QAction::triggered, this, fn);
            };
            add(QStringLiteral("新建页"), [this] {
                auto *c = new CanvasWidget(this);
                c->setTitle(QStringLiteral("新页 · 画布"));
                connect(c, &CanvasWidget::activated, this,
                        [this](const QString &t) {
                            setPropRows(t, {{QStringLiteral("类型"), QStringLiteral("画布")},
                                            {QStringLiteral("时间窗"), QStringLiteral("30.0 s")},
                                            {QStringLiteral("降采样"), QStringLiteral("min/max 1:4")}});
                        });
                m_pages->addTab(c, QStringLiteral("新页"));
                m_pages->setCurrentWidget(c);
                log(QStringLiteral("新建页"));
            });
            add(QStringLiteral("自动 Y"), [this] { log(QStringLiteral("自动 Y：按可见曲线量程重算")); });
            add(QStringLiteral("游标"), [this] { log(QStringLiteral("十字光标已启用")); });
        }
        else if (g == QStringLiteral("仪表"))
        {
            auto add = [&](const QString &text, auto fn) {
                auto *a = bar->addAction(text);
                connect(a, &QAction::triggered, this, fn);
            };
            add(QStringLiteral("添加地平仪"), [this] { log(QStringLiteral("添加地平仪（QPainter 自绘，M1 落地）")); });
            add(QStringLiteral("添加罗盘"), [this] { log(QStringLiteral("添加罗盘（QPainter 自绘，M1 落地）")); });
        }
        else if (g == QStringLiteral("脚本"))
        {
            auto add = [&](const QString &text, auto fn) {
                auto *a = bar->addAction(text);
                connect(a, &QAction::triggered, this, fn);
            };
            add(QStringLiteral("运行"), [this] {
                log(QStringLiteral("Lua 5.4 逐块执行（10 ms/块）：out = ps.movingAvg(ps.derivative(ps.ch(\"att.roll\")), 5)"));
            });
            add(QStringLiteral("格式化"), [this] { log(QStringLiteral("脚本格式化（stylua 规则，M2 接）")); });
        }
        else if (g == QStringLiteral("协议"))
        {
            auto add = [&](const QString &text, auto fn) {
                auto *a = bar->addAction(text);
                connect(a, &QAction::triggered, this, fn);
            };
            add(QStringLiteral("新建 Loader"), [this] { log(QStringLiteral("新建 Loader：打开附属编辑器（QDialog，D13）")); });
            add(QStringLiteral("帧模板 YAML"), [this] { log(QStringLiteral("帧模板：YAML 热编译（D6）")); });
            add(QStringLiteral("校验"), [this] { log(QStringLiteral("校验模板：字段偏移/位域越界检查（M1）")); });
        }
        else // 视图
        {
            auto add = [&](const QString &text, const char *objName) {
                auto *a = bar->addAction(text);
                connect(a, &QAction::triggered, this, [this, objName] {
                    if (auto *d = findChild<QDockWidget *>(QString::fromLatin1(objName)))
                        d->setVisible(!d->isVisible());
                });
            };
            add(QStringLiteral("数据源面板"), "dock.src");
            add(QStringLiteral("属性面板"), "dock.prop");
            add(QStringLiteral("脚本面板"), "dock.script");
            add(QStringLiteral("日志面板"), "dock.log");
        }

        tabs->addTab(g);
        stack->addWidget(bar);
    }

    connect(tabs, &QTabBar::currentChanged, stack, &QStackedWidget::setCurrentIndex);

    lay->addWidget(tabs);
    lay->addWidget(stack);
    return wrap;
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
    if (ps_csv_load(path.toUtf8().constData(), &ds, err, sizeof(err)) != 0)
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
    Q_UNUSED(n);
    const QFileInfo fi(path);
    auto *top = new QTreeWidgetItem(m_srcTree,
        {QStringLiteral("%1  [CSV · %2 通道]").arg(fi.fileName()).arg(n)});
    QVector<const PsChannelInfo *> channels;
    for (uint32_t i = 0; i < n; ++i)
    {
        const PsChannelInfo *ch = ps_dataset_channel(ds, i);
        if (!ch)
            continue;
        channels.append(ch);
        auto *item = new QTreeWidgetItem(top, {QString::fromUtf8(ch->name)});
        item->setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(ch)));
    }
    top->setExpanded(true);

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
        log(QStringLiteral("已加载 %1：%2 通道 × %3 点，时间窗 %4 s")
                .arg(fi.fileName()).arg(n).arg(first->point_count)
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

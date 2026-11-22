#include <QApplication>
#include <QMainWindow>
#include <QAction>
#include <QLabel>
#include <QStatusBar>
#include <QSplitter>
#include <QDebug>
#include <QStyle>
#include <QTimer>

#include "SARibbonBar.h"
#include "SARibbonTabBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonThemeManager.h"
#include "SARibbonGlobal.h"

#include "ps_kernel.h"
#include "PlottingCanvas.h"

static QAction *act(QObject *owner, const QString &text)
{
    auto *a = new QAction(text, owner);
    a->setToolTip(text);
    return a;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QMainWindow win;
    win.setWindowTitle(QStringLiteral("PlotScope · SARibbon spike"));
    win.resize(1560, 960);

    SARibbonBar *ribbon = new SARibbonBar(&win);
    win.setMenuWidget(ribbon);

    // 七页签，和原型一致
    struct PageDef { const char *page; QList<QPair<QString, QStringList>> panels; };
    const QList<PageDef> defs = {
        {"文件", {{"布局", {"打开文件", "导入 .psl"}}, {"导出", {"导出 .psl", "导出设置", "导出数据"}}, {"截图", {"截图当前页", "另存为图片"}}}},
        {"数据", {{"来源", {"打开 CSV", "连接在线源"}}, {"Loader", {"新增 Loader", "编辑协议"}}}},
        {"绘图", {{"页", {"新建页", "关闭页"}}, {"视图", {"自动 Y", "复位视图", "框选缩放"}}}},
        {"仪表", {{"新建", {"地平仪", "罗盘", "表盘", "文本"}}}},
        {"脚本", {{"Lua", {"运行", "停止", "打开脚本"}}}},
        {"协议", {{"编辑器", {"导入 DBC", "新建帧模板", "校验位域"}}}},
        {"视图", {{"面板", {"数据源", "属性", "脚本", "日志"}}}},
    };

    for (const PageDef &pd : defs)
    {
        SARibbonCategory *cat = ribbon->addCategoryPage(QString::fromUtf8(pd.page));
        for (const auto &pn : pd.panels)
        {
            SARibbonPanel *panel = cat->addPanel(pn.first);
            // 每 panel 第一个大按钮，其余小按钮：对应原型"1 大 + N 小"
            const QStringList &acts = pn.second;
            for (int i = 0; i < acts.size(); ++i)
            {
                QAction *a = act(&win, acts[i]);
                if (i == 0)
                    panel->addLargeAction(a);
                else
                    panel->addSmallAction(a);
            }
        }
    }

    // 右侧状态芯片位置（原型里的 qchip）
    SARibbonButtonGroupWidget *rg = ribbon->rightButtonGroup();
    for (const QString &t : {QStringLiteral("CSV · 3000×5"), QStringLiteral("LOD: Rust")})
    {
        QAction *a = act(&win, t);
        rg->addAction(a);
    }

    // 紧凑两行 + 隐藏标题栏，尽量往原型那套 74px 靠
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleCompactTwoRow);
    ribbon->setTitleVisible(false);
    ribbon->setTabBarHeight(26);
    ribbon->setPanelTitleHeight(15);
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setPanelToolButtonIconSize(QSize(16, 16), QSize(32, 32));
    Q_INIT_RESOURCE(SARibbonResource);
    SA::applyRibbonTheme(&win, ribbon, SARibbonTheme::RibbonThemeDark);

    // 画布：真实 CSV 走内核
    QSplitter *sp = new QSplitter(Qt::Vertical, &win);
    QVector<PlottingCanvas *> canvases;
    for (int i = 0; i < 3; ++i)
    {
        auto *c = new PlottingCanvas(sp);
        c->setTitle(QStringLiteral("空画布"));
        sp->addWidget(c);
        canvases.append(c);
    }
    // 数据加载已下沉插件；这个 spike 只验证 Ribbon，不再直接读 CSV。
    win.setCentralWidget(sp);
    win.statusBar()->showMessage(QStringLiteral("SARibbon %1.%2.%3 · Qt %4")
                                     .arg(SA_RIBBON_BAR_VERSION_MAJ)
                                     .arg(SA_RIBBON_BAR_VERSION_MIN)
                                     .arg(SA_RIBBON_BAR_VERSION_PAT)
                                     .arg(QString(qVersion())));
    win.show();
    if (argc > 2 && QString::fromLocal8Bit(argv[2]) == QLatin1String("min"))
        QTimer::singleShot(500, [ribbon] { ribbon->setMinimumMode(true); });
    QTimer::singleShot(600, [ribbon, &win] {
        qInfo().noquote() << QStringLiteral("[sari] ribbon height = %1 px, tabBar = %2 px, window = %3×%4")
                                 .arg(ribbon->height())
                                 .arg(ribbon->ribbonTabBar()->height())
                                 .arg(win.width())
                                 .arg(win.height());
    });
    return app.exec();
}

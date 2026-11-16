#include "ThemeManager.h"
#include "DataSourceModule.h"

#include "DataSourceEntry.h"
#include "DataSourceRegistry.h"
#include "DataSourceSession.h"
#include "MainWindow.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include <QPalette>
#include <QToolButton>

#include <QAction>
#include <QFileDialog>
#include <QDockWidget>
#include <QIcon>
#include <SARibbonPanel.h>
#include <SARibbonPanelItem.h>

namespace
{
// 每种加载器给一张文件类型图标：扩展名 + 配色，比纯文字好认
QIcon loaderIcon(const DataSourceEntry &e)
{
    const QString key = (e.id + QLatin1Char(' ') + e.label + QLatin1Char(' ')
                         + e.extensions.join(QLatin1Char(' ')))
                            .toLower();
    struct Rule
    {
        const char *needle;
        const char *alias;
    };
    static const Rule rules[] = {
        {"ulog", "file_ulog.svg"},   {"ulg", "file_ulog.svg"},   {"apbin", "file_apbin.svg"},
        {"dataflash", "file_apbin.svg"}, {"blf", "file_blf.svg"},  {"asc", "file_asc.svg"},
        {"mcap", "file_mcap.svg"},   {"mdf", "file_mdf4.svg"},   {"rosbag", "file_rosbag.svg"},
        {"nmea", "file_nmea.svg"},   {"ubx", "file_ubx.svg"},    {"json", "file_json.svg"},
        {"csv", "file_csv.svg"},     {"tsv", "file_csv.svg"},    {"binary", "file_binary.svg"}};
    for (const Rule &r : rules)
        if (key.contains(QLatin1String(r.needle)))
            return QIcon(QStringLiteral(":/icons/") + QLatin1String(r.alias));
    return QIcon(QStringLiteral(":/icons/loader.svg"));
}
} // namespace

namespace ps::components
{
void registerDataSourceContributions(Registry &registry)
{
    registry.addDock(DockContribution{QStringLiteral("dock.src"), QStringLiteral("数据源"),
                                      Qt::LeftDockWidgetArea, true, 0,
                                      QStringLiteral(":/icons/datasource.svg"),
                                      [](QWidget *parent) -> QWidget * {
                                          auto *mw = qobject_cast<MainWindow *>(parent);
                                          return mw ? dataSourceSession(mw)->createDataSourceDock(parent, QStringLiteral(":/icons/datasource.svg")) : nullptr;
                                      },
                                      [](QWidget *parent) {
                                          auto *mw = qobject_cast<MainWindow *>(parent);
                                          auto *dock = mw ? mw->findChild<QDockWidget *>(QStringLiteral("dock.src")) : nullptr;
                                          if (dock) mw->resizeDocks({dock}, {470}, Qt::Vertical);
                                      }});

    registry.addRibbon(RibbonContribution{QStringLiteral("数据"), {}, {},
        {{QStringLiteral("常用"),
             {{QStringLiteral("data.quickOpen"), QStringLiteral("快速打开"), {}, true, QStringLiteral("open")},
              {QStringLiteral("data.recent"), QStringLiteral("最近"), {}, false, QStringLiteral("import")},
              {QStringLiteral("data.exportSnapshot"), QStringLiteral("导出快照"), {}, false, QStringLiteral("export_data")},
              {QStringLiteral("data.reload"), QStringLiteral("重载"), {}, false, QStringLiteral("reset")}}},
         {QStringLiteral("来源"), {},
             [](QWidget *p) {
                 auto *panel = static_cast<SARibbonPanel *>(p);
                 auto *mw = qobject_cast<MainWindow *>(panel->window());
                 auto openFile = [mw](const DataSourceEntry &e) {
                     const QStringList fs = QFileDialog::getOpenFileNames(mw, e.label, {}, e.filter.isEmpty() ? QStringLiteral("全部 (*)") : e.filter);
                     for (const QString &f : fs) dataSourceSession(mw)->openPath(f);
                 };
                 // 图库：一行摆 4 个常用加载器，框内箭头弹出全量列表，
                 // 外观和绘图 ribbon 的图表类型图库同一个组件
                 QList<QAction *> fileActs;
                 for (const DataSourceEntry &e : DataSourceRegistry::instance().fileEntries())
                 {
                     auto *a = new QAction(loaderIcon(e), e.label, panel);
                     a->setToolTip(e.label + (e.filter.isEmpty() ? QString() : QStringLiteral("  ") + e.filter));
                     QObject::connect(a, &QAction::triggered, mw, [openFile, e] { openFile(e); });
                     fileActs.append(a);
                 }
                 QList<QAction *> streamActs;
                 for (const DataSourceEntry &e : DataSourceRegistry::instance().streamEntries())
                 {
                     auto *a = new QAction(loaderIcon(e), e.label, panel);
                     a->setToolTip(e.label);
                     QObject::connect(a, &QAction::triggered, mw,
                                      [mw, id = e.id] { dataSourceSession(mw)->connectSource(id); });
                     streamActs.append(a);
                 }
                 // 原生 SARibbonGallery：单行模式只留框内「更多」，弹出分组由控件的 viewport 负责
                 auto *gallery = panel->addGallery();
                 gallery->setSingleRowMode(true);
                 // vendor 的「更多」按钮用位图箭头，深色主题下看不见；改成文字 ▾，仍是原生按钮
                 if (auto *more = gallery->findChild<QToolButton *>(QStringLiteral("SARibbonGalleryButtonMore")))
                 {
                     more->setToolButtonStyle(Qt::ToolButtonTextOnly);
                     more->setIcon(QIcon());
                     more->setText(QStringLiteral("▾"));
                 }
                 auto styleGroup = [](SARibbonGalleryGroup *g) {
                     g->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
                     g->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
                     g->setGridMinimumWidth(64);
                     g->setGridMaximumWidth(72);
                     g->setSpacing(1);
                     // vendor 弹窗会给 group 分多余高度，锁死单行高度，避免大片留白
                 };
                 auto *common = gallery->addCategoryActions(QStringLiteral("常用"), fileActs.mid(0, 4));
                 auto *formats = gallery->addCategoryActions(QStringLiteral("文件格式"), fileActs);
                 auto *streams = gallery->addCategoryActions(QStringLiteral("实时源"), streamActs);
                 styleGroup(common);
                 styleGroup(formats);
                 styleGroup(streams);
                 gallery->setCurrentViewGroup(common);
                 // 弹出 viewport 是独立 Popup 窗口，主题 QSS 传不进去，落一次背景色
                 if (auto *vp = gallery->getPopupViewPort())
                 {
                     vp->setMinimumWidth(780);
                     QPalette pal = vp->palette();
                     pal.setColor(QPalette::Window, ThemeManager::color(QStringLiteral("panelAlt")));
                     pal.setColor(QPalette::Base, ThemeManager::color(QStringLiteral("panelAlt")));
                     vp->setPalette(pal);
                     vp->setAutoFillBackground(true);
                     if (QWidget *cw = vp->widget())
                     {
                         cw->setPalette(pal);
                         cw->setAutoFillBackground(true);
                     }
                     vp->setAttribute(Qt::WA_StyledBackground, true);
                     vp->setStyleSheet(QStringLiteral(
                         "SARibbonGalleryViewport{background:#1a2127;border:1px solid #232b33;}"
                         "QWidget#contentWidget{background:#1a2127;}"
                         "SARibbonGalleryGroup{background:transparent;border:0;}"));
                 }
                 panel->setMaximumWidth(320);
             }}},
        1});
}

} // namespace ps::components

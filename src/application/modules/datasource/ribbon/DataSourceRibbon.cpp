#include "DataSourceModule.h"

#include "DataSourceEntry.h"
#include "DataSourceRegistry.h"
#include "DataSourceSession.h"
#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QDockWidget>
#include <QIcon>
#include <QMenu>
#include <QToolButton>
#include <SARibbonPanel.h>

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
                 // 面板一行摆不下全部加载器：放前 4 个大按钮（带文件类型图标），
                 // 其余走右端下拉三角，展开后是全量 loader 列表
                 int shown = 0;
                 for (const DataSourceEntry &e : DataSourceRegistry::instance().fileEntries())
                 {
                     auto *a = new QAction(loaderIcon(e), e.label, panel);
                     a->setToolTip(e.label);
                     QObject::connect(a, &QAction::triggered, mw, [openFile, e] { openFile(e); });
                     if (shown++ < 4) panel->addLargeAction(a);
                 }
                 auto *more = new QAction(QStringLiteral("▾"), panel);
                 more->setToolTip(QStringLiteral("更多加载器"));
                 more->setProperty("rbIconOnly", true);
                 auto *moreMenu = new QMenu(panel);
                 QObject::connect(moreMenu, &QMenu::aboutToShow, moreMenu, [moreMenu, mw, openFile] {
                     moreMenu->clear();
                     for (const DataSourceEntry &e : DataSourceRegistry::instance().fileEntries())
                     {
                         auto *a = moreMenu->addAction(loaderIcon(e), e.label);
                         QObject::connect(a, &QAction::triggered, mw, [openFile, e] { openFile(e); });
                     }
                     const QList<DataSourceEntry> streams = DataSourceRegistry::instance().streamEntries();
                     if (!streams.isEmpty())
                     {
                         auto *sub = moreMenu->addMenu(QStringLiteral("实时源"));
                         for (const DataSourceEntry &e : streams)
                         {
                             auto *a = sub->addAction(loaderIcon(e), e.label);
                             QObject::connect(a, &QAction::triggered, mw, [mw, id = e.id] { dataSourceSession(mw)->connectSource(id); });
                         }
                     }
                 });
                 // 下拉三角挂一个即时展开的菜单，一次能列出全部 loader
                 more->setMenu(moreMenu);
                 panel->addSmallAction(more, QToolButton::InstantPopup);
                 panel->setMaximumWidth(520);
             }}},
        1});
}

} // namespace ps::components

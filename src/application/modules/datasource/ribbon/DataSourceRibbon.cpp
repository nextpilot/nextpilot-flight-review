#include "ThemeManager.h"
#include "DataSourceModule.h"

#include "DataSourceEntry.h"
#include "DataSourceRegistry.h"
#include "ProtocolInspectorDock.h"
#include "DataSourceSession.h"
#include "SettingsManager.h"
#include "CommandManager.h"
#include "ModuleContext.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"

#include <QAction>
#include <QDockWidget>
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
            return ThemeManager::icon(QStringLiteral(":/icons/") + QLatin1String(r.alias));
    return ThemeManager::icon(QStringLiteral(":/icons/loader.svg"));
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
                                          auto *ctx = ps::runtime::ModuleContext::fromWidget(parent);
                                          return ctx ? dataSourceSession(*ctx)->createDataSourcePanel(parent) : nullptr;
                                      },
                                      [](QWidget *parent) {
                                          auto *ctx = ps::runtime::ModuleContext::fromWidget(parent);
                                          auto *dock = ctx ? ctx->findDock(QStringLiteral("dock.src")) : nullptr;
                                          if (dock) ctx->resizeDocks({dock}, {470}, Qt::Vertical);
                                      }});

    registry.addDock(DockContribution{QStringLiteral("dock.protocol"), QStringLiteral("协议检查"),
                                      Qt::BottomDockWidgetArea, false, 100,
                                      QStringLiteral(":/icons/datasource.svg"),
                                      [](QWidget *parent) -> QWidget * {
                                          return new ProtocolInspectorDock(parent);
                                      },
                                      nullptr});

    registry.addRibbon(RibbonContribution{QStringLiteral("数据"), {}, {},
        {{QStringLiteral("常用"),
             {{QStringLiteral("data.quickOpen"), QStringLiteral("快速打开"), {}, true, QStringLiteral("open")},
              {QStringLiteral("data.recent"), QStringLiteral("最近"), {}, false, QStringLiteral("import")},
              {QStringLiteral("data.exportSnapshot"), QStringLiteral("导出快照"), {}, false, QStringLiteral("export_data")},
              {QStringLiteral("data.reload"), QStringLiteral("重载"), {}, false, QStringLiteral("reset")}}},
         {QStringLiteral("来源"), {},
             [](QWidget *p) {
                 auto *panel = static_cast<SARibbonPanel *>(p);
                 auto *ctx = ps::runtime::ModuleContext::fromWidget(panel->window());
                 if (!ctx)
                     return;
                 QWidget *mw = ctx->window();
                 auto openFile = [ctx](const DataSourceEntry &e) {
                     const QStringList fs = QFileDialog::getOpenFileNames(ctx->window(), e.label, {}, e.filter.isEmpty() ? QStringLiteral("全部 (*)") : e.filter);
                     for (const QString &f : fs) dataSourceSession(*ctx)->openPath(f);
                 };
                 // 图库：一行摆 4 个常用加载器，框内箭头弹出全量列表，
                 // 外观和绘图 ribbon 的图表类型图库同一个组件
                 QList<QAction *> fileActs;
                 for (const DataSourceEntry &e : DataSourceRegistry::instance().fileEntries())
                 {
                     auto *a = new QAction(loaderIcon(e), e.label, panel);
                     a->setToolTip(e.label + (e.filter.isEmpty() ? QString() : QStringLiteral("  ") + e.filter));
                     QObject::connect(a, &QAction::triggered, panel, [openFile, e] { openFile(e); });
                     fileActs.append(a);
                 }
                 QList<QAction *> streamActs;
                 for (const DataSourceEntry &e : DataSourceRegistry::instance().streamEntries())
                 {
                     auto *a = new QAction(loaderIcon(e), e.label, panel);
                     a->setToolTip(e.label);
                     QObject::connect(a, &QAction::triggered, panel,
                                      [ctx, id = e.id] { dataSourceSession(*ctx)->connectSource(id); });
                     streamActs.append(a);
                 }
                 auto *gallery = panel->addGallery();
                 gallery->setSingleRowMode(true);
                 auto styleGroup = [](SARibbonGalleryGroup *g) {
                     g->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
                     g->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
                     g->setGridMinimumWidth(88);
                     g->setGridMaximumWidth(96);
                     g->setSpacing(1);
                     QFont f = g->font();
                     f.setPixelSize(10);
                     g->setFont(f);
                 };
                 auto *common = gallery->addCategoryActions(QStringLiteral("常用"), fileActs.mid(0, 4));
                 auto *formats = gallery->addCategoryActions(QStringLiteral("文件格式"), fileActs);
                 auto *streams = gallery->addCategoryActions(QStringLiteral("实时源"), streamActs);
                 styleGroup(common);
                 styleGroup(formats);
                 styleGroup(streams);
                 gallery->setCurrentViewGroup(common);
                 panel->setMaximumWidth(420);
             }},
         {QStringLiteral("调试"),
          {{QStringLiteral("protocol.inspect"), QStringLiteral("协议检查"), {}, false,
            QStringLiteral("datasource")}},
          {}},
        },
        1});

}

} // namespace ps::components

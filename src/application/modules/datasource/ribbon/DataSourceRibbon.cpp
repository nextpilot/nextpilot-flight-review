#include "DataSourceModule.h"

#include "DataSourceEntry.h"
#include "DataSourceRegistry.h"
#include "DataSourceSession.h"
#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QDockWidget>
#include <SARibbonGallery.h>
#include <SARibbonGalleryGroup.h>
#include <SARibbonGalleryItem.h>
#include <SARibbonPanel.h>

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
                 auto *gal = panel->addGallery(true);
                 QList<QAction *> files, streams;
                 for (const DataSourceEntry &e : DataSourceRegistry::instance().fileEntries())
                 {
                     auto *a = new QAction(e.label, panel);
                     QObject::connect(a, &QAction::triggered, mw, [mw, e] {
                         const QStringList fs = QFileDialog::getOpenFileNames(mw, e.label, {}, e.filter.isEmpty() ? QStringLiteral("全部 (*)") : e.filter);
                         for (const QString &f : fs) dataSourceSession(mw)->openPath(f);
                     });
                     files.append(a);
                 }
                 for (const DataSourceEntry &e : DataSourceRegistry::instance().streamEntries())
                 {
                     auto *a = new QAction(e.label, panel);
                     QObject::connect(a, &QAction::triggered, mw, [mw, id = e.id] { dataSourceSession(mw)->connectSource(id); });
                     streams.append(a);
                 }
                 auto add = [&](const QString &name, const QList<QAction *> &acts) {
                     auto *g = gal->addGalleryGroup();
                     g->setGalleryGroupStyle(SARibbonGalleryGroup::IconWithText);
                     g->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
                     for (QAction *a : acts) g->addItem(new SARibbonGalleryItem(a));
                     Q_UNUSED(name);
                 };
                 add(QStringLiteral("日志文件"), files);
                 add(QStringLiteral("实时源"), streams);
                 gal->setSingleRowMode(true);
                 panel->setMaximumWidth(380);
             }}},
        1});
}

} // namespace ps::components

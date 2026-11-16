#include "ThemeManager.h"
#include "DataSourceModule.h"

#include "DataSourceEntry.h"
#include "DataSourceRegistry.h"
#include "ProtocolInspectorDock.h"
#include "DataSourceSession.h"
#include "SourceModel.h"
#include "AnalysisRegistry.h"
#include "CommandManager.h"
#include "MainWindow.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include <QPalette>
#include <QToolButton>

#include <QAction>
#include <QActionGroup>
#include <QTimer>
#include <QHash>
#include <QMenu>
#include <QDockWidget>
#include <QFileDialog>
#include <QSettings>
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

namespace
{
void openAnalysisApp(MainWindow *mw, const ps::data_source::AnalysisEntry &entry,
                     const QString &path)
{
    if (!mw || !entry.create)
        return;
    const QString objectName = QStringLiteral("dock.analysis.%1").arg(entry.id);
    if (auto *existing = mw->findChild<QDockWidget *>(objectName))
    {
        existing->show();
        existing->raise();
        return;
    }
    QWidget *body = entry.create(mw, path);
    if (!body)
        return;
    auto *dock = new QDockWidget(entry.title, mw);
    dock->setObjectName(objectName);
    dock->setWidget(body);
    mw->addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->show();
}

// 分析页是唯一入口：领域工具箱只注册条目，不新增 Ribbon 页。
// 当前数据源变化时只替换 Gallery 条目，避免每个工具箱占一个页签。
void buildAnalysisGallery(QWidget *parent)
{
    auto *panel = static_cast<SARibbonPanel *>(parent);
    auto *mw = qobject_cast<MainWindow *>(parent ? parent->window() : nullptr);
    DataSourceSession *session = mw ? ps::components::dataSourceSession(mw) : nullptr;
    if (!panel || !mw || !session)
        return;

    auto *gallery = panel->addGallery();
    gallery->setSingleRowMode(true);
    auto styleGroup = [](SARibbonGalleryGroup *g) {
        g->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
        g->setDisplayRow(SARibbonGalleryGroup::DisplayOneRow);
        g->setGridMinimumWidth(92);
        g->setGridMaximumWidth(104);
        g->setSpacing(2);
        QFont f = g->font();
        f.setPixelSize(10);
        g->setFont(f);
    };

    auto recentCallback = std::make_shared<std::function<void(const QString &)>>();
    auto favoriteCallback = std::make_shared<std::function<void(const QString &)>>();
    auto makeAction = [&](const ps::data_source::AnalysisEntry &entry, bool allGroup) {
        auto *action = new QAction(entry.title, panel);
        action->setToolTip(entry.description.isEmpty() ? entry.title : entry.description);
        if (!entry.icon.isEmpty())
        {
            const QString path = entry.icon.startsWith(QLatin1String(":/"))
                                     ? entry.icon
                                     : QStringLiteral(":/icons/%1.svg").arg(entry.icon);
            action->setIcon(ThemeManager::icon(path));
        }
        action->setProperty("analysisFormats", entry.formats);
        action->setProperty("analysisId", entry.id);
        action->setProperty("analysisInGroup", false);
        action->setProperty("analysisAllGroup", allGroup);
        if (entry.create)
        {
            QObject::connect(action, &QAction::triggered, mw,
                             [mw, session, entry] {
                                 auto *source = session->sourceById(session->activeSourceId());
                                 openAnalysisApp(mw, entry, source ? source->path : QString());
                             });
        }
        else
        {
            QObject::connect(action, &QAction::triggered, mw,
                             [mw, id = entry.id] { mw->commands().invoke(id, {mw, QString()}); });
        }
        QObject::connect(action, &QAction::triggered, mw,
                         [recentCallback, id = entry.id] {
                             if (*recentCallback)
                                 (*recentCallback)(id);
                         });
        return action;
    };

    auto *placeholder = new QAction(QStringLiteral("打开数据源后显示分析应用"), panel);
    placeholder->setEnabled(false);
    placeholder->setIcon(ThemeManager::icon(QStringLiteral(":/icons/datasource.svg")));
    auto *group = gallery->addCategoryActions(QStringLiteral("当前数据源"), {placeholder});
    styleGroup(group);

    QVector<QAction *> entries;
    QVector<QAction *> allEntries;
    for (const ps::data_source::AnalysisEntry &entry : ps::data_source::AnalysisRegistry::instance().all())
    {
        entries.append(makeAction(entry, false));
        allEntries.append(makeAction(entry, true));
    }
    auto *allGroup = gallery->addCategoryActions(QStringLiteral("全部应用"), allEntries);
    styleGroup(allGroup);

    auto recentSettings = std::make_shared<QSettings>(
        QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    auto recentIds = std::make_shared<QStringList>(
        recentSettings->value(QStringLiteral("analysis.recent")).toStringList());
    auto recentActions = std::make_shared<QHash<QString, QAction *>>();
    auto *recentPlaceholder = new QAction(QStringLiteral("暂无最近使用的分析应用"), panel);
    recentPlaceholder->setEnabled(false);
    auto *recentGroup = gallery->addCategoryActions(QStringLiteral("最近使用"), {recentPlaceholder});
    styleGroup(recentGroup);
    gallery->setCurrentViewGroup(group);

    auto entryById = [](const QString &id) -> const ps::data_source::AnalysisEntry * {
        for (const auto &entry : ps::data_source::AnalysisRegistry::instance().all())
            if (entry.id == id)
                return &entry;
        return nullptr;
    };
    *recentCallback = [recentIds, recentActions, recentGroup, recentPlaceholder, entryById, makeAction,
                       recentSettings](const QString &id) mutable {
        if (id.isEmpty())
            return;
        recentIds->removeAll(id);
        recentIds->prepend(id);
        while (recentIds->size() > 6)
            recentIds->removeLast();
        recentSettings->setValue(QStringLiteral("analysis.recent"), *recentIds);

        for (QAction *action : *recentActions)
        {
            if (recentGroup->actionGroup()->actions().contains(action))
                recentGroup->removeActionItem(action);
        }
        for (const QString &recentId : *recentIds)
        {
            const ps::data_source::AnalysisEntry *entry = entryById(recentId);
            if (!entry)
                continue;
            QAction *action = recentActions->value(recentId);
            if (!action)
            {
                action = makeAction(*entry, false);
                recentActions->insert(recentId, action);
            }
            if (!recentGroup->actionGroup()->actions().contains(action))
                recentGroup->addActionItem(action);
        }
        if (recentIds->isEmpty())
            recentGroup->addActionItem(recentPlaceholder);
        else
            recentGroup->removeActionItem(recentPlaceholder);
    };
    for (const QString &recentId : *recentIds)
    {
        const ps::data_source::AnalysisEntry *entry = entryById(recentId);
        if (!entry)
            continue;
        QAction *action = makeAction(*entry, false);
        recentActions->insert(recentId, action);
        recentGroup->addActionItem(action);
    }
    if (!recentIds->isEmpty())
        recentGroup->removeActionItem(recentPlaceholder);

    auto favoriteSettings = std::make_shared<QSettings>(
        QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("NextPilot"), QStringLiteral("PlotScope"));
    auto favoriteIds = std::make_shared<QStringList>(
        favoriteSettings->value(QStringLiteral("analysis.favorites")).toStringList());
    auto favoriteActions = std::make_shared<QHash<QString, QAction *>>();
    auto *favoritePlaceholder = new QAction(QStringLiteral("尚无收藏的分析应用"), panel);
    favoritePlaceholder->setEnabled(false);
    auto *favoriteGroup = gallery->addCategoryActions(QStringLiteral("收藏"), {favoritePlaceholder});
    styleGroup(favoriteGroup);

    *favoriteCallback = [favoriteIds, favoriteActions, favoriteGroup, favoritePlaceholder,
                         entryById, makeAction, favoriteSettings](const QString &id) {
        if (id.isEmpty())
            return;
        if (favoriteIds->contains(id))
        {
            favoriteIds->removeAll(id);
        }
        else
        {
            favoriteIds->append(id);
        }
        favoriteSettings->setValue(QStringLiteral("analysis.favorites"), *favoriteIds);

        for (QAction *action : *favoriteActions)
        {
            if (favoriteGroup->actionGroup()->actions().contains(action))
                favoriteGroup->removeActionItem(action);
        }
        for (const QString &favoriteId : *favoriteIds)
        {
            const ps::data_source::AnalysisEntry *entry = entryById(favoriteId);
            if (!entry)
                continue;
            QAction *action = favoriteActions->value(favoriteId);
            if (!action)
            {
                action = makeAction(*entry, false);
                favoriteActions->insert(favoriteId, action);
            }
            if (!favoriteGroup->actionGroup()->actions().contains(action))
                favoriteGroup->addActionItem(action);
        }
        if (favoriteIds->isEmpty())
            favoriteGroup->addActionItem(favoritePlaceholder);
        else
            favoriteGroup->removeActionItem(favoritePlaceholder);
    };
    for (const QString &favoriteId : *favoriteIds)
    {
        const ps::data_source::AnalysisEntry *entry = entryById(favoriteId);
        if (!entry)
            continue;
        QAction *action = makeAction(*entry, false);
        favoriteActions->insert(favoriteId, action);
        favoriteGroup->addActionItem(action);
    }
    if (!favoriteIds->isEmpty())
        favoriteGroup->removeActionItem(favoritePlaceholder);

    auto installFavoriteMenu = [favoriteCallback, favoriteIds](SARibbonGalleryGroup *g) {
        g->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(g, &QWidget::customContextMenuRequested, g,
                         [g, favoriteCallback, favoriteIds](const QPoint &pos) {
                             const QModelIndex index = g->indexAt(pos);
                             if (!index.isValid())
                                 return;
                             auto *item = g->groupModel()->at(index.row());
                             if (!item)
                                 return;
                             auto *itemAction = item->action();
                             const QString id = itemAction ? itemAction->property("analysisId").toString() : QString();
                             if (id.isEmpty())
                                 return;
                             QMenu menu(g);
                             auto *toggle = menu.addAction(favoriteIds->contains(id)
                                                                ? QStringLiteral("取消收藏")
                                                                : QStringLiteral("收藏到分析应用库"));
                             if (menu.exec(g->viewport()->mapToGlobal(pos)) == toggle && *favoriteCallback)
                                 (*favoriteCallback)(id);
                         });
    };
    installFavoriteMenu(group);
    installFavoriteMenu(allGroup);
    installFavoriteMenu(recentGroup);
    installFavoriteMenu(favoriteGroup);

    auto refresh = [session, group, allGroup, placeholder, entries, allEntries, recentActions, favoriteActions]() {
        auto *source = session->sourceById(session->activeSourceId());
        const QString format = source ? source->fmt : QString();
        bool any = false;
        for (QAction *action : entries)
        {
            const QStringList formats = action->property("analysisFormats").toStringList();
            const bool match = !formats.isEmpty() && formats.contains(format, Qt::CaseInsensitive);
            const bool inGroup = action->property("analysisInGroup").toBool();
            if (match && !inGroup)
            {
                group->addActionItem(action);
                action->setProperty("analysisInGroup", true);
            }
            else if (!match && inGroup)
            {
                group->removeActionItem(action);
                action->setProperty("analysisInGroup", false);
            }
            any = any || match;
        }
        for (QAction *action : allEntries)
        {
            const QStringList formats = action->property("analysisFormats").toStringList();
            action->setEnabled(!formats.isEmpty() &&
                               formats.contains(format, Qt::CaseInsensitive));
        }
        for (QAction *action : *recentActions)
        {
            const QStringList formats = action->property("analysisFormats").toStringList();
            action->setEnabled(!formats.isEmpty() &&
                               formats.contains(format, Qt::CaseInsensitive));
        }
        for (QAction *action : *favoriteActions)
        {
            const QStringList formats = action->property("analysisFormats").toStringList();
            action->setEnabled(!formats.isEmpty() &&
                               formats.contains(format, Qt::CaseInsensitive));
        }
        if (any)
            group->removeActionItem(placeholder);
        else if (!group->actionGroup()->actions().contains(placeholder))
            group->addActionItem(placeholder);
    };

    auto *timer = new QTimer(panel);
    timer->setInterval(250);
    QObject::connect(timer, &QTimer::timeout, panel, refresh);
    timer->start();
    refresh();
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
                                          return mw ? dataSourceSession(mw)->createDataSourcePanel(parent) : nullptr;
                                      },
                                      [](QWidget *parent) {
                                          auto *mw = qobject_cast<MainWindow *>(parent);
                                          auto *dock = mw ? mw->findChild<QDockWidget *>(QStringLiteral("dock.src")) : nullptr;
                                          if (dock) mw->resizeDocks({dock}, {470}, Qt::Vertical);
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

    registry.addRibbon(RibbonContribution{QStringLiteral("分析"), {}, {},
        {{QStringLiteral("应用库"), {}, [](QWidget *p) { buildAnalysisGallery(p); }}},
        200});
}

} // namespace ps::components

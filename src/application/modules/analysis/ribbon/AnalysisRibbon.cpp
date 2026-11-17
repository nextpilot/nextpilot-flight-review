#include "analysis/AnalysisRibbon.h"

#include "analysis/AnalysisRegistry.h"
#include "SettingsManager.h"
#include "ThemeManager.h"
#include "CommandManager.h"
#include "ModuleContext.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonPanel.h"
#include "SARibbonPanelItem.h"

#include <QAction>
#include <QActionGroup>
#include <QHash>
#include <QMenu>
#include <QDockWidget>
#include <QIcon>
#include <QTimer>

namespace
{
void openAnalysisApp(ps::runtime::ModuleContext *ctx, const ps::analysis::AnalysisEntry &entry)
{
    if (!ctx || !entry.create)
        return;
    const QString objectName = QStringLiteral("dock.analysis.%1").arg(entry.id);
    if (auto *existing = ctx->window()->findChild<QDockWidget *>(objectName))
    {
        existing->show();
        existing->raise();
        return;
    }
    QWidget *body = entry.create(ctx->window());
    if (!body)
        return;
    auto *dock = new QDockWidget(entry.title, ctx->window());
    dock->setObjectName(objectName);
    dock->setWidget(body);
    ctx->addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->show();
}

// 分析页只负责展示应用目录；数据是否可用由领域 App 的 canOpen 判断。
void buildAnalysisGalleryImpl(QWidget *parent)
{
    auto *panel = static_cast<SARibbonPanel *>(parent);
    auto *ctx = ps::runtime::ModuleContext::fromWidget(parent ? parent->window() : nullptr);
    if (!panel || !ctx)
        return;
    QWidget *mw = ctx->window();

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

    auto entryById = [](const QString &id) -> const ps::analysis::AnalysisEntry * {
        for (const auto &entry : ps::analysis::AnalysisRegistry::instance().all())
            if (entry.id == id)
                return &entry;
        return nullptr;
    };
    auto recentCallback = std::make_shared<std::function<void(const QString &)>>();
    auto favoriteCallback = std::make_shared<std::function<void(const QString &)>>();
    auto makeAction = [&](const ps::analysis::AnalysisEntry &entry) {
        auto *action = new QAction(entry.title, panel);
        action->setToolTip(entry.description.isEmpty() ? entry.title : entry.description);
        if (!entry.icon.isEmpty())
        {
            const QString path = entry.icon.startsWith(QLatin1String(":/"))
                                     ? entry.icon
                                     : QStringLiteral(":/icons/%1.svg").arg(entry.icon);
            action->setIcon(ThemeManager::icon(path));
        }
        action->setProperty("analysisId", entry.id);
        if (entry.create)
            QObject::connect(action, &QAction::triggered, panel,
                             [ctx, entry] { openAnalysisApp(ctx, entry); });
        else
            QObject::connect(action, &QAction::triggered, panel,
                             [ctx, mw, id = entry.id] { ctx->commands().invoke(id, {mw, QString()}); });
        QObject::connect(action, &QAction::triggered, panel,
                         [recentCallback, id = entry.id] {
                             if (*recentCallback)
                                 (*recentCallback)(id);
                         });
        return action;
    };

    QVector<QAction *> allEntries;
    for (const ps::analysis::AnalysisEntry &entry : ps::analysis::AnalysisRegistry::instance().all())
        allEntries.append(makeAction(entry));
    auto *allGroup = gallery->addCategoryActions(QStringLiteral("全部应用"), allEntries);
    styleGroup(allGroup);
    gallery->setCurrentViewGroup(allGroup);

    auto recentIds = std::make_shared<QStringList>(
        SettingsManager::instance().get(ps::settings::kAnalysisRecent));
    auto recentActions = std::make_shared<QHash<QString, QAction *>>();
    auto *recentPlaceholder = new QAction(QStringLiteral("暂无最近使用的分析应用"), panel);
    recentPlaceholder->setEnabled(false);
    auto *recentGroup = gallery->addCategoryActions(QStringLiteral("最近使用"), {recentPlaceholder});
    styleGroup(recentGroup);

    *recentCallback = [recentIds, recentActions, recentGroup, recentPlaceholder, entryById, makeAction](const QString &id) mutable {
        if (id.isEmpty())
            return;
        recentIds->removeAll(id);
        recentIds->prepend(id);
        while (recentIds->size() > 6)
            recentIds->removeLast();
        SettingsManager::instance().set(ps::settings::kAnalysisRecent, *recentIds);

        for (QAction *action : *recentActions)
            if (recentGroup->actionGroup()->actions().contains(action))
                recentGroup->removeActionItem(action);
        for (const QString &recentId : *recentIds)
        {
            const ps::analysis::AnalysisEntry *entry = entryById(recentId);
            if (!entry)
                continue;
            QAction *action = recentActions->value(recentId);
            if (!action)
            {
                action = makeAction(*entry);
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
        const ps::analysis::AnalysisEntry *entry = entryById(recentId);
        if (!entry)
            continue;
        QAction *action = makeAction(*entry);
        recentActions->insert(recentId, action);
        recentGroup->addActionItem(action);
    }
    if (!recentIds->isEmpty())
        recentGroup->removeActionItem(recentPlaceholder);

    auto favoriteIds = std::make_shared<QStringList>(
        SettingsManager::instance().get(ps::settings::kAnalysisFavorites));
    auto favoriteActions = std::make_shared<QHash<QString, QAction *>>();
    auto *favoritePlaceholder = new QAction(QStringLiteral("尚无收藏的分析应用"), panel);
    favoritePlaceholder->setEnabled(false);
    auto *favoriteGroup = gallery->addCategoryActions(QStringLiteral("收藏"), {favoritePlaceholder});
    styleGroup(favoriteGroup);

    *favoriteCallback = [favoriteIds, favoriteActions, favoriteGroup, favoritePlaceholder,
                         entryById, makeAction](const QString &id) {
        if (id.isEmpty())
            return;
        if (favoriteIds->contains(id))
            favoriteIds->removeAll(id);
        else
            favoriteIds->append(id);
        SettingsManager::instance().set(ps::settings::kAnalysisFavorites, *favoriteIds);

        for (QAction *action : *favoriteActions)
            if (favoriteGroup->actionGroup()->actions().contains(action))
                favoriteGroup->removeActionItem(action);
        for (const QString &favoriteId : *favoriteIds)
        {
            const ps::analysis::AnalysisEntry *entry = entryById(favoriteId);
            if (!entry)
                continue;
            QAction *action = favoriteActions->value(favoriteId);
            if (!action)
            {
                action = makeAction(*entry);
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
        const ps::analysis::AnalysisEntry *entry = entryById(favoriteId);
        if (!entry)
            continue;
        QAction *action = makeAction(*entry);
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
    installFavoriteMenu(allGroup);
    installFavoriteMenu(recentGroup);
    installFavoriteMenu(favoriteGroup);

    auto refresh = [mw, allEntries, recentActions, favoriteActions]() {
        const auto update = [mw](QAction *action) {
            if (!action)
                return;
            const auto *entry = [&]() -> const ps::analysis::AnalysisEntry * {
                for (const auto &e : ps::analysis::AnalysisRegistry::instance().all())
                    if (e.id == action->property("analysisId").toString())
                        return &e;
                return nullptr;
            }();
            action->setEnabled(!entry || !entry->canOpen || entry->canOpen(mw));
        };
        for (QAction *action : allEntries)
            update(action);
        for (QAction *action : *recentActions)
            update(action);
        for (QAction *action : *favoriteActions)
            update(action);
    };

    auto *timer = new QTimer(panel);
    timer->setInterval(250);
    QObject::connect(timer, &QTimer::timeout, panel, refresh);
    timer->start();
    refresh();
}
} // namespace

namespace ps::analysis
{
void buildAnalysisGallery(QWidget *parent)
{
    buildAnalysisGalleryImpl(parent);
}
} // namespace ps::analysis

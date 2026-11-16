#include "RibbonManager.h"

#include "CommandManager.h"
#include "Component.h"

#include <QAction>
#include <QIcon>
#include <QMainWindow>
#include <QMenuBar>
#include <QPair>
#include <QStatusBar>
#include <QWidget>

#include <algorithm>

#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonThemeManager.h"
#include "SARibbonToolButton.h"

RibbonManager::RibbonManager(QMainWindow *window, ps::commands::CommandManager *commands,
                             const ps::components::Registry *registry, QObject *eventFilter)
    : m_window(window), m_commands(commands), m_registry(registry), m_filter(eventFilter)
{
}

QWidget *RibbonManager::create()

{
    // Ribbon 结构层交给 SARibbon（MIT，vendored 在 src/3dparty/saribbon）：
    // 页签溢出、键盘导航、面板布局都是它的事；我们只负责配色与命令。
    auto *ribbon = new SARibbonBar(m_window);
    ribbon->setRibbonStyle(SARibbonBar::RibbonStyleLooseSingleRow);
    // 有 Ribbon 就不要再留一条同名 QMenuBar，两排"文件/数据/绘图"并排是最刺眼的不协调
    if (auto *mb = m_window->menuBar())
        mb->hide();
    ribbon->setTitleVisible(false);       // 窗口标题在 titlebar，不占 ribbon 高度
    // 标题带直接压成 0：留 30px 空行只会把页签往下推，组标题也被挤掉
    ribbon->setTitleBarHeight(0, true);
    ribbon->setTabBarHeight(26);
    // 收缩：右下角 chevron + 双击页签，都能把面板收起只留页签行
    ribbon->showMinimumModeButton(true);
    ribbon->setTabDoubleClickToMinimumMode(true);
    ribbon->setPanelTitleHeight(14);
    // 分组标题：vendored 布局把标题画在面板底边内，category 高度必须把它算进去
    ribbon->setEnableShowPanelTitle(true);
    ribbon->setPanelToolButtonIconSize(QSize(18, 18), QSize(26, 26));
    ribbon->setPanelSpacing(3);
    ribbon->setCategoryHeight(78, true); // 图标 23 + 内边距 + 分组标题 14，留出基线余量

    auto applyState = [this](QAction *action, const QString &id, const QString &arg) {
        const auto *spec = m_commands->spec(id);
        if (!spec)
            return;
        ps::commands::CommandContext ctx{m_window, arg};
        action->setCheckable(spec->checkable);
        if (spec->checkable && spec->checked)
            action->setChecked(spec->checked(ctx));
        if (spec->enabled)
            action->setEnabled(spec->enabled(ctx));
    };
    auto makeAction = [this, &applyState](SARibbonPanel *panel, bool large, const QString &id,
                             const QString &text, const QString &icon, const QString &arg,
                             std::function<void()> fn, QObject *ctx) {
        const auto *spec = m_commands->spec(id);
        const QString label = !text.isEmpty() ? text : (spec ? spec->text : QString());
        const QString iconName = !icon.isEmpty() ? icon : (spec ? spec->icon : QString());
        auto *a = new QAction(label, ctx);
        a->setToolTip(label);
        a->setProperty("rbIconOnly", true);
        if (!iconName.isEmpty())
        {
            const QString path = iconName.startsWith(QLatin1String(":/"))
                                     ? iconName : QStringLiteral(":/icons/%1.svg").arg(iconName);
            a->setIcon(QIcon(path));
            a->setText(QString());
        }
        a->setProperty("rbCommandId", id);
        applyState(a, id, arg);
        QObject::connect(a, &QAction::triggered, ctx, fn);
        if (large)
            panel->addLargeAction(a);
        else
            panel->addSmallAction(a);
    };
    auto addLarge = [&](SARibbonPanel *panel, const QString &id, const QString &text,
                        const QString &icon, const QString &arg, std::function<void()> fn,
                        QObject *ctx) {
        makeAction(panel, true, id, text, icon, arg, std::move(fn), ctx);
    };
    auto addSmall = [&](SARibbonPanel *panel, const QString &id, const QString &text,
                        const QString &icon, const QString &arg, std::function<void()> fn,
                        QObject *ctx) {
        makeAction(panel, false, id, text, icon, arg, std::move(fn), ctx);
    };

    auto run = [this](const QString &id, const QString &arg = QString()) {
        return [this, id, arg] { m_commands->invoke(id, ps::commands::CommandContext{m_window, arg}); };
    };

    // 组件贡献的菜单页：一个页面可拆多个面板；页内动作只写命令 ID。
    auto renderPage = [&](const QString &title, int order = 1000) -> bool {
        bool any = false;
        SARibbonCategory *cat = nullptr;
        for (const auto &page : m_registry->ribbon())
        {
            if (page.page != title || page.order > order)
                continue;
            if (!cat)
            {
                cat = ribbon->addCategoryPage(title);
                any = true;
            }
            const QVector<ps::components::RibbonPanel> panels =
                page.panels.isEmpty() ? QVector<ps::components::RibbonPanel>{
                    ps::components::RibbonPanel{page.panel, page.items}} : page.panels;
            for (const auto &panelSpec : panels)
            {
                auto *panel = cat->addPanel(panelSpec.title);
                if (panelSpec.build)
                {
                    panelSpec.build(panel);
                    continue;
                }
                for (const auto &item : panelSpec.items)
                {
                    auto fn = [this, action = item.action, arg = item.arg] {
                        if (!m_commands->invoke(action, ps::commands::CommandContext{m_window, arg}))
                            m_registry->invoke(action, ps::components::ActionContext{m_window});
                    };
                    if (item.large)
                        addLarge(panel, item.action, item.label, item.icon, item.arg, fn, m_window);
                    else
                        addSmall(panel, item.action, item.label, item.icon, item.arg, fn, m_window);
                }
            }
        }
        return any;
    };

    // 业务页顺序由 contribution 注册顺序决定，shell 不写死页名
    QVector<QPair<int, QString>> pages;
    for (const auto &page : m_registry->ribbon())
    {
        if (page.page == QLatin1String("视图"))
            continue;
        auto it = std::find_if(pages.begin(), pages.end(),
                               [&](const QPair<int, QString> &p) { return p.second == page.page; });
        if (it == pages.end())
            pages.append({page.order, page.page});
        else if (page.order < it->first)
            it->first = page.order;
    }
    std::sort(pages.begin(), pages.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.first != b.first ? a.first < b.first : a.second < b.second;
              });
    for (const auto &page : pages)
        renderPage(page.second, 1000000);

    // 视图：按 Dock 贡献表生成，shell 不写死任何 dock id
    if (SARibbonCategory *viewCat = ribbon->addCategoryPage(QStringLiteral("视图")))
    {
        auto *panel = viewCat->addPanel(QStringLiteral("面板"));
        for (const auto &dock : m_registry->docks())
            addSmall(panel, QStringLiteral("view.toggleDock"), dock.title, dock.icon, dock.id,
                     run(QStringLiteral("view.toggleDock"), dock.id), m_window);
    }

    // 右侧芯片位：只读状态摘要，跟随实际数据源刷新
    auto *rg = ribbon->rightButtonGroup();
    auto *chipSource = new QAction(QStringLiteral("无数据"), m_window);
    chipSource->setObjectName(QStringLiteral("ps.chip.source"));
    auto *chipLod = new QAction(QStringLiteral("LOD: Rust"), m_window);
    chipLod->setObjectName(QStringLiteral("ps.chip.lod"));
    for (QAction *a : {chipSource, chipLod})
    {
        a->setEnabled(false);
        rg->addAction(a);
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
        b->installEventFilter(m_filter);
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
    SA::applyRibbonTheme(m_window, ribbon, SARibbonTheme::RibbonThemeDark);
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



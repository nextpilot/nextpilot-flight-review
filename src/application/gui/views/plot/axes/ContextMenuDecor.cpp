#include "ContextMenuDecor.h"

#include <QAction>
#include <QMenu>

#include "MenuIcons.h"

namespace nextpilot::plotscope::gui
{

namespace
{
// 右键菜单图标：文案 → MenuIcons 名字。
// 集中成一张映射表，避免每个 addAction 后面再拖一行 setIcon；改菜单文案时
// **必须同步这里**，否则图标会静默消失（不会崩，只是缺图标）。
QString menuIconName(const QString &text)
{
    if (text.contains(QStringLiteral("时间书签")))
        return QStringLiteral("bookmark");
    if (text.contains(QStringLiteral("A/B")))
        return QStringLiteral("measure-ab");
    // "固定当前读数" / "取消固定读数" 都归到图钉。
    if (text.contains(QStringLiteral("读数")))
        return QStringLiteral("pin");
    if (text.contains(QStringLiteral("放大")))
        return QStringLiteral("zoom-in");
    if (text.contains(QStringLiteral("缩小")))
        return QStringLiteral("zoom-out");
    if (text.contains(QStringLiteral("自适应")))
        return QStringLiteral("fit");
    if (text.contains(QStringLiteral("撤销视图")))
        return QStringLiteral("undo");
    if (text.contains(QStringLiteral("添加曲线")))
        return QStringLiteral("add-curve");
    if (text.contains(QStringLiteral("显示网格")))
        return QStringLiteral("grid");
    if (text.contains(QStringLiteral("显示图例")))
        return QStringLiteral("legend");
    if (text.contains(QStringLiteral("向右拆分")))
        return QStringLiteral("split-h");
    if (text.contains(QStringLiteral("向下拆分")))
        return QStringLiteral("split-v");
    if (text.contains(QStringLiteral("导出 PNG")))
        return QStringLiteral("image");
    if (text.contains(QStringLiteral("CSV")))
        return QStringLiteral("csv");
    if (text.contains(QStringLiteral("删除此画布")))
        return QStringLiteral("trash");
    if (text == QStringLiteral("属性") || text.contains(QStringLiteral("属性")))
        return QStringLiteral("info");
    if (text == QStringLiteral("曲线"))
        return QStringLiteral("curve");
    return QString();
}

void decorateMenuImpl(QMenu *menu)
{
    for (QAction *a : menu->actions())
    {
        if (a->isSeparator())
            continue;
        if (QMenu *sub = a->menu())
        {
            decorateMenu(sub);
            const QString n = menuIconName(sub->title());
            if (!n.isEmpty())
                a->setIcon(MenuIcons::icon(n));
            // 曲线子菜单里每条曲线本身也是子菜单：统一给折线图标，读作"这是曲线组"。
            if (sub->title() == QStringLiteral("曲线"))
                for (QAction *inner : sub->actions())
                    if (!inner->isSeparator() && inner->menu())
                        inner->setIcon(MenuIcons::icon(QStringLiteral("curve")));
            continue;
        }
        const QString n = menuIconName(a->text());
        if (!n.isEmpty())
            a->setIcon(MenuIcons::icon(n));
    }
}
} // namespace

void decorateMenu(QMenu *menu)
{
    decorateMenuImpl(menu);
}

} // namespace nextpilot::plotscope::gui

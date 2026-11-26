#include "IconRegistry.h"

nextpilot::plotscope::gui::IconRegistry &nextpilot::plotscope::gui::IconRegistry::instance()
{
    static IconRegistry s_instance; // NOLINT
    return s_instance;
}

void nextpilot::plotscope::gui::IconRegistry::registerIcon(const QString &id, const QIcon &icon)
{
    if (id.isEmpty() || icon.isNull())
        return; // 空 id / 空图标不入表——壳回退主题图标，注册方不会"注册成功"却无效
    if (!m_icons.contains(id))
        m_icons.insert(id, icon);
}

QIcon nextpilot::plotscope::gui::IconRegistry::icon(const QString &id) const { return m_icons.value(id); }

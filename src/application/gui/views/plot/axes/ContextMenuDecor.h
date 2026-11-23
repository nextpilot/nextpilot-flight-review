#pragma once

class QMenu;

namespace nextpilot::plotscope::gui
{
// 右键菜单统一装饰：按文案映射图标（文案改动必须同步实现里的映射表）。
void decorateMenu(QMenu *menu);
} // namespace nextpilot::plotscope::gui

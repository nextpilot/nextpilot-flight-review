#pragma once

#include <QString>

namespace nextpilot::plotscope::foundation
{

// 事件主题集中定义，避免字符串散落各处。
namespace topics
{
inline const QString kDatasetAdded = QStringLiteral("core.dataset.added");
inline const QString kDatasetRemoved = QStringLiteral("core.dataset.removed");
// 旧主题：仅曲线选择（保留兼容）。统一选中请改用 Selection.h 的 kSelectionChanged。
inline const QString kSeriesSelectionChanged = QStringLiteral("plot.selection.changed");
} // namespace topics

} // namespace nextpilot::plotscope::foundation
// B7 过渡别名：core 消亡前全仓仍以 core::topics 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
namespace topics = nextpilot::plotscope::foundation::topics;
} // namespace nextpilot::plotscope::core


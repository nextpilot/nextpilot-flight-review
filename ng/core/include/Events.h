#pragma once

#include <QString>

namespace nextpilot::plotscope::core
{

// 事件主题集中定义，避免字符串散落各处。
namespace topics
{
inline const QString kDatasetAdded = QStringLiteral("core.dataset.added");
inline const QString kDatasetRemoved = QStringLiteral("core.dataset.removed");
inline const QString kSeriesSelectionChanged = QStringLiteral("plot.selection.changed");
} // namespace topics

} // namespace nextpilot::plotscope::core

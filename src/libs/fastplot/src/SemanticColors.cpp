#include "fastplot/SemanticColors.h"

#include <QHash>

namespace nextpilot::plotscope::fastplot
{
namespace
{
// 进程级语义色表：GUI 线程单线程访问（Qt Widgets 绘制约束），不加锁。
QHash<QString, QColor> &table()
{
    static QHash<QString, QColor> t;
    return t;
}
} // namespace

void setSemanticColor(const QString &id, const QColor &color)
{
    if (id.isEmpty() || !color.isValid())
        return;
    table().insert(id, color);
}

QColor semanticColor(const QString &id, const QColor &fallback)
{
    const auto it = table().constFind(id);
    return it == table().constEnd() ? fallback : it.value();
}

bool hasSemanticColor(const QString &id)
{
    return table().contains(id);
}

void clearSemanticColors()
{
    table().clear();
}

} // namespace nextpilot::plotscope::fastplot

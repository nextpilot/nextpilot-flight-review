#pragma once

#include "IToolContribution.h"

#include <QList>
#include <QString>

namespace nextpilot::plotscope::registry
{

// 文档贡献里描述的一篇知识文档（格式规格 / 使用说明 / 领域知识）。
struct DocEntry
{
    QString id;
    QString title;
    QString location;    // 文档资源路径（md/html/pdf…）
    QString mime;        // 便于 UI 选择渲染器，可空
};

// 文档贡献：一个知识包随附的领域知识文档集合。
class IDocContribution : public IToolContribution
{
public:
    QString kind() const override { return ContributionKind::Doc; }
    virtual QList<DocEntry> entries() const = 0;
};

} // namespace nextpilot::plotscope::registry
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using registry::DocEntry;
using registry::IDocContribution;
} // namespace nextpilot::plotscope::core


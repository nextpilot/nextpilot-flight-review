#pragma once

#include "IToolContribution.h"

#include <QList>
#include <QString>

namespace nextpilot::plotscope::registry
{

// 例子贡献里描述的一个示例工程 / demo / 教程条目。
struct ExampleEntry
{
    QString id;
    QString title;
    QString location;    // 示例资源路径（脚本 / 数据 / Job JSON 等）
    QString description;
};

// 例子贡献：一个知识包随附的可运行示例，通常是"脚本 + 数据 + 文档"的组合入口。
class IExampleContribution : public IToolContribution
{
public:
    QString kind() const override { return ContributionKind::Example; }
    virtual QList<ExampleEntry> entries() const = 0;
};

} // namespace nextpilot::plotscope::registry
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using registry::ExampleEntry;
using registry::IExampleContribution;
} // namespace nextpilot::plotscope::core


#pragma once

#include "Contribution.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 飞控知识包：ULog 格式 + 飞控 UI/脚本/数据/例子/文档（六类齐全的样板）。
class AutopilotPack final : public nextpilot::plotscope::registry::IToolPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::IToolPack> build();

    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::registry::Domain domain() const override;
    const std::vector<std::unique_ptr<nextpilot::plotscope::registry::IToolContribution>> &contributions() const override;
    std::vector<std::unique_ptr<nextpilot::plotscope::registry::IToolContribution>> &contributions() override;

private:
    nextpilot::plotscope::registry::ToolPack m_pack;
};

} // namespace nextpilot::plotscope::ext

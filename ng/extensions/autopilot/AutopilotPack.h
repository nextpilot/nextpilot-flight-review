#pragma once

#include "IToolPack.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 飞控知识包：ULog 格式 + 飞控 UI/脚本/数据/例子/文档（六类齐全的样板）。
class AutopilotPack final : public nextpilot::plotscope::core::IToolPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::core::IToolPack> build();

    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::core::FormatDomain domain() const override;
    const std::vector<std::unique_ptr<nextpilot::plotscope::core::IToolContribution>> &contributions() const override;
    std::vector<std::unique_ptr<nextpilot::plotscope::core::IToolContribution>> &contributions() override;

private:
    nextpilot::plotscope::core::ToolPack m_pack;
};

} // namespace nextpilot::plotscope::ext

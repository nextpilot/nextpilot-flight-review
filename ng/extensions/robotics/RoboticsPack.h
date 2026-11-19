#pragma once

#include "IToolPack.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 机器人知识包：预留领域，后续平移 MCAP / ROSbag2 格式。
class RoboticsPack final : public nextpilot::plotscope::core::IToolPack
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

#pragma once

#include "IToolPack.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 通用知识包（CSV 等通用格式 + 通用文档）。
//
// 每个领域包一个独立文件，用 NG_REGISTER_PACK 静态自注册：
// 新增一个领域包 = 新增一个目录 + 一行 CMake，既有的包、宿主、App 全都不动。
class CommonPack final : public nextpilot::plotscope::registry::IToolPack
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

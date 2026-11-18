#pragma once

#include "IToolPack.h"

#include <QString>

#include <memory>

namespace ng::ext
{

// 汽车知识包：预留领域，后续按 ULog 公式平移 ASC/BLF/MDF4/CAN 格式时落入本包。
class AutomotivePack final : public ng::core::IToolPack
{
public:
    static QString packId();
    static std::unique_ptr<ng::core::IToolPack> build();

    QString id() const override;
    QString displayName() const override;
    ng::core::FormatDomain domain() const override;
    const std::vector<std::unique_ptr<ng::core::IToolContribution>> &contributions() const override;
    std::vector<std::unique_ptr<ng::core::IToolContribution>> &contributions() override;

private:
    ng::core::ToolPack m_pack;
};

} // namespace ng::ext

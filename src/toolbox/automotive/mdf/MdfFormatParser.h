#pragma once

#include "IMessageParser.h"

// MDF 4.x 格式解析器：身份/后缀声明在这里，字节解析由 SPI 的 mdf4 解析器执行。
class MdfFormatParser final : public nextpilot::plotscope::parser::IMessageParser
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::registry::Domain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("mdf4"); }

    QList<nextpilot::plotscope::parser::ParseOutput> outputs() const override
    {
        return {nextpilot::plotscope::parser::ParseOutput::FrameBatch};
    }
};

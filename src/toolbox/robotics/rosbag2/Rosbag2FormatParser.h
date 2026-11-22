#pragma once

#include "IMessageParser.h"

// ROS2 bag（.db3）格式解析器：身份/后缀在这里声明，字节解析由 SPI 的 rosbag2 执行。
class Rosbag2FormatParser final : public nextpilot::plotscope::parser::IMessageParser
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::registry::Domain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("rosbag2"); }

    QList<nextpilot::plotscope::parser::ParseOutput> outputs() const override
    {
        return {nextpilot::plotscope::parser::ParseOutput::FrameBatch};
    }
};

#pragma once

#include "IMessageParser.h"

// 自定义顺序协议解析器：声明式格式知识 + 数值批输出（经 SPI CustomProtocolParser 执行）。
// 无后缀，靠 format=custom-protocol 选择；templatePath 由宿主展开后经 configure 注入（解析器不读文件）。
class CustomProtocolFormatParser final : public nextpilot::plotscope::parser::IMessageParser
{
public:
    QString id() const override;
    QString displayName() const override;
    nextpilot::plotscope::registry::Domain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("custom-protocol"); }
    QString preferredTableName(const nextpilot::plotscope::intent::Intent &intent,
                               const QString &sourcePath) const override;

    QList<nextpilot::plotscope::parser::ParseOutput> outputs() const override
    {
        return {nextpilot::plotscope::parser::ParseOutput::FrameBatch};
    }

    QJsonObject configSchema() const override;
    QByteArray prepareParserOptions(const nextpilot::plotscope::intent::Intent &intent,
                                    const QByteArray &options) const override;
};

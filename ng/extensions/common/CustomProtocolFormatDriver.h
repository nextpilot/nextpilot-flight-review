#pragma once

#include "IFormatDriver.h"

// 自定义顺序协议驱动：把"按 .psproto 模板解析二进制帧"接成一条正式数据格式。
//
// 它没有固定后缀（数据文件是任意二进制），靠 format=custom-protocol 显式选择；
// 配置来自通用导入对话框的 file 字段（templatePath）。
// 驱动把 templatePath 文件读成模板文本注入 parser 的 configure——
// 读配置文件是格式知识的职责，解析器自身仍不读任何文件（铁律）。
class CustomProtocolFormatDriver final : public ng::core::IFormatDriver
{
public:
    QString id() const override;
    QString displayName() const override;
    ng::core::FormatDomain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    QString parserId() const override { return QStringLiteral("custom-protocol"); }
    QString preferredTableName(const ng::core::Intent &intent,
                               const QString &sourcePath) const override;

    ng::core::Result<ng::core::IntentResult> load(
        const ng::core::Intent &intent, ng::core::DataRepository &repo) override;

    QJsonObject importSchema() const override;
    QByteArray prepareParserOptions(const ng::core::Intent &intent,
                                    const QByteArray &options) const override;
};

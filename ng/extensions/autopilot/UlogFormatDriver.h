#pragma once

#include "IFormatDriver.h"

// ULog 格式驱动：把工具箱接口接到流式 ULog 解析器上。
// 解析算法复用 UlogStreamParser（源自旧 extension 的协议知识），
// 本类只负责"身份声明 + 缓存/入库"这套所有格式驱动共用的外壳。
class UlogFormatDriver final : public ng::core::IFormatDriver
{
public:
    QString id() const override;
    QString displayName() const override;
    ng::core::FormatDomain domain() const override;
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;

    // 本格式由 ULog SPI 解析器负责——宿主据此走统一摄取管道
    // （读字节 -> 扇出 -> 落库），而不是让驱动自己读文件。
    // 于是 ULog 的解析实现只有**一份**（SPI 那份）。
    QString parserId() const override { return QStringLiteral("ulog"); }

    ng::core::Result<ng::core::IntentResult> load(
        const ng::core::Intent &intent, ng::core::DataRepository &repo) override;
};

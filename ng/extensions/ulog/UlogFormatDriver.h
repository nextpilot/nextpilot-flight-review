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
    QStringList suffixes() const override;
    bool acceptsFormat(const QString &format) const override;
    ng::core::Result<ng::core::IntentResult> load(
        const ng::core::Intent &intent, ng::core::DataRepository &repo) override;
};

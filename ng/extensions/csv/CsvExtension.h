#pragma once

#include "IModule.h"

// CSV 知识包：唯一职责是把 .csv 文件解析成 Dataset。
// 它不认识画布、不认识数据源 UI，只注册一个能处理 "data.load.csv" 的能力。
class CsvExtension final : public ng::host::IModule
{
    Q_OBJECT
public:
    ng::host::ModuleInfo info() const override;
    void initialize(ng::host::ModuleContext &ctx) override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent 给 this，随模块析构自动回收
};

Q_DECLARE_INTERFACE(CsvExtension, "nextpilot.ng.CsvExtension/1.0")

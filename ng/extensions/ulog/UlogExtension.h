#pragma once

#include "IModule.h"

// ULog 知识包：唯一职责是把 .ulg 文件解析成 Dataset。
// 协议解析复用旧 extension 的 ULog 算法，产出端改为流式灌入 Rust kernel。
// 它不认识画布、不认识 UI，只注册一个能处理 "data.load" (format=ulog) 的能力。
class UlogExtension final : public ng::host::IModule
{
    Q_OBJECT
public:
    ng::host::ModuleInfo info() const override;
    void initialize(ng::host::ModuleContext &ctx) override;

private:
    class Handler;
    Handler *m_handler = nullptr; // parent 给 this，随模块析构自动回收
};

Q_DECLARE_INTERFACE(UlogExtension, "nextpilot.ng.UlogExtension/1.0")

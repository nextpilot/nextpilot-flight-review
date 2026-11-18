#pragma once

#include "ExtensionRegistry.h"
#include "Intent.h"

namespace ng::core { class DataRepository; }

// 数据加载意图的统一入口（宿主的系统级能力，不归属任何可卸载模块）。
//
// 它不认识任何具体格式：拿到 data.load 意图后，
//   1) 有 format 参数 -> registry.driverForFormat 精确认领；
//   2) 否则 -> registry.driverForPath 按文件后缀推断；
//   3) 找到驱动 -> 委托 driver.load()（解析/缓存/入库全在驱动内）；
//   4) 找不到 -> eformat。
//
// 这样格式扩展之间彻底解耦：新增格式只往注册表加驱动；卸载=从注册表撤销，
// data 器官与其他器官都不用改。
class DataLoadHandler final : public ng::core::IIntentHandler
{
public:
    DataLoadHandler(ng::core::ExtensionRegistry &registry,
                    ng::core::DataRepository &repo)
        : m_registry(registry), m_repo(repo) {}

    bool canHandle(const ng::core::Intent &intent) const override;
    ng::core::Result<ng::core::IntentResult> execute(
        const ng::core::Intent &intent) override;

private:
    ng::core::ExtensionRegistry &m_registry;
    ng::core::DataRepository &m_repo;
};

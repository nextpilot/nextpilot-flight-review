#pragma once

#include "ExtensionRegistry.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 飞控知识包：ULog 格式 + 飞控 UI/脚本/数据/例子/文档（六类齐全的样板）。
// 包只是一个 build() 工厂：填一份纯数据清单（Pack），宿主 mount() 挂进工具箱。
class AutopilotPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext

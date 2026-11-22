#pragma once

#include "ToolboxHub.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// 通用扩展包：CSV / 自定义协议格式 + 通用文档。
// 包只是一个 build() 工厂：填一份纯数据清单（Pack），宿主 mount() 挂进工具箱。
class CommonPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext

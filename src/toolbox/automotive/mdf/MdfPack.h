#pragma once

#include "ToolboxHub.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// MDF 扩展包：ASAM MDF 4.x 车载测量文件。
// 只装一个格式解析器；UI/脚本/数据/例子暂不铺（不造没有消费方的入口）。
class MdfPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext

#pragma once

#include "ToolboxHub.h"

#include <QString>

#include <memory>

namespace nextpilot::plotscope::ext
{

// ArduPilot DataFlash 扩展包：自驾域的 .bin 自描述日志。
class ApbinPack
{
public:
    static QString packId();
    static std::unique_ptr<nextpilot::plotscope::registry::Pack> build();
};

} // namespace nextpilot::plotscope::ext

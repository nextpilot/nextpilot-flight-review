#pragma once

#include "Intent.h"

namespace nextpilot::plotscope::core
{

// 意图执行器抽象。core 只定义契约，具体实现在 host。
// 脚本门面依赖这个抽象而非任何具体宿主——这是 headless 与 GUI 共用的接口。
class IIntentExecutor
{
public:
    virtual ~IIntentExecutor() = default;
    virtual Result<IntentResult> execute(const Intent &intent) = 0;
};

} // namespace nextpilot::plotscope::core

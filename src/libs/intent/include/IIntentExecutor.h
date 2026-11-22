#pragma once

#include "Intent.h"

namespace nextpilot::plotscope::intent
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::foundation::fail;

// 意图执行器抽象，headless 与 GUI 共用。
class IIntentExecutor
{
public:
    virtual ~IIntentExecutor() = default;
    virtual Result<IntentResult> execute(const Intent &intent) = 0;
};

} // namespace nextpilot::plotscope::intent

#pragma once

#include "protocol/DigestEngine.h"

#include <cstdint>
#include <string>

namespace nextpilot::plotscope::protocol
{

/// Lua 5.4 后端是否编进本库。
bool scriptEngineAvailable();

/// 执行摘要脚本。脚本形如 `return ctx.data:len()` 之外的任意表达式代码，
/// 被包成 function(ctx) ... end，返回值必须是数字。
/// 超配额时 timeout=true 且 err 说明原因。
bool runDigestScript(const std::string &code, const ScriptDigestInput &in, uint64_t &out,
                     std::string &err, bool &timeout);

} // namespace nextpilot::plotscope::protocol

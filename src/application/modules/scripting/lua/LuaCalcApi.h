#pragma once

struct lua_State;

namespace ps::scripting
{

// 计算库：ps.stats/fft/psd/filter/decimate 等，全部转发到 Rust 内核。
// 数据来源可以是通道句柄（零拷贝读内核内存）或 Lua 数字数组（拷贝一次）。
void installCalcApi(lua_State *L);

} // namespace ps::scripting

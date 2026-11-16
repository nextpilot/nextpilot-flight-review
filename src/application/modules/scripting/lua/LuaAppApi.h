#pragma once

struct lua_State;

namespace ps::scripting
{

// 往 ps.app 下装数据面 API（S1 只读）。
// 每次 run 的新 state 调用一次；hostService 未注册时调用即报 eneedhost。
void installAppApi(lua_State *L);

} // namespace ps::scripting

// Lua 5.4 脚本后端（摘要校验用）。设计约束：
//   - 不暴露 io/os/package（只留 base/string/table/math/bit32），脚本不能碰文件与进程；
//   - 指令配额（count hook）：超过就让 pcall 失败，错误码归 ScriptTimeout；
//   - 每次调用独立 lua_State，无跨调用状态，天然线程安全。

#include "protocol/ScriptEngine.h"

#include "protocol/TypedVec.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <map>
#include <string>

namespace nextpilot::plotscope::protocol
{

namespace
{

constexpr int kInstructionQuota = 400000;

void quotaHook(lua_State *L, lua_Debug *)
{
    luaL_error(L, "script budget exceeded");
}

void pushTypedValue(lua_State *L, const TypedValue &v)
{
    if (v.type == ValueType::Text)
        lua_pushstring(L, v.s.c_str());
    else
        lua_pushnumber(L, v.asDouble());
}

int l_ctxIndex(lua_State *L)
{
    // ctx.fields["name"] → 数组段返回表，标量段返回值
    lua_getfield(L, 1, "fields");
    if (!lua_istable(L, -1))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
}

} // namespace

bool scriptEngineAvailable()
{
    return true;
}

bool runDigestScript(const std::string &code, const ScriptDigestInput &in, uint64_t &out,
                     std::string &err, bool &timeout)
{
    timeout = false;
    lua_State *L = luaL_newstate();
    if (!L)
    {
        err = "luaL_newstate 失败";
        return false;
    }
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, "string", luaopen_string, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    luaL_requiref(L, "math", luaopen_math, 1);
    lua_pop(L, 4);
    lua_sethook(L, quotaHook, LUA_MASKCOUNT, kInstructionQuota);

    // 脚本本身就是一个 chunk，全局 ctx 可用：`return <expr>` 即摘要值。
    if (luaL_loadstring(L, code.c_str()) != LUA_OK)
    {
        err = std::string("脚本语法错误：") + lua_tostring(L, -1);
        lua_close(L);
        return false;
    }

    lua_newtable(L); // ctx
    lua_pushlstring(L, reinterpret_cast<const char *>(in.data), in.len);
    lua_setfield(L, -2, "data");
    lua_pushinteger(L, static_cast<lua_Integer>(in.len));
    lua_setfield(L, -2, "len");

    lua_newtable(L); // ctx.fields
    if (in.fields)
    {
        const auto *cols = static_cast<const std::map<std::string, TypedVec> *>(in.fields);
        for (const auto &kv : *cols)
        {
            const TypedVec &col = kv.second;
            if (col.size() == 0)
                continue;
            if (col.size() == 1)
            {
                pushTypedValue(L, col.at(0));
                lua_setfield(L, -2, kv.first.c_str());
            }
            else
            {
                lua_newtable(L);
                for (std::size_t i = 0; i < col.size(); ++i)
                {
                    pushTypedValue(L, col.at(i));
                    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                }
                lua_setfield(L, -2, kv.first.c_str());
            }
        }
    }
    lua_setfield(L, -2, "fields");
    lua_setglobal(L, "ctx");

    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
        const std::string m = lua_tostring(L, -1) ? lua_tostring(L, -1) : "脚本执行失败";
        timeout = m.find("budget exceeded") != std::string::npos;
        err = timeout ? "脚本超时（指令配额耗尽）" : m;
        lua_close(L);
        return false;
    }
    if (!lua_isnumber(L, -1))
    {
        err = "脚本必须返回数字（摘要值）";
        lua_close(L);
        return false;
    }
    out = static_cast<uint64_t>(lua_tonumber(L, -1));
    lua_close(L);
    return true;
}

} // namespace nextpilot::plotscope::protocol

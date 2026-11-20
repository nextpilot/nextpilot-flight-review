// Lua 5.4 脚本后端（摘要校验用）。设计约束：
//   - 不暴露 io/os/package（只留 base/string/table/math），脚本不能碰文件与进程；
//   - 指令配额（count hook）：超过就让 pcall 失败，错误码归 ScriptTimeout；
//   - 内存配额：自定义分配器记账，超预算拒绝分配，归类为内存超限（非指令超时）；
//   - 字节码预编译：源码只 loadstring/dump 一次并缓存，每帧用独立干净 state 从字节码
//     loadbuffer，跳过源码词法语法解析；独立 state 保证可重入、无线程间残留。

#include "protocol/ScriptEngine.h"

#include "protocol/TypedVec.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

namespace nextpilot::plotscope::protocol
{

namespace
{

constexpr int kInstructionQuota = 400000;
constexpr std::size_t kMemoryQuota = 1u << 20; // 1 MiB：摘要脚本只处理一帧字段/字节

// 内存预算：分配器按块记账，超预算返回 NULL 触发 Lua 内存错误。
struct MemBudget
{
    std::size_t quota = kMemoryQuota;
    std::size_t used = 0;
    bool exceeded = false;
};

void *budgetAlloc(void *ud, void *ptr, std::size_t osize, std::size_t nsize)
{
    auto *b = static_cast<MemBudget *>(ud);
    if (nsize == 0)
    {
        std::free(ptr);
        if (b->used >= osize)
            b->used -= osize;
        else
            b->used = 0;
        return nullptr;
    }
    if (ptr == nullptr)
    {
        if (b->used + nsize > b->quota)
        {
            b->exceeded = true;
            return nullptr;
        }
        void *p = std::malloc(nsize);
        if (p)
            b->used += nsize;
        return p;
    }
    if (b->used - osize + nsize > b->quota)
    {
        b->exceeded = true;
        return nullptr;
    }
    void *p = std::realloc(ptr, nsize);
    if (p)
        b->used = b->used - osize + nsize;
    // realloc 失败：原块仍在，记账保持不变，由 Lua 走失败路径。
    return p;
}

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

// 把输入包成 ctx 表压栈（data / len / fields）。
void pushCtx(lua_State *L, const ScriptDigestInput &in)
{
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
}

struct DumpSink
{
    std::string data;
};

int dumpWriter(lua_State *, const void *p, std::size_t sz, void *ud)
{
    auto *s = static_cast<DumpSink *>(ud);
    const auto *b = static_cast<const char *>(p);
    s->data.append(b, sz);
    return 0; // 0 = 写入成功
}

// 源码 → 预编译字节码缓存。键为源码文本；值为同 Lua 构建下可 loadbuffer 的字节码。
std::mutex g_scriptMtx;
std::unordered_map<std::string, std::string> g_bytecode;

// 编译一次源码并 dump 成字节码。失败时 err 给出语法/内存原因，不缓存。
bool compileOnce(const std::string &code, std::string &bytecode, std::string &err)
{
    MemBudget budget;
    lua_State *L = lua_newstate(budgetAlloc, &budget);
    if (!L)
    {
        err = "lua 状态创建失败（内存预算不足）";
        return false;
    }
    if (luaL_loadstring(L, code.c_str()) != LUA_OK)
    {
        err = std::string("脚本语法错误：") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_close(L);
        return false;
    }
    DumpSink sink;
    if (lua_dump(L, dumpWriter, &sink, 0) != LUA_OK)
    {
        err = "脚本字节码编译失败";
        lua_close(L);
        return false;
    }
    lua_close(L);
    bytecode = std::move(sink.data);
    return true;
}

// 取（必要时编译并缓存）源码对应的字节码。
bool getBytecode(const std::string &code, std::string &bytecode, std::string &err)
{
    {
        std::lock_guard<std::mutex> lock(g_scriptMtx);
        const auto it = g_bytecode.find(code);
        if (it != g_bytecode.end())
        {
            bytecode = it->second;
            return true;
        }
    }
    std::string bc;
    if (!compileOnce(code, bc, err))
        return false;
    {
        std::lock_guard<std::mutex> lock(g_scriptMtx);
        g_bytecode.emplace(code, bc);
        bytecode = std::move(bc);
    }
    return true;
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

    std::string bytecode;
    if (!getBytecode(code, bytecode, err))
        return false;

    MemBudget budget;
    lua_State *L = lua_newstate(budgetAlloc, &budget);
    if (!L)
    {
        err = "lua 状态创建失败（内存预算不足）";
        return false;
    }
    luaL_requiref(L, "_G", luaopen_base, 1);
    luaL_requiref(L, "string", luaopen_string, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    luaL_requiref(L, "math", luaopen_math, 1);
    lua_pop(L, 4);
    lua_sethook(L, quotaHook, LUA_MASKCOUNT, kInstructionQuota);

    // 从缓存字节码加载（跳过源码解析）；chunk 名仅用于报错定位。
    if (luaL_loadbuffer(L, bytecode.data(), bytecode.size(), "digest") != LUA_OK)
    {
        err = std::string("脚本加载失败：") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_close(L);
        return false;
    }

    pushCtx(L, in);

    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
        const std::string m = lua_tostring(L, -1) ? lua_tostring(L, -1) : "脚本执行失败";
        if (budget.exceeded)
        {
            timeout = false;
            err = "脚本内存超限（分配预算耗尽）";
        }
        else
        {
            timeout = m.find("budget exceeded") != std::string::npos;
            err = timeout ? "脚本超时（指令配额耗尽）" : m;
        }
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

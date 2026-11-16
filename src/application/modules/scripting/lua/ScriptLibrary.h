#pragma once

#include <QString>

struct lua_State;

namespace ps::scripting
{

// 受限 require：只能从 <root>/<name>.lua 加载，拒绝绝对路径、盘符与目录穿越。
//
// 不复用标准 require，是因为沙箱把 `package` 表摘掉了，标准实现在
// package.searchers 上取路径，摘掉后必然失败；自己实现还能顺带锁死路径。
class ScriptLibrary
{
public:
    // 未设置时回落到 <AppDataLocation>/scripts/lib
    static void setRoot(const QString &root);
    static QString root();

    // 名字 → 绝对路径。非法名字（空、绝对路径、含 ".."、"\\"、":"）直接判错。
    static bool resolve(const QString &name, QString *path, QString *error);

    // 把受限 require 装进 _G，覆盖标准 require
    static void install(lua_State *L);

    // 清缓存，下次 require 重新读盘（控制台 reload 元命令用）
    static void invalidate(lua_State *L, const QString &name);
    static void invalidateAll(lua_State *L);
};

} // namespace ps::scripting

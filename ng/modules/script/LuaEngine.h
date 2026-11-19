#pragma once

#include "Result.h"

#include <QString>

struct lua_State;

namespace nextpilot::plotscope::core
{
class ScriptFacade;
}

namespace nextpilot::plotscope::script
{

// Lua 引擎：封装 Lua VM，把 ScriptFacade 注入为全局门面 ps。
// 脚本只能看到 ps.data.load / ps.plot.render 等，碰不到 QWidget 或模块内部对象。
class LuaEngine
{
public:
    explicit LuaEngine(nextpilot::plotscope::core::ScriptFacade &facade);
    ~LuaEngine();

    LuaEngine(const LuaEngine &) = delete;
    LuaEngine &operator=(const LuaEngine &) = delete;

    // 执行一段脚本源码；失败时 Result 带 Lua 错误信息。
    nextpilot::plotscope::core::Result<void> runString(const QByteArray &source, const QString &chunkName = QStringLiteral("=script"));

    // 执行脚本文件。
    nextpilot::plotscope::core::Result<void> runFile(const QString &path);

private:
    lua_State *m_lua = nullptr;
};

} // namespace nextpilot::plotscope::script

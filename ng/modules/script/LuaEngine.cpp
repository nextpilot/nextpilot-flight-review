#include "LuaEngine.h"

#include "ScriptFacade.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <QFile>
#include <QVariant>
#include <QVariantMap>

#include <cstdio>

using namespace ng::core;

namespace ng::script
{

// 门面指针在 registry 中的 key。脚本拿不到它，只有 C 回调能取。
static const char *kFacadeKey = "ng.script.facade";

static ScriptFacade *facadeOf(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kFacadeKey);
    auto *f = static_cast<ScriptFacade *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return f;
}

// QVariant -> Lua 值。PlotSpec 是 QVariantMap，递归展开成 table。
static void pushVariant(lua_State *L, const QVariant &v)
{
    if (!v.isValid() || v.isNull())
    {
        lua_pushnil(L);
        return;
    }
    switch (v.typeId())
    {
    case QMetaType::Bool:
        lua_pushboolean(L, v.toBool() ? 1 : 0);
        break;
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        lua_pushnumber(L, v.toDouble());
        break;
    case QMetaType::QString:
        lua_pushstring(L, v.toString().toUtf8().constData());
        break;
    case QMetaType::QVariantMap:
    {
        const QVariantMap map = v.toMap();
        lua_createtable(L, 0, static_cast<int>(map.size()));
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        {
            lua_pushstring(L, it.key().toUtf8().constData());
            pushVariant(L, it.value());
            lua_rawset(L, -3);
        }
        break;
    }
    case QMetaType::QStringList:
    {
        const QStringList list = v.toStringList();
        lua_createtable(L, static_cast<int>(list.size()), 0);
        for (int i = 0; i < list.size(); ++i)
        {
            pushVariant(L, list.at(i));
            lua_rawseti(L, -2, i + 1); // Lua 数组从 1 开始
        }
        break;
    }
    default:
        lua_pushstring(L, v.toString().toUtf8().constData());
        break;
    }
}

// ps.version()
static int l_version(lua_State *L)
{
    lua_pushliteral(L, "PlotScope NG script 0.1");
    return 1;
}

// ps.log(msg)
static int l_log(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    fprintf(stdout, "[lua] %s\n", msg);
    return 0;
}

// ps.data.load(path, format?) -> dataset id
static int l_data_load(lua_State *L)
{
    ScriptFacade *f = facadeOf(L);
    if (!f)
        return luaL_error(L, "脚本门面不可用");
    const char *path = luaL_checkstring(L, 1);
    const char *fmt = luaL_optstring(L, 2, "csv");
    std::optional<DatasetId> stableId;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3))
        stableId = static_cast<DatasetId>(luaL_checkinteger(L, 3));
    Result<DatasetId> r = f->loadDataset(QString::fromUtf8(path), QString::fromUtf8(fmt), stableId);
    if (!r)
        return luaL_error(L, "data.load 失败[%s]: %s",
                          qPrintable(r.error().code), qPrintable(r.error().message));
    lua_pushinteger(L, r.value());
    return 1;
}

// ps.plot.render(dataset, channel?) -> PlotSpec table
static int l_plot_render(lua_State *L)
{
    ScriptFacade *f = facadeOf(L);
    if (!f)
        return luaL_error(L, "脚本门面不可用");
    const lua_Integer id = luaL_checkinteger(L, 1);
    const char *channel = luaL_optstring(L, 2, "");
    Result<QVariantMap> r = f->renderPlot(static_cast<DatasetId>(id), QString::fromUtf8(channel));
    if (!r)
        return luaL_error(L, "plot.render 失败[%s]: %s",
                          qPrintable(r.error().code), qPrintable(r.error().message));
    pushVariant(L, r.value());
    return 1;
}

// 组装全局 ps 表。
static void installFacade(lua_State *L)
{
    lua_createtable(L, 0, 4); // ps

    lua_pushcfunction(L, l_version);
    lua_setfield(L, -2, "version");
    lua_pushcfunction(L, l_log);
    lua_setfield(L, -2, "log");

    // ps.data
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, l_data_load);
    lua_setfield(L, -2, "load");
    lua_setfield(L, -2, "data");

    // ps.plot
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, l_plot_render);
    lua_setfield(L, -2, "render");
    lua_setfield(L, -2, "plot");

    lua_setglobal(L, "ps");
}

LuaEngine::LuaEngine(ScriptFacade &facade)
{
    m_lua = luaL_newstate();
    if (!m_lua)
        return;
    luaL_openlibs(m_lua);

    // 门面存 registry，脚本脚本本身拿不到指针。
    lua_pushlightuserdata(m_lua, &facade);
    lua_setfield(m_lua, LUA_REGISTRYINDEX, kFacadeKey);

    installFacade(m_lua);
}

LuaEngine::~LuaEngine()
{
    if (m_lua)
        lua_close(m_lua);
}

Result<void> LuaEngine::runString(const QByteArray &source, const QString &chunkName)
{
    if (!m_lua)
        return fail(QStringLiteral("enolua"), QStringLiteral("Lua 状态创建失败"));
    const int status = luaL_loadbuffer(m_lua, source.constData(), source.size(),
                                       chunkName.toUtf8().constData());
    if (status != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本编译失败");
        lua_pop(m_lua, 1);
        return fail(QStringLiteral("esyntax"), msg);
    }
    const int callStatus = lua_pcall(m_lua, 0, 0, 0);
    if (callStatus != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本运行失败");
        lua_pop(m_lua, 1);
        return fail(QStringLiteral("eruntime"), msg);
    }
    return Result<void>();
}

Result<void> LuaEngine::runFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("eio"), QStringLiteral("无法读取脚本: %1").arg(path));
    return runString(f.readAll(), QStringLiteral("@%1").arg(path));
}

} // namespace ng::script

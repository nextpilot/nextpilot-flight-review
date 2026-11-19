#include "LuaEngine.h"

#include "ScriptFacade.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>
#include <QVariantMap>

#include <cstdio>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::script
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

// ===== LuaTransform：单点 / 批量数值变换 =====

// QJsonValue -> Lua 值（递归）。用于把 ctx.params 传给脚本。
static void pushJson(lua_State *L, const QJsonValue &v)
{
    if (v.isUndefined() || v.isNull())
        lua_pushnil(L);
    else if (v.isBool())
        lua_pushboolean(L, v.toBool() ? 1 : 0);
    else if (v.isDouble())
        lua_pushnumber(L, v.toDouble());
    else if (v.isString())
        lua_pushstring(L, v.toString().toUtf8().constData());
    else if (v.isArray())
    {
        const QJsonArray arr = v.toArray();
        lua_createtable(L, arr.size(), 0);
        for (int i = 0; i < arr.size(); ++i)
        {
            pushJson(L, arr.at(i));
            lua_rawseti(L, -2, i + 1);
        }
    }
    else if (v.isObject())
    {
        const QJsonObject obj = v.toObject();
        lua_createtable(L, 0, obj.size());
        for (auto it = obj.begin(); it != obj.end(); ++it)
        {
            lua_pushstring(L, it.key().toUtf8().constData());
            pushJson(L, it.value());
            lua_rawset(L, -3);
        }
    }
    else
        lua_pushnil(L);
}

LuaTransform::LuaTransform(const QByteArray &source, const QJsonObject &params, const QString &name)
    : m_params(params)
{
    m_lua = luaL_newstate();
    if (!m_lua)
    {
        m_error = QStringLiteral("Lua 状态创建失败");
        return;
    }
    luaL_openlibs(m_lua);

    // 加载用户源码。约定 transform(x, ctx) 函数；未提供则把源码当表达式。
    // 先尝试原样加载（源码里定义 transform 函数）。
    int st = luaL_loadbuffer(m_lua, source.constData(), source.size(), name.toUtf8().constData());
    if (st == LUA_OK)
        st = lua_pcall(m_lua, 0, 0, 0);
    if (st != LUA_OK)
    {
        const char *e = lua_tostring(m_lua, -1);
        m_error = e ? QString::fromUtf8(e) : QStringLiteral("Lua 编译失败");
        lua_pop(m_lua, 1);
        return;
    }

    // 判断是否有全局 transform 函数。
    lua_getglobal(m_lua, "transform");
    const bool hasFn = lua_isfunction(m_lua, -1);
    lua_pop(m_lua, 1);

    if (!hasFn)
    {
        // 把源码当表达式：构造 `transform = function(x, ctx) ... return (SRC) end`。
        const QByteArray wrapped = "transform = function(x, ctx)\n  return (" + source + ")\nend";
        st = luaL_loadbuffer(m_lua, wrapped.constData(), wrapped.size(), name.toUtf8().constData());
        if (st == LUA_OK)
            st = lua_pcall(m_lua, 0, 0, 0);
        if (st != LUA_OK)
        {
            const char *e = lua_tostring(m_lua, -1);
            m_error = e ? QString::fromUtf8(e) : QStringLiteral("Lua 表达式编译失败");
            lua_pop(m_lua, 1);
            return;
        }
    }
}

LuaTransform::~LuaTransform()
{
    if (m_lua)
        lua_close(m_lua);
}

bool LuaTransform::eval(double x, double t, int index, double *out, QString *err) const
{
    if (!m_lua || !out)
    {
        if (err) *err = m_error.isEmpty() ? QStringLiteral("Lua 变换不可用") : m_error;
        return false;
    }
    // 取 transform 函数。
    lua_getglobal(m_lua, "transform");
    if (!lua_isfunction(m_lua, -1))
    {
        lua_pop(m_lua, 1);
        if (err) *err = QStringLiteral("未定义 transform 函数");
        return false;
    }
    // 参数 1: x
    lua_pushnumber(m_lua, x);
    // 参数 2: ctx = { t=..., index=..., params=... }
    lua_createtable(m_lua, 0, 3);
    lua_pushnumber(m_lua, t);
    lua_setfield(m_lua, -2, "t");
    lua_pushinteger(m_lua, index);
    lua_setfield(m_lua, -2, "index");
    pushJson(m_lua, QJsonValue(m_params));
    lua_setfield(m_lua, -2, "params");

    const int st = lua_pcall(m_lua, 2, 1, 0);
    if (st != LUA_OK)
    {
        const char *e = lua_tostring(m_lua, -1);
        if (err) *err = e ? QString::fromUtf8(e) : QStringLiteral("Lua 变换运行失败");
        lua_pop(m_lua, 1);
        return false;
    }
    if (!lua_isnumber(m_lua, -1))
    {
        lua_pop(m_lua, 1);
        if (err) *err = QStringLiteral("transform 未返回数值");
        return false;
    }
    *out = lua_tonumber(m_lua, -1);
    lua_pop(m_lua, 1);
    return true;
}

bool LuaTransform::evalSeries(const QVector<double> &t, const QVector<double> &v,
                              QVector<double> *out, QString *err) const
{
    if (!out)
        return false;
    out->resize(qMin(t.size(), v.size()));
    for (int i = 0; i < out->size(); ++i)
    {
        double y = 0.0;
        if (!eval(v.at(i), t.value(i), i, &y, err))
            return false;
        (*out)[i] = y;
    }
    return true;
}

} // namespace nextpilot::plotscope::script

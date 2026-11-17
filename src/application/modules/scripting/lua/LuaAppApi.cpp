#include "LuaAppApi.h"

#ifdef PS_HAVE_LUA


#include "HostDataService.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace ps::scripting
{
// 消费的是宿主数据访问契约，这里只是少写前缀
using host::ChannelInfo;
using host::JobHandle;
using host::LiveStatus;
using host::Result;
using host::SourceInfo;

namespace
{
const char *kSourceMt = "ps.app.source";
const char *kChannelMt = "ps.app.channel";
const char *kJobMt = "ps.app.job";

host::HostDataService *host(lua_State *L)
{
    host::HostDataService *svc = host::hostService();
    if (!svc)
        luaL_error(L, "eneedhost: 脚本数据面未注册");
    return svc;
}

// 通用：把 code/message 做成 nil, err 两个返回值
int pushFailure(lua_State *L, const Result &r)
{
    lua_pushnil(L);
    lua_pushfstring(L, "%s: %s", r.code.toUtf8().constData(),
                    r.message.toUtf8().constData());
    return 2;
}

void pushSourceRef(lua_State *L, const SourceInfo &s)
{
    lua_newtable(L);
    lua_pushinteger(L, s.id);
    lua_setfield(L, -2, "__id");
    lua_pushinteger(L, s.id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, s.name.toUtf8().constData());
    lua_setfield(L, -2, "name");
    lua_pushstring(L, s.alias.toUtf8().constData());
    lua_setfield(L, -2, "alias");
    lua_pushstring(L, s.path.toUtf8().constData());
    lua_setfield(L, -2, "path");
    lua_pushstring(L, s.format.toUtf8().constData());
    lua_setfield(L, -2, "format");
    lua_pushboolean(L, s.live);
    lua_setfield(L, -2, "live");
    lua_pushboolean(L, s.paused);
    lua_setfield(L, -2, "paused");
    luaL_getmetatable(L, kSourceMt);
    lua_setmetatable(L, -2);
}

void pushChannelRef(lua_State *L, int sourceId, const ChannelInfo &c)
{
    lua_newtable(L);
    lua_pushinteger(L, sourceId);
    lua_setfield(L, -2, "__src");
    lua_pushstring(L, c.name.toUtf8().constData());
    lua_setfield(L, -2, "name");
    lua_pushstring(L, c.unit.toUtf8().constData());
    lua_setfield(L, -2, "unit");
    lua_pushinteger(L, static_cast<lua_Integer>(c.count));
    lua_setfield(L, -2, "count");
    lua_pushnumber(L, c.t0);
    lua_setfield(L, -2, "t0");
    lua_pushnumber(L, c.t1);
    lua_setfield(L, -2, "t1");
    lua_pushnumber(L, c.vMin);
    lua_setfield(L, -2, "vMin");
    lua_pushnumber(L, c.vMax);
    lua_setfield(L, -2, "vMax");
    luaL_getmetatable(L, kChannelMt);
    lua_setmetatable(L, -2);
}

void pushJob(lua_State *L, const JobHandle &h)
{
    lua_newtable(L);
    lua_pushinteger(L, h.id);
    lua_setfield(L, -2, "id");
    lua_pushboolean(L, h.valid());
    lua_setfield(L, -2, "ok");
    luaL_getmetatable(L, kJobMt);
    lua_setmetatable(L, -2);
}

// ---- ps.app.sources ----
int l_sources(lua_State *L)
{
    const QVector<SourceInfo> list = host(L)->sources();
    lua_newtable(L);
    for (int i = 0; i < list.size(); ++i)
    {
        pushSourceRef(L, list[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ---- ps.app.source(name|id) ----
int l_source(lua_State *L)
{
    host::HostDataService *svc = host(L);
    const QVector<SourceInfo> list = svc->sources();
    SourceInfo hit;
    bool found = false;

    if (lua_isnumber(L, 1))
    {
        const int id = static_cast<int>(lua_tointeger(L, 1));
        for (const SourceInfo &s : list)
            if (s.id == id)
            {
                hit = s;
                found = true;
                break;
            }
    }
    else
    {
        const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
        for (const SourceInfo &s : list)
            if (s.name == name || s.alias == name)
            {
                hit = s;
                found = true;
                break;
            }
    }
    if (!found)
    {
        lua_pushnil(L);
        lua_pushfstring(L, "enoent: 没有匹配的数据源");
        return 2;
    }
    pushSourceRef(L, hit);
    return 1;
}

// ---- ps.app.currentSource ----
int l_currentSource(lua_State *L)
{
    const QVector<SourceInfo> list = host(L)->sources();
    if (list.isEmpty())
    {
        lua_pushnil(L);
        lua_pushfstring(L, "enoent: 当前没有数据源");
        return 2;
    }
    pushSourceRef(L, list.last());
    return 1;
}

int srcId(lua_State *L)
{
    if (lua_isnumber(L, 1))
        return static_cast<int>(lua_tointeger(L, 1));
    lua_getfield(L, 1, "__id");
    const int id = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return id;
}

// ---- src:channels() ----
int m_channels(lua_State *L)
{
    const int id = srcId(L);
    const QVector<ChannelInfo> list = host(L)->channels(id);
    lua_newtable(L);
    for (int i = 0; i < list.size(); ++i)
    {
        pushChannelRef(L, id, list[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ---- src:channel(name) ----
int m_channel(lua_State *L)
{
    const int id = srcId(L);
    const QString name = QString::fromUtf8(luaL_checkstring(L, 2));
    for (const ChannelInfo &c : host(L)->channels(id))
        if (c.name == name)
        {
            pushChannelRef(L, id, c);
            return 1;
        }
    lua_pushnil(L);
    lua_pushfstring(L, "enoent: 通道不存在");
    return 2;
}

// ---- src:liveStatus() ----
int m_liveStatus(lua_State *L)
{
    const LiveStatus st = host(L)->liveStatus(srcId(L));
    lua_newtable(L);
    lua_pushboolean(L, st.connected);
    lua_setfield(L, -2, "connected");
    lua_pushboolean(L, st.recording);
    lua_setfield(L, -2, "recording");
    lua_pushstring(L, st.recordPath.toUtf8().constData());
    lua_setfield(L, -2, "recordPath");
    lua_pushinteger(L, static_cast<lua_Integer>(st.dropped));
    lua_setfield(L, -2, "dropped");
    lua_pushinteger(L, static_cast<lua_Integer>(st.samples));
    lua_setfield(L, -2, "samples");
    lua_pushboolean(L, st.overflow);
    lua_setfield(L, -2, "overflow");
    return 1;
}

// ---- ch:slice(t0, t1, maxPoints) -> {t=..., v=...} ----
int m_slice(lua_State *L)
{
    lua_getfield(L, 1, "__src");
    const int id = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, 1, "name");
    const QString ch = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);

    const double t0 = luaL_optnumber(L, 2, -1e300);
    const double t1 = luaL_optnumber(L, 3, 1e300);
    const int maxPoints = static_cast<int>(luaL_optinteger(L, 4, 0));

    QVector<double> t;
    QVector<double> v;
    const Result r = host(L)->slice(id, ch, t0, t1, maxPoints, &t, &v);
    if (!r.ok)
        return pushFailure(L, r);

    lua_newtable(L);
    lua_newtable(L);
    for (int i = 0; i < t.size(); ++i)
    {
        lua_pushnumber(L, t[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "t");
    lua_newtable(L);
    for (int i = 0; i < v.size(); ++i)
    {
        lua_pushnumber(L, v[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "v");
    return 1;
}

// ---- ps.app.load(path, {loader=...}) ----
int l_load(lua_State *L)
{
    const QString path = QString::fromUtf8(luaL_checkstring(L, 1));
    QString loader;
    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "loader");
        if (lua_isstring(L, -1))
            loader = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    const JobHandle h = host(L)->loadFile(path, loader);
    if (!h.valid())
        return pushFailure(L, Result::failure(QStringLiteral("eload"),
                                              QStringLiteral("加载失败：%1").arg(path)));
    pushJob(L, h);
    return 1;
}

int jobId(lua_State *L)
{
    lua_getfield(L, 1, "id");
    const int id = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return id;
}

// S1 的作业是同步完成的，wait 立即返回
int m_wait(lua_State *L)
{
    lua_pushboolean(L, jobId(L) >= 0);
    return 1;
}

int m_cancel(lua_State *L)
{
    lua_pushboolean(L, true);
    return 1;
}

// ---- ps.app.exportCsv(src, path, {overwrite=true}) ----
int l_exportCsv(lua_State *L)
{
    const int id = srcId(L);
    const QString path = QString::fromUtf8(luaL_checkstring(L, 2));
    bool overwrite = false;
    if (lua_istable(L, 3))
    {
        lua_getfield(L, 3, "overwrite");
        overwrite = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    const JobHandle h = host(L)->exportCsv(id, path, overwrite);
    if (!h.valid())
        return pushFailure(L, Result::failure(QStringLiteral("eexport"),
                                              QStringLiteral("导出失败：%1").arg(path)));
    lua_pushboolean(L, true);
    return 1;
}

// ---- ps.app.close(src) ----
int l_close(lua_State *L)
{
    const Result r = host(L)->closeSource(srcId(L));
    if (!r.ok)
        return pushFailure(L, r);
    lua_pushboolean(L, true);
    return 1;
}
} // namespace

void installAppApi(lua_State *L)
{
    // metatable: source
    luaL_newmetatable(L, kSourceMt);
    lua_newtable(L);
    lua_pushcfunction(L, m_channels);
    lua_setfield(L, -2, "channels");
    lua_pushcfunction(L, m_channel);
    lua_setfield(L, -2, "channel");
    lua_pushcfunction(L, m_liveStatus);
    lua_setfield(L, -2, "liveStatus");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // metatable: channel
    luaL_newmetatable(L, kChannelMt);
    lua_newtable(L);
    lua_pushcfunction(L, m_slice);
    lua_setfield(L, -2, "slice");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // metatable: job
    luaL_newmetatable(L, kJobMt);
    lua_newtable(L);
    lua_pushcfunction(L, m_wait);
    lua_setfield(L, -2, "wait");
    lua_pushcfunction(L, m_cancel);
    lua_setfield(L, -2, "cancel");
    lua_pushcfunction(L, m_wait);
    lua_setfield(L, -2, "onDone");
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // ps.app
    lua_getglobal(L, "ps");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "ps");
    }
    lua_newtable(L);
    lua_pushcfunction(L, l_sources);
    lua_setfield(L, -2, "sources");
    lua_pushcfunction(L, l_source);
    lua_setfield(L, -2, "source");
    lua_pushcfunction(L, l_currentSource);
    lua_setfield(L, -2, "currentSource");
    lua_pushcfunction(L, l_load);
    lua_setfield(L, -2, "load");
    lua_pushcfunction(L, l_exportCsv);
    lua_setfield(L, -2, "exportCsv");
    lua_pushcfunction(L, l_close);
    lua_setfield(L, -2, "close");
    lua_setfield(L, -2, "app");
    lua_pop(L, 1);
}

} // namespace ps::scripting

#else

namespace ps::scripting
{
void installAppApi(lua_State *) {}

} // namespace ps::scripting

#endif

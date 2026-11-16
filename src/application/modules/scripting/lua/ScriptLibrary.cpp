#include "ScriptLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef PS_HAVE_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

namespace ps::scripting
{
namespace
{
QString g_root;

QString defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/scripts/lib");
}

// 名字只允许字母数字、下划线、连字符、点与正斜杠；其余一律拒绝。
bool nameAllowed(const QString &name)
{
    if (name.isEmpty() || name.size() > 128)
        return false;
    if (name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
        name.contains(QLatin1Char(':')) || name.contains(QLatin1String("..")))
        return false;
    for (const QChar c : name)
    {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-') &&
            c != QLatin1Char('.') && c != QLatin1Char('/'))
            return false;
    }
    return true;
}

#ifdef PS_HAVE_LUA
// 加载前先放哨兵，避免 a→b→a 的递归 require 无限递归
int l_require(lua_State *L)
{
    const char *raw = luaL_checkstring(L, 1);
    const QString name = QString::fromUtf8(raw);

    // _LOADED 表用绝对索引记住，避免后面压入/弹出时错位
    const int loaded = lua_gettop(L) + 1;
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    lua_getfield(L, loaded, raw);
    if (!lua_isnil(L, -1))
        return 1; // 栈: 表, 值
    lua_pop(L, 1);

    QString path;
    QString error;
    if (!ScriptLibrary::resolve(name, &path, &error))
        return luaL_error(L, "%s", error.toUtf8().constData());

    lua_pushboolean(L, 1); // 哨兵
    lua_setfield(L, loaded, raw);

    if (luaL_loadfile(L, path.toUtf8().constData()) != LUA_OK)
    {
        lua_pushnil(L);
        lua_setfield(L, loaded, raw);
        return luaL_error(L, "%s", lua_tostring(L, -1));
    }
    lua_pushstring(L, raw); // chunk 参数：脚本可接 ... 拿到模块名
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        lua_pushnil(L);
        lua_setfield(L, loaded, raw);
        return luaL_error(L, "%s", lua_tostring(L, -1));
    }
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }

    lua_pushvalue(L, -1);
    lua_setfield(L, loaded, raw);
    return 1;
}
#endif
} // namespace

void ScriptLibrary::setRoot(const QString &root)
{
    g_root = QDir::cleanPath(root);
}

QString ScriptLibrary::root()
{
    return g_root.isEmpty() ? defaultRoot() : g_root;
}

bool ScriptLibrary::resolve(const QString &name, QString *path, QString *error)
{
    if (!nameAllowed(name))
    {
        if (error)
            *error = QStringLiteral("非法模块名：%1").arg(name);
        return false;
    }
    const QString base = root();
    const QString rel = QString(name).replace(QLatin1Char('.'), QLatin1Char('/'));
    const QString candidate = base + QLatin1Char('/') + rel + QStringLiteral(".lua");
    const QFileInfo fi(candidate);
    if (!fi.exists() || !fi.isFile())
    {
        if (error)
            *error = QStringLiteral("脚本库中找不到模块：%1（%2）").arg(name, candidate);
        return false;
    }
    if (path)
        *path = fi.absoluteFilePath();
    return true;
}

#ifdef PS_HAVE_LUA
void ScriptLibrary::install(lua_State *L)
{
    lua_pushcfunction(L, l_require);
    lua_setglobal(L, "require");
}

void ScriptLibrary::invalidate(lua_State *L, const QString &name)
{
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    lua_pushnil(L);
    lua_setfield(L, -2, name.toUtf8().constData());
    lua_pop(L, 1);
}

void ScriptLibrary::invalidateAll(lua_State *L)
{
    lua_newtable(L); // 新空表覆盖 _LOADED
    lua_setfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
}
#else
void ScriptLibrary::install(lua_State *) {}
void ScriptLibrary::invalidate(lua_State *, const QString &) {}
void ScriptLibrary::invalidateAll(lua_State *) {}
#endif

} // namespace ps::scripting

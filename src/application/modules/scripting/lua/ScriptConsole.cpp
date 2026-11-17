#include "scripting/ScriptConsole.h"

#include "scripting/ScriptEngine.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <QFile>
#include <QRegularExpression>

#include <cmath>

namespace ps::scripting
{
namespace
{
const char *kConsoleOut = "__ps_console_out";
const char *kChunkName = "=console"; // '=' 前缀：错误里不以 [string "..."] 形式出现

// print 的输出去控制台：按 Lua 语义用 tab 连接，写进 __ps_console_out
int l_console_print(lua_State *L)
{
    const int n = lua_gettop(L);
    QStringList parts;
    for (int i = 1; i <= n; ++i)
    {
        size_t len = 0;
        const char *s = luaL_tolstring(L, i, &len);
        parts << QString::fromUtf8(s, static_cast<int>(len));
        lua_pop(L, 1);
    }
    lua_getglobal(L, kConsoleOut);
    if (lua_istable(L, -1))
    {
        const lua_Integer idx = luaL_len(L, -1) + 1;
        lua_pushstring(L, parts.join(QStringLiteral("\t")).toUtf8().constData());
        lua_rawseti(L, -2, idx);
    }
    lua_pop(L, 1);
    return 0;
}

// collectgarbage 已从沙箱摘掉，给回一个受控入口
int l_ps_gc(lua_State *L)
{
    const int kb = lua_gc(L, LUA_GCCOLLECT, 0);
    lua_pushinteger(L, kb);
    return 1;
}

QString numberText(double v)
{
    if (std::floor(v) == v && std::abs(v) < 9e15)
        return QString::number(static_cast<qlonglong>(v));
    return QString::number(v, 'g', 14);
}

// 序列：键总数等于 #t，说明是 1..n 的连续数组
bool isSequence(lua_State *L, int idx)
{
    idx = lua_absindex(L, idx);
    const lua_Integer len = luaL_len(L, idx);
    if (len <= 0)
        return false;
    int total = 0;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0)
    {
        lua_pop(L, 1);
        ++total;
    }
    return total == static_cast<int>(len);
}

QString describe(lua_State *L, int idx)
{
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx))
    {
    case LUA_TNIL:
        return QStringLiteral("nil");
    case LUA_TBOOLEAN:
        return lua_toboolean(L, idx) ? QStringLiteral("true") : QStringLiteral("false");
    case LUA_TNUMBER:
        return numberText(lua_tonumber(L, idx));
    case LUA_TSTRING:
    {
        QString s = QString::fromUtf8(lua_tostring(L, idx));
        if (s.size() > 80)
            s = s.left(77) + QStringLiteral("...");
        return QStringLiteral("\"%1\"").arg(s);
    }
    case LUA_TTABLE:
    {
        if (isSequence(L, idx))
        {
            const lua_Integer len = luaL_len(L, idx);
            const lua_Integer show = qMin<lua_Integer>(len, 8);
            QStringList items;
            for (lua_Integer i = 1; i <= show; ++i)
            {
                lua_rawgeti(L, idx, i);
                items << describe(L, -1);
                lua_pop(L, 1);
            }
            QString body = items.join(QStringLiteral(", "));
            if (len > show)
                body += QStringLiteral(", ...");
            return QStringLiteral("[%1] {%2}").arg(len).arg(body);
        }
        lua_pushnil(L);
        int count = 0;
        while (lua_next(L, idx) != 0)
        {
            lua_pop(L, 1);
            ++count;
        }
        return QStringLiteral("table (%1 项)").arg(count);
    }
    case LUA_TFUNCTION:
        return QStringLiteral("function");
    case LUA_TUSERDATA:
        return QStringLiteral("userdata");
    case LUA_TTHREAD:
        return QStringLiteral("thread");
    default:
        return QStringLiteral("lightuserdata");
    }
}

// console:3: msg → 行 3: msg；文件 chunk 的错误保持原样（本来就带路径）
QString prettyError(const QString &raw)
{
    QString s = raw;
    s.replace(QRegularExpression(QStringLiteral("^console:(\\d+): ")), QStringLiteral("行 \\1: "));
    return s;
}

QStringList takeOutput(lua_State *L)
{
    QStringList lines;
    lua_getglobal(L, kConsoleOut);
    if (lua_istable(L, -1))
    {
        const lua_Integer len = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= len; ++i)
        {
            lua_rawgeti(L, -1, i);
            lines << QString::fromUtf8(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_newtable(L);
    lua_setglobal(L, kConsoleOut);
    return lines;
}
} // namespace

struct ScriptConsole::Impl
{
    lua_State *L = nullptr;
    QStringList builtin; // 初始全局名，who 时排除
    QVector<std::function<void(lua_State *)>> hooks;

    void recordBaseline()
    {
        builtin.clear();
        lua_pushglobaltable(L);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            if (lua_type(L, -2) == LUA_TSTRING)
                builtin << QString::fromUtf8(lua_tostring(L, -2));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }

    void install()
    {
        L = ps::scripting::createConsoleState();
        if (!L)
            return;
        lua_pushcfunction(L, l_console_print);
        lua_setglobal(L, "print");
        lua_getglobal(L, "ps");
        if (lua_istable(L, -1))
        {
            lua_pushcfunction(L, l_ps_gc);
            lua_setfield(L, -2, "gc");
        }
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_setglobal(L, "ans");
        lua_newtable(L);
        lua_setglobal(L, kConsoleOut);
        for (auto &hook : hooks)
            if (hook)
                hook(L);
        recordBaseline();
    }
};

ScriptConsole::ScriptConsole() : d(std::make_unique<Impl>())
{
    d->install();
}

ScriptConsole::~ScriptConsole()
{
    if (d && d->L)
        ps::scripting::destroyConsoleState(d->L);
}

bool ScriptConsole::isMetaCommand(const QString &input)
{
    return input.trimmed().startsWith(QLatin1Char(':'));
}

void ScriptConsole::addStateHook(std::function<void(lua_State *)> hook)
{
    if (!hook)
        return;
    d->hooks.append(std::move(hook));
    if (d->L)
        d->hooks.last()(d->L);
}

void ScriptConsole::clear()
{
    if (!d->L)
        return;
    ps::scripting::destroyConsoleState(d->L);
    d->L = nullptr;
    d->install();
}

QStringList ScriptConsole::globals() const
{
    QStringList out;
    if (!d->L)
        return out;
    lua_pushglobaltable(d->L);
    lua_pushnil(d->L);
    while (lua_next(d->L, -2) != 0)
    {
        if (lua_type(d->L, -2) == LUA_TSTRING)
        {
            const QString k = QString::fromUtf8(lua_tostring(d->L, -2));
            if (!k.startsWith(QLatin1Char('_')) && !d->builtin.contains(k))
                out << k;
        }
        lua_pop(d->L, 1);
    }
    lua_pop(d->L, 1);
    return out;
}

QString ScriptConsole::help() const
{
    return QStringLiteral(
               "表达式直接回车求值，结果存入 ans。\n"
               "  1+2             → 3\n"
               "  x = ps.app.sources()[1]\n"
               "  x:channels()\n"
               "元命令：\n"
               "  :help           帮助\n"
               "  :clear          清空 workspace\n"
               "  :who            列出变量\n"
               "  :whos           列出变量与类型\n"
               "  :run <file>     执行脚本文件（变量留在 workspace）\n"
               "  :reload [name]  重载模块（无参数则清空模块缓存）\n"
               "  :clc            清屏\n"
               "数据面：ps.app.sources/source/channels/slice/load/exportCsv\n");
}

bool ScriptConsole::incomplete(const QString &input) const
{
    if (!d->L)
        return false;
    const QByteArray code = input.toUtf8();
    const int rc = luaL_loadbufferx(d->L, code.constData(), static_cast<size_t>(code.size()),
                                    kChunkName, "t");
    const QString err = rc == LUA_OK ? QString() : QString::fromUtf8(lua_tostring(d->L, -1));
    lua_pop(d->L, 1); // 函数或错误消息
    return rc != LUA_OK && err.contains(QStringLiteral("<eof>"));
}

ScriptConsole::Result ScriptConsole::eval(const QString &input)
{
    Result r;
    if (!d->L)
    {
        r.kind = Result::Error;
        r.text = QStringLiteral("当前构建未启用 Lua 5.4");
        return r;
    }
    const QString text = input.trimmed();
    if (text.isEmpty())
        return r;

    if (isMetaCommand(text))
    {
        const QString body = text.mid(1).trimmed();
        const QStringList parts = body.split(QRegularExpression(QStringLiteral("\\s+")),
                                             Qt::SkipEmptyParts);
        const QString cmd = parts.isEmpty() ? QString() : parts.first();
        const QString arg = body.mid(cmd.size()).trimmed();

        if (cmd == QLatin1String("help"))
        {
            r.kind = Result::Output;
            r.text = help();
            return r;
        }
        if (cmd == QLatin1String("clear"))
        {
            clear();
            r.kind = Result::Output;
            r.text = QStringLiteral("workspace 已清空");
            return r;
        }
        if (cmd == QLatin1String("clc"))
        {
            r.kind = Result::Clear;
            return r;
        }
        if (cmd == QLatin1String("who") || cmd == QLatin1String("whos"))
        {
            r.kind = Result::Output;
            const QStringList names = globals();
            if (names.isEmpty())
            {
                r.text = QStringLiteral("（空）");
                return r;
            }
            if (cmd == QLatin1String("who"))
            {
                r.text = names.join(QStringLiteral("  "));
                return r;
            }
            QStringList rows;
            for (const QString &n : names)
            {
                lua_getglobal(d->L, n.toUtf8().constData());
                rows << QStringLiteral("%1\t%2").arg(n, describe(d->L, -1));
                lua_pop(d->L, 1);
            }
            r.text = rows.join(QChar('\n'));
            return r;
        }
        if (cmd == QLatin1String("reload"))
        {
            lua_getfield(d->L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
            if (lua_istable(d->L, -1))
            {
                // 内置库不能清：清掉后 require("string") 拿不回来，且会破坏已有代码
                const QStringList keep = {QStringLiteral("_G"), QStringLiteral("string"),
                                          QStringLiteral("table"), QStringLiteral("math"),
                                          QStringLiteral("utf8"), QStringLiteral("coroutine")};
                if (!arg.isEmpty())
                {
                    if (!keep.contains(arg))
                    {
                        lua_pushnil(d->L);
                        lua_setfield(d->L, -2, arg.toUtf8().constData());
                    }
                }
                else
                {
                    // 边遍历边删会破坏 next，先收集再删
                    QStringList drop;
                    lua_pushnil(d->L);
                    while (lua_next(d->L, -2) != 0)
                    {
                        lua_pop(d->L, 1);
                        if (lua_type(d->L, -1) == LUA_TSTRING)
                        {
                            const QString k = QString::fromUtf8(lua_tostring(d->L, -1));
                            if (!keep.contains(k))
                                drop << k;
                        }
                    }
                    for (const QString &k : drop)
                    {
                        lua_pushnil(d->L);
                        lua_setfield(d->L, -2, k.toUtf8().constData());
                    }
                }
            }
            lua_pop(d->L, 1);
            r.kind = Result::Output;
            r.text = arg.isEmpty() ? QStringLiteral("模块缓存已清空")
                                   : QStringLiteral("已重载 %1").arg(arg);
            return r;
        }
        if (cmd == QLatin1String("run"))
        {
            if (arg.isEmpty())
            {
                r.kind = Result::Error;
                r.text = QStringLiteral("用法：:run <file.lua>");
                return r;
            }
            QFile f(arg);
            if (!f.open(QIODevice::ReadOnly))
            {
                r.kind = Result::Error;
                r.text = QStringLiteral("无法读取：%1").arg(arg);
                return r;
            }
            const QByteArray code = f.readAll();
            const QByteArray chunk = arg.toUtf8();
            if (luaL_loadbufferx(d->L, code.constData(), static_cast<size_t>(code.size()),
                                 chunk.constData(), "t") != LUA_OK)
            {
                r.kind = Result::Error;
                r.text = QString::fromUtf8(lua_tostring(d->L, -1));
                lua_pop(d->L, 1);
                return r;
            }
            if (lua_pcall(d->L, 0, 0, 0) != LUA_OK)
            {
                r.kind = Result::Error;
                r.text = QString::fromUtf8(lua_tostring(d->L, -1));
                lua_pop(d->L, 1);
                return r;
            }
            const QStringList lines = takeOutput(d->L);
            if (!lines.isEmpty())
            {
                r.kind = Result::Output;
                r.text = lines.join(QChar('\n'));
            }
            return r;
        }
        r.kind = Result::Error;
        r.text = QStringLiteral("未知元命令：%1（:help 看列表）").arg(cmd);
        return r;
    }

    // 先按表达式试，失败再按语句
    QByteArray code = ("return " + text).toUtf8();
    const int exprRc =
        luaL_loadbufferx(d->L, code.constData(), static_cast<size_t>(code.size()), kChunkName, "t");
    const bool asExpr = exprRc == LUA_OK;
    if (!asExpr)
        lua_pop(d->L, 1);

    if (!asExpr)
    {
        code = text.toUtf8();
        if (luaL_loadbufferx(d->L, code.constData(), static_cast<size_t>(code.size()), kChunkName,
                             "t") != LUA_OK)
        {
            r.kind = Result::Error;
            r.text = prettyError(QString::fromUtf8(lua_tostring(d->L, -1)));
            lua_pop(d->L, 1);
            return r;
        }
    }

    QStringList lines;
    // pcall 会先弹出栈上的 chunk 函数，返回值从 base 位置开始
    const int base = lua_gettop(d->L);
    const int restore = base - 1;
    // 多返回值：print(...) 没有返回值，不能给它补一个 nil
    if (lua_pcall(d->L, 0, LUA_MULTRET, 0) != LUA_OK)
    {
        lines = takeOutput(d->L);
        const QString err = prettyError(QString::fromUtf8(lua_tostring(d->L, -1)));
        lua_settop(d->L, restore);
        r.kind = Result::Error;
        r.text = lines.isEmpty() ? err : lines.join(QChar('\n')) + QChar('\n') + err;
        return r;
    }

    // pcall 之后立刻收 print 的输出
    lines = takeOutput(d->L);

    if (asExpr && lua_gettop(d->L) >= base)
    {
        QString shown = describe(d->L, base);
        // nil + 错误串：Lua 侧的失败约定是 return nil, err，别把错误吞掉
        if (lua_gettop(d->L) > base && lua_isnil(d->L, base) && lua_isstring(d->L, base + 1))
            shown += QStringLiteral("  ") + QString::fromUtf8(lua_tostring(d->L, base + 1));
        lua_pushvalue(d->L, base);
        lua_setglobal(d->L, "ans");
        lines << shown;
    }
    lua_settop(d->L, restore);

    if (!lines.isEmpty())
    {
        r.kind = Result::Output;
        r.text = lines.join(QChar('\n'));
    }
    return r;
}

} // namespace ps::scripting

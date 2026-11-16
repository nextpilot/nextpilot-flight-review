#include "scripting/ScriptEngine.h"

#include "LuaAppApi.h"
#include "ScriptLibrary.h"

#include <cmath>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <cstdlib>
#include <cstring>

#ifdef PS_HAVE_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

namespace {

ScriptResult runPython(const QString &source, const QVector<ScriptSeries> &inputs,
                       const QMap<QString, double> &params)
{
    ScriptResult r;
    const QString py = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (py.isEmpty())
    {
        r.error = QStringLiteral("未找到 python3");
        return r;
    }
    QTemporaryFile script(QDir::tempPath() + QStringLiteral("/plotscope-XXXXXX.py"));
    if (!script.open())
    {
        r.error = QStringLiteral("无法创建临时脚本");
        return r;
    }
    script.write(source.toUtf8());
    script.close();

    QJsonArray inArr;
    for (const ScriptSeries &s : inputs)
    {
        QJsonObject o;
        o[QStringLiteral("name")] = s.name;
        QJsonArray t, v;
        for (double x : s.time) t.append(x);
        for (double x : s.value) v.append(x);
        o[QStringLiteral("time")] = t;
        o[QStringLiteral("value")] = v;
        inArr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("inputs")] = inArr;
    QJsonObject p;
    for (auto it = params.begin(); it != params.end(); ++it)
        p[it.key()] = it.value();
    root[QStringLiteral("params")] = p;
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QProcess proc;
    proc.setProgram(py);
    proc.setArguments({script.fileName()});
    proc.start();
    if (!proc.waitForStarted(5000))
    {
        r.error = QStringLiteral("Python 启动失败");
        return r;
    }
    proc.write(payload);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(15000))
    {
        proc.kill();
        r.error = QStringLiteral("Python 执行超时");
        return r;
    }
    const QString stderrText = QString::fromUtf8(proc.readAllStandardError());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        r.error = stderrText.trimmed().isEmpty()
                      ? QStringLiteral("Python 退出码 %1").arg(proc.exitCode())
                      : stderrText.trimmed();
        return r;
    }
    const QJsonDocument out = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!out.isObject())
    {
        r.error = QStringLiteral("Python 输出不是 JSON 对象");
        return r;
    }
    for (const QJsonValue &v : out.object().value(QStringLiteral("outputs")).toArray())
    {
        const QJsonObject o = v.toObject();
        ScriptOutput so;
        so.name = o.value(QStringLiteral("name")).toString();
        for (const QJsonValue &x : o.value(QStringLiteral("time")).toArray())
            so.time.append(x.toDouble());
        for (const QJsonValue &x : o.value(QStringLiteral("value")).toArray())
            so.value.append(x.toDouble());
        if (!so.name.isEmpty() && !so.time.isEmpty() && so.time.size() == so.value.size())
            r.outputs.append(so);
    }
    r.ok = !r.outputs.isEmpty();
    if (!r.ok)
        r.error = QStringLiteral("Python 没有返回有效 outputs");
    return r;
}

#ifdef PS_HAVE_LUA
struct LuaMem
{
    size_t used = 0;
    size_t limit = 64u * 1024u * 1024u;
};

void *trackedAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    LuaMem *m = static_cast<LuaMem *>(ud);
    if (nsize == 0)
    {
        if (ptr)
        {
            m->used -= osize;
            std::free(ptr);
        }
        return nullptr;
    }
    if (!ptr)
    {
        if (m->used + nsize > m->limit)
            return nullptr;
        void *p = std::malloc(nsize);
        if (p)
            m->used += nsize;
        return p;
    }
    if (nsize > osize && m->used + (nsize - osize) > m->limit)
        return nullptr;
    void *p = std::realloc(ptr, nsize);
    if (p)
        m->used = m->used - osize + nsize;
    return p;
}

void instructionHook(lua_State *L, lua_Debug *)
{
    luaL_error(L, "脚本超出 2000 万条指令预算");
}

void pushVector(lua_State *L, const QVector<double> &v)
{
    lua_createtable(L, v.size(), 0);
    for (int i = 0; i < v.size(); ++i)
    {
        lua_pushnumber(L, v.at(i));
        lua_rawseti(L, -2, i + 1);
    }
}

bool readVector(lua_State *L, int index, QVector<double> &out)
{
    index = lua_absindex(L, index);
    if (!lua_istable(L, index))
        return false;
    const lua_Integer n = luaL_len(L, index);
    out.clear();
    out.reserve(static_cast<int>(n));
    for (lua_Integer i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, index, i);
        if (!lua_isnumber(L, -1))
        {
            lua_pop(L, 1);
            return false;
        }
        out.append(static_cast<double>(lua_tonumber(L, -1)));
        lua_pop(L, 1);
    }
    return true;
}

int l_ch(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    lua_getglobal(L, "__ps_inputs");
    lua_getfield(L, -1, name);
    lua_remove(L, -2);
    return 1;
}

int l_time(lua_State *L)
{
    lua_getglobal(L, "__ps_time");
    return 1;
}

int l_input(lua_State *L)
{
    const int n = static_cast<int>(luaL_checkinteger(L, 1));
    if (n < 1 || n > 4)
        return luaL_error(L, "ps.input 只支持 1..4");
    lua_getglobal(L, "__ps_v");
    lua_rawgeti(L, -1, n);
    lua_remove(L, -2);
    return 1;
}

int l_plot(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    if (!lua_istable(L, 2))
        return luaL_error(L, "ps.plot 第二个参数必须是数组");
    lua_getglobal(L, "__ps_out");
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
    return 0;
}

int l_moving_avg(lua_State *L)
{
    QVector<double> v;
    if (!readVector(L, 1, v))
        return luaL_error(L, "ps.movingAvg 第一个参数必须是数组");
    int n = static_cast<int>(luaL_optinteger(L, 2, 1));
    n = std::max(1, n);
    QVector<double> out(v.size());
    double sum = 0.0;
    for (int i = 0; i < v.size(); ++i)
    {
        sum += v.at(i);
        if (i >= n)
            sum -= v.at(i - n);
        out[i] = sum / std::min(i + 1, n);
    }
    pushVector(L, out);
    return 1;
}

int l_derivative(lua_State *L)
{
    QVector<double> v, t;
    if (!readVector(L, 1, v))
        return luaL_error(L, "ps.derivative 第一个参数必须是数组");
    const bool hasT = lua_istable(L, 2) && readVector(L, 2, t);
    QVector<double> out(v.size());
    for (int i = 1; i < v.size(); ++i)
    {
        const double dt = hasT && i < t.size() ? t.at(i) - t.at(i - 1) : 1.0;
        out[i] = dt > 0 ? (v.at(i) - v.at(i - 1)) / dt : 0.0;
    }
    if (out.size() > 0)
        out[0] = out.size() > 1 ? out.at(1) : 0.0;
    pushVector(L, out);
    return 1;
}

int l_integral(lua_State *L)
{
    QVector<double> v, t;
    if (!readVector(L, 1, v))
        return luaL_error(L, "ps.integral 第一个参数必须是数组");
    const bool hasT = lua_istable(L, 2) && readVector(L, 2, t);
    QVector<double> out(v.size());
    for (int i = 1; i < v.size(); ++i)
    {
        const double dt = hasT && i < t.size() ? t.at(i) - t.at(i - 1) : 1.0;
        out[i] = out.at(i - 1) + 0.5 * (v.at(i) + v.at(i - 1)) * dt;
    }
    pushVector(L, out);
    return 1;
}

int l_scale(lua_State *L)
{
    QVector<double> v;
    if (!readVector(L, 1, v))
        return luaL_error(L, "ps.scale 第一个参数必须是数组");
    const double k = luaL_optnumber(L, 2, 1.0);
    const double b = luaL_optnumber(L, 3, 0.0);
    for (double &x : v)
        x = x * k + b;
    pushVector(L, v);
    return 1;
}

void registerApi(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_ch);
    lua_setfield(L, -2, "ch");
    lua_pushcfunction(L, l_time);
    lua_setfield(L, -2, "time");
    lua_pushcfunction(L, l_input);
    lua_setfield(L, -2, "input");
    lua_pushcfunction(L, l_plot);
    lua_setfield(L, -2, "plot");
    lua_pushcfunction(L, l_moving_avg);
    lua_setfield(L, -2, "movingAvg");
    lua_pushcfunction(L, l_derivative);
    lua_setfield(L, -2, "derivative");
    lua_pushcfunction(L, l_integral);
    lua_setfield(L, -2, "integral");
    lua_pushcfunction(L, l_scale);
    lua_setfield(L, -2, "scale");
    lua_setglobal(L, "ps");
}

void clearDangerous(lua_State *L)
{
    // require 不在名单里：它由 ScriptLibrary 换成受限版本（只认 scripts/lib）。
    const char *names[] = {"os", "io", "package", "debug", "dofile", "loadfile", "collectgarbage"};
    for (const char *n : names)
    {
        lua_pushnil(L);
        lua_setglobal(L, n);
    }
    // 只摘全局不够：luaL_openlibs 把这些表也登记进了 _LOADED，
    // 不清掉的话 require("os") 会把 os 原样交回来，沙箱形同虚设。
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    if (lua_istable(L, -1))
    {
        for (const char *n : names)
        {
            lua_pushnil(L);
            lua_setfield(L, -2, n);
        }
    }
    lua_pop(L, 1);
}

QVector<double> timeFor(const QVector<ScriptSeries> &inputs, int len);

ScriptResult runStepMode(lua_State *L, const QVector<ScriptSeries> &inputs,
                         const QMap<QString, double> &params)
{
    ScriptResult r;
    auto fail = [&](const char *where) {
        const char *msg = lua_tostring(L, -1);
        r.error = QStringLiteral("%1：%2").arg(QString::fromUtf8(where),
                                               QString::fromUtf8(msg ? msg : "未知错误"));
        return r;
    };

    lua_getglobal(L, "init");
    if (lua_isfunction(L, -1))
    {
        lua_newtable(L); // init(params)
        for (auto it = params.begin(); it != params.end(); ++it)
        {
            lua_pushstring(L, it.key().toUtf8().constData());
            lua_pushnumber(L, it.value());
            lua_settable(L, -3);
        }
        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
            return fail("init");
    }
    else
    {
        lua_pop(L, 1);
    }

    lua_getglobal(L, "reset");
    if (lua_isfunction(L, -1))
    {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK)
            return fail("reset");
    }
    else
    {
        lua_pop(L, 1);
    }

    const int n = inputs.isEmpty() ? 0 : inputs.first().value.size();
    QMap<QString, QVector<double>> out;
    for (int i = 0; i < n; ++i)
    {
        lua_getglobal(L, "step");
        if (!lua_isfunction(L, -1))
        {
            lua_pop(L, 1);
            r.error = QStringLiteral("step 不是函数");
            return r;
        }
        const double t = inputs.first().time.size() > i ? inputs.first().time.at(i) : static_cast<double>(i);
        lua_pushnumber(L, t);
        for (int j = 0; j < 4; ++j)
            if (j < inputs.size() && inputs.at(j).value.size() > i)
                lua_pushnumber(L, inputs.at(j).value.at(i));
            else
                lua_pushnil(L);
        if (lua_pcall(L, 5, 1, 0) != LUA_OK)
            return fail("step");
        if (lua_isnumber(L, -1))
        {
            out[QStringLiteral("step")].append(lua_tonumber(L, -1));
        }
        else if (lua_istable(L, -1))
        {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                if (lua_isstring(L, -2) && lua_isnumber(L, -1))
                    out[QString::fromUtf8(lua_tostring(L, -2))].append(lua_tonumber(L, -1));
                lua_pop(L, 1);
            }
        }
        else
        {
            lua_pop(L, 1);
            r.error = QStringLiteral("step 返回类型必须是 number 或 table");
            return r;
        }
        lua_pop(L, 1);
    }

    for (auto it = out.begin(); it != out.end(); ++it)
    {
        ScriptOutput o;
        o.name = it.key();
        o.value = it.value();
        o.time = timeFor(inputs, o.value.size());
        r.outputs.append(o);
    }
    if (r.outputs.isEmpty())
    {
        r.error = QStringLiteral("step 没有输出");
        return r;
    }
    r.ok = true;
    return r;
}

QVector<double> timeFor(const QVector<ScriptSeries> &inputs, int len)
{
    QVector<double> t;
    if (!inputs.isEmpty() && !inputs.first().time.isEmpty())
    {
        t = inputs.first().time;
        if (t.size() >= len)
        {
            t.resize(len);
            return t;
        }
    }
    const double dt = t.size() > 1 ? t.at(1) - t.at(0) : 0.01;
    const double t0 = t.isEmpty() ? 0.0 : t.first();
    t.resize(len);
    for (int i = 0; i < len; ++i)
        t[i] = t0 + i * dt;
    return t;
}
#endif
} // namespace

ScriptResult ScriptEngine::run(const QString &source, const QVector<ScriptSeries> &inputs,
                               const QMap<QString, double> &params, const QString &language,
                               bool requireOutput)
{
    ScriptResult r;
    const QString lang = language.isEmpty()
                             ? (source.trimmed().startsWith(QStringLiteral("# python")) ||
                                        source.trimmed().startsWith(QStringLiteral("#!python"))
                                    ? QStringLiteral("python")
                                    : QStringLiteral("lua"))
                             : language.toLower();
    if (lang == QLatin1String("python"))
        return runPython(source, inputs, params);
#ifndef PS_HAVE_LUA
    Q_UNUSED(source)
    Q_UNUSED(inputs)
    r.error = QStringLiteral("当前构建未启用 Lua 5.4（缺少 liblua5.4-dev）");
    return r;
#else
    LuaMem mem;
    lua_State *L = lua_newstate(trackedAlloc, &mem);
    if (!L)
    {
        r.error = QStringLiteral("Lua 状态创建失败");
        return r;
    }
    luaL_openlibs(L);
    clearDangerous(L);
    ps::scripting::ScriptLibrary::install(L);
    lua_sethook(L, instructionHook, LUA_MASKCOUNT, 20000000);

    lua_newtable(L); // __ps_inputs
    for (const ScriptSeries &s : inputs)
    {
        pushVector(L, s.value);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, s.name.toUtf8().constData());
        // "run1 · att.roll" 同时注册后缀 "att.roll"
        const int sep = s.name.lastIndexOf(QStringLiteral(" · "));
        if (sep >= 0)
        {
            const QString suffix = s.name.mid(sep + 3);
            lua_pushvalue(L, -1);
            lua_setfield(L, -3, suffix.toUtf8().constData());
        }
        lua_pop(L, 1);
    }
    lua_setglobal(L, "__ps_inputs");

    lua_newtable(L); // __ps_v：按绑定顺序暴露 v1..v4
    for (int i = 0; i < 4 && i < inputs.size(); ++i)
    {
        pushVector(L, inputs.at(i).value);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setglobal(L, "__ps_v");

    pushVector(L, inputs.isEmpty() ? QVector<double>() : inputs.first().time);
    lua_setglobal(L, "__ps_time");

    lua_newtable(L);
    lua_setglobal(L, "__ps_out");
    registerApi(L);
    ps::scripting::installAppApi(L);

    const QByteArray code = source.toUtf8();
    if (luaL_loadbufferx(L, code.constData(), static_cast<size_t>(code.size()), "script", "t") != LUA_OK)
    {
        r.error = QString::fromUtf8(lua_tostring(L, -1));
        lua_close(L);
        return r;
    }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        r.error = QString::fromUtf8(lua_tostring(L, -1));
        lua_close(L);
        return r;
    }

    // init/reset/step 协议优先：逐点调用，结果按名字聚合成序列。
    lua_getglobal(L, "step");
    const bool stepMode = lua_isfunction(L, -1);
    lua_pop(L, 1);
    if (stepMode)
    {
        const ScriptResult sr = runStepMode(L, inputs, params);
        lua_close(L);
        return sr;
    }

    // 没有 ps.plot 时，把全局 out 当作唯一输出
    lua_getglobal(L, "__ps_out");
    lua_pushnil(L);
    if (lua_next(L, -2) == 0)
    {
        lua_getglobal(L, "out");
        if (lua_istable(L, -1))
        {
            lua_getglobal(L, "__ps_out");
            lua_pushvalue(L, -2);
            lua_setfield(L, -2, "out");
            lua_pop(L, 2);
        }
        else
        {
            lua_pop(L, 1);
        }
    }
    else
    {
        lua_pop(L, 2); // value, key
    }
    lua_pop(L, 1); // __ps_out

    lua_getglobal(L, "__ps_out");
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        if (lua_isstring(L, -2) && lua_istable(L, -1))
        {
            ScriptOutput o;
            o.name = QString::fromUtf8(lua_tostring(L, -2));
            if (readVector(L, -1, o.value))
            {
                o.time = timeFor(inputs, o.value.size());
                if (!o.value.isEmpty())
                    r.outputs.append(o);
            }
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_close(L);

    if (requireOutput && r.outputs.isEmpty())
    {
        r.error = QStringLiteral("脚本没有输出：请调用 ps.plot(name, values) 或给全局 out 赋值");
        return r;
    }
    r.ok = true;
    return r;
#endif
}

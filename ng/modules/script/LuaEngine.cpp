#include "LuaEngine.h"

#include "ScriptFacade.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>
#include <QVariantMap>

#include <cstdio>

#include "ErrorCodes.h"
using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::context;

namespace nextpilot::plotscope::script
{

// 门面指针在 registry 中的 key。脚本拿不到它，只有 C 回调能取。
static const char *kFacadeKey = "ng.script.facade";
// 引擎指针在 registry 中的 key：print/app.log 输出经它路由到 sink（REPL）。
static const char *kEngineKey = "ng.script.engine";

static ScriptFacade *facadeOf(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kFacadeKey);
    auto *f = static_cast<ScriptFacade *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return f;
}

static LuaEngine *engineOf(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kEngineKey);
    auto *e = static_cast<LuaEngine *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return e;
}

// 输出去向：有 sink 走 sink（REPL 控制台），否则按旧约定走 stdout/stderr。
// PLOTSCONG_QUIET_STDOUT：MCP stdio 桥下 stdout 是协议通道，让路到 stderr。
static void emitOutput(lua_State *L, const QString &msg)
{
    if (LuaEngine *e = engineOf(L); e && e->hasOutputSink())
    {
        e->emitToSink(msg);
        return;
    }
    if (!qEnvironmentVariableIsEmpty("PLOTSCONG_QUIET_STDOUT"))
        fprintf(stderr, "[lua] %s\n", qPrintable(msg));
    else
        fprintf(stdout, "[lua] %s\n", qPrintable(msg));
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
    case QMetaType::QVariantList:
    {
        // 数组 payload（data.series 的 t/v、ai 目录、Job 步骤）→ Lua 数组（1..n）。
        // 缺这个分支数组会落到 default 变成串，脚本侧 #t 恒 0、无法遍历取值。
        const QVariantList list = v.toList();
        lua_createtable(L, static_cast<int>(list.size()), 0);
        for (int i = 0; i < list.size(); ++i)
        {
            pushVariant(L, list.at(i));
            lua_rawseti(L, -2, i + 1);
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

// Lua 值 -> QVariant（递归 table）。app.call 的参数表用它转成 QVariantMap。
// 支持：nil/bool/number/string/table（数组用 1..n，其余按字符串 key）。
static QVariant readVariant(lua_State *L, int idx)
{
    // 关键：idx 可能是相对索引（递归传 -1）。函数内 lua_pushnil/lua_rawgeti 会压栈，
    // 使 -1 指向新元素而非原表，lua_next(L,-1) 会把 nil 当表遍历 → 崩溃。
    // 先转成绝对索引，保证后续所有栈操作都指向同一张表。
    idx = lua_absindex(L, idx);
    const int t = lua_type(L, idx);
    switch (t)
    {
    case LUA_TNIL:
        return QVariant();
    case LUA_TBOOLEAN:
        return QVariant(lua_toboolean(L, idx) != 0);
    case LUA_TNUMBER:
        return QVariant(lua_tonumber(L, idx));
    case LUA_TSTRING:
        return QVariant(QString::fromUtf8(lua_tostring(L, idx)));
    case LUA_TTABLE:
    {
        // 先判是否为纯数组（1..n 连续且无其它键）。
        QVariantList arr;
        bool isArray = true;
        for (int i = 1; ; ++i)
        {
            lua_rawgeti(L, idx, i);
            const bool has = !lua_isnil(L, -1);
            if (has)
                arr.append(readVariant(L, -1));
            lua_pop(L, 1);
            if (!has)
                break;
        }
        lua_pushnil(L);
        while (lua_next(L, idx) != 0)
        {
            // key 在 -2，value 在 -1。非整数键说明不是纯数组。
            if (lua_type(L, -2) != LUA_TNUMBER)
                isArray = false;
            lua_pop(L, 1);
        }
        if (isArray)
            return QVariant(arr);
        QVariantMap map;
        lua_pushnil(L);
        while (lua_next(L, idx) != 0)
        {
            const QString key = QString::fromUtf8(lua_tostring(L, -2));
            map.insert(key, readVariant(L, -1));
            lua_pop(L, 1);
        }
        return QVariant(map);
    }
    default:
        return QVariant(QString::fromUtf8(lua_typename(L, t)));
    }
}

// app.log(msg) / ps.log(msg)
static int l_log(lua_State *L)
{
    const char *msg = luaL_checkstring(L, 1);
    emitOutput(L, QString::fromUtf8(msg));
    return 0;
}

// print(...)：多参数以 tab 连接，路由与 app.log 相同（REPL 控制台可捕获）。
static int l_print(lua_State *L)
{
    const int n = lua_gettop(L);
    QString out;
    for (int i = 1; i <= n; ++i)
    {
        if (i > 1)
            out += QLatin1Char('\t');
        size_t len = 0;
        const char *s = luaL_tolstring(L, i, &len);
        out += QString::fromUtf8(s, static_cast<qsizetype>(len));
    }
    emitOutput(L, out);
    return 0;
}

// app.call(op, params?) -> result（统一入口，意图 + view action 智能路由）
static int l_app_call(lua_State *L)
{
    ScriptFacade *f = facadeOf(L);
    if (!f)
        return luaL_error(L, "应用门面不可用");
    const QString op = QString::fromUtf8(luaL_checkstring(L, 1));
    QVariantMap params;
    if (lua_gettop(L) >= 2 && lua_istable(L, 2))
    {
        const QVariant pv = readVariant(L, 2);
        if (pv.typeId() == QMetaType::QVariantMap)
            params = pv.toMap();
    }
    Result<QVariant> r = f->callAny(op, params);
    if (!r)
        return luaL_error(L, "app.call(%s) 失败[%s]: %s",
                          qPrintable(op), qPrintable(r.error().code), qPrintable(r.error().message));
    pushVariant(L, r.value());
    return 1;
}

// app.view(id, params?) -> result（只走 view action 通道，语义更明确）
static int l_app_view(lua_State *L)
{
    ScriptFacade *f = facadeOf(L);
    if (!f)
        return luaL_error(L, "应用门面不可用");
    const QString id = QString::fromUtf8(luaL_checkstring(L, 1));
    QVariantMap params;
    if (lua_gettop(L) >= 2 && lua_istable(L, 2))
    {
        const QVariant pv = readVariant(L, 2);
        if (pv.typeId() == QMetaType::QVariantMap)
            params = pv.toMap();
    }
    Result<QVariant> r = f->callView(id, params);
    if (!r)
        return luaL_error(L, "app.view(%s) 失败[%s]: %s",
                          qPrintable(id), qPrintable(r.error().code), qPrintable(r.error().message));
    pushVariant(L, r.value());
    return 1;
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

// 组装全局 app 表（脚本直接引用它操作整个应用，类似 CLI）。
// app 暴露：
//   app.call(op, params?)   统一入口，意图+view action 智能路由（核心）
//   app.view(id, params?)   只走 view action 通道
//   app.load(path, fmt?)    语义化：加载数据源
//   app.render(id, ch?)     语义化：渲染曲线规格
//   app.log(msg) / app.version()
//   app.data / app.plot      ps 风格子表（向后兼容）
// 全局 ps 是 app 的别名，老脚本照常可用。
static void installFacade(lua_State *L)
{
    lua_createtable(L, 0, 10); // app

    lua_pushcfunction(L, l_version);
    lua_setfield(L, -2, "version");
    lua_pushcfunction(L, l_log);
    lua_setfield(L, -2, "log");
    lua_pushcfunction(L, l_app_call);
    lua_setfield(L, -2, "call");
    lua_pushcfunction(L, l_app_view);
    lua_setfield(L, -2, "view");
    // 语义化快捷：app.load / app.render 直接复用 data.load / plot.render 实现。
    lua_pushcfunction(L, l_data_load);
    lua_setfield(L, -2, "load");
    lua_pushcfunction(L, l_plot_render);
    lua_setfield(L, -2, "render");

    // app.data.load（兼容 ps.data.load）
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, l_data_load);
    lua_setfield(L, -2, "load");
    lua_setfield(L, -2, "data");

    // app.plot.render（兼容 ps.plot.render）
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, l_plot_render);
    lua_setfield(L, -2, "render");
    lua_setfield(L, -2, "plot");

    // 同时设为 app 与 ps（同一张表，别名）。
    lua_pushvalue(L, -1);
    lua_setglobal(L, "app");
    lua_setglobal(L, "ps");
}

// ===== 调试器 v1（2.1.7-2.1.8）：行钩子 + 断点 + 暂停/继续协议 =====

// hook 入口（friend）：只关心行事件。
void ngLuaDebugHook(lua_State *L, lua_Debug *ar)
{
    if (ar->event != LUA_HOOKLINE)
        return;
    if (LuaEngine *e = engineOf(L))
        e->debugLineHook(L, ar);
}

// 当前栈深度（帧数，0 层 = 正在运行的函数）。
int LuaEngine::stackDepth(lua_State *L) const
{
    int depth = 0;
    lua_Debug d;
    while (lua_getstack(L, depth, &d))
        ++depth;
    return depth;
}

void LuaEngine::debugLineHook(lua_State *L, lua_Debug *ar)
{
    if (!m_dbgActive)
        return;
    lua_getinfo(L, "Sl", ar);
    const int line = ar->currentline;
    const int depth = stackDepth(L);

    // 停止优先：hook 内抛错中断执行（longjmp 回 runStringDebug 的 pcall）。
    if (m_dbgStop)
    {
        m_dbgActive = false;
        m_dbgPaused = false;
        lua_sethook(L, nullptr, 0, 0);
        luaL_error(L, "执行已停止（调试停止请求）");
        return;
    }

    // 是否暂停：入口 → 单步语义 → 断点（断点只认本 chunk，库函数行号不误停）。
    bool hit = false;
    const char *reason = nullptr;
    if (m_dbgEntryPause)
    {
        hit = true;
        reason = "entry";
        m_dbgEntryPause = false;
    }
    else if (m_dbgStep == StepMode::In) // 进入：任意行都停
    {
        hit = true;
        reason = "step";
    }
    else if (m_dbgStep == StepMode::Over && depth <= m_dbgDepth) // 跳过：同深度才停
    {
        hit = true;
        reason = "step";
    }
    else if (m_dbgStep == StepMode::Out && depth < m_dbgDepth) // 跳出：深度回退才停
    {
        hit = true;
        reason = "step";
    }
    else if (m_dbgBreakpoints.contains(line) && ar->source == m_dbgChunk)
    {
        hit = true;
        reason = "breakpoint";
    }
    if (!hit)
        return;

    // 暂停快照：本 chunk 基准深度（单步基准）+ 当前帧局部变量 + 调用栈。
    m_dbgDepth = depth;
    DebugPause pause;
    pause.line = line;
    pause.chunkName = QString::fromUtf8(ar->source);
    pause.reason = reason;
    lua_Debug frame;
    if (lua_getstack(L, 0, &frame))
    {
        for (int i = 1;; ++i)
        {
            const char *name = lua_getlocal(L, &frame, i);
            if (!name)
                break;
            pause.locals.insert(QString::fromUtf8(name), readVariant(L, -1));
            lua_pop(L, 1);
        }
    }
    for (int lvl = 0; lua_getstack(L, lvl, &frame); ++lvl)
    {
        lua_getinfo(L, "nSl", &frame);
        const QString fname = frame.name ? QString::fromUtf8(frame.name) : QStringLiteral("?");
        pause.stack << QStringLiteral("%1  %2:%3")
                           .arg(fname, QString::fromUtf8(frame.short_src))
                           .arg(frame.currentline);
    }

    // 上报快照 → 嵌套事件循环等主机恢复（UI 保持响应，控制指令改写 m_dbgStep 后 quit）。
    m_dbgPaused = true;
    if (m_dbgPauseCb)
        m_dbgPauseCb(pause);
    QEventLoop loop;
    m_dbgLoop = &loop;
    loop.exec();
    m_dbgLoop = nullptr;
    m_dbgPaused = false;
    // 恢复后继续执行，下一行事件再判定。
}

void LuaEngine::debugContinue()
{
    m_dbgStep = StepMode::None;
    if (m_dbgLoop)
        m_dbgLoop->quit();
}

void LuaEngine::debugStepOver()
{
    m_dbgStep = StepMode::Over;
    if (m_dbgLoop)
        m_dbgLoop->quit();
}

void LuaEngine::debugStepIn()
{
    m_dbgStep = StepMode::In;
    if (m_dbgLoop)
        m_dbgLoop->quit();
}

void LuaEngine::debugStepOut()
{
    m_dbgStep = StepMode::Out;
    if (m_dbgLoop)
        m_dbgLoop->quit();
}

void LuaEngine::debugStop()
{
    m_dbgStop = true;
    if (m_dbgLoop)
        m_dbgLoop->quit(); // hook 恢复后看到 m_dbgStop，抛错中断
}

Result<QVariant> LuaEngine::debugEvaluate(const QString &source)
{
    if (!m_lua || !m_dbgPaused)
        return fail(err::Args, QStringLiteral("仅调试暂停期可用"));
    // 在新协程上求值：独立 Lua 栈、共享全局表。挂起的主线程栈不能再入，
    // 新协程是唯一正解。注意 lua_newthread 会继承父线程钩子，必须摘掉，
    // 否则 REPL 求值自己会触发行钩子。协程对象锚进 registry 防 GC。
    lua_State *co = lua_newthread(m_lua);
    lua_sethook(co, nullptr, 0, 0);
    const int coRef = luaL_ref(m_lua, LUA_REGISTRYINDEX);

    const QByteArray src = source.toUtf8();
    const QByteArray wrapped = "return (" + src + ")";
    int st = luaL_loadbuffer(co, wrapped.constData(), wrapped.size(), "=dbg-repl");
    if (st != LUA_OK)
    {
        lua_pop(co, 1);
        st = luaL_loadbuffer(co, src.constData(), src.size(), "=dbg-repl");
    }
    if (st != LUA_OK)
    {
        const char *err = lua_tostring(co, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("编译失败");
        lua_pop(co, 1);
        luaL_unref(m_lua, LUA_REGISTRYINDEX, coRef);
        return fail(err::Syntax, msg);
    }
    if (lua_pcall(co, 0, 1, 0) != LUA_OK)
    {
        const char *err = lua_tostring(co, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("运行失败");
        lua_pop(co, 1);
        luaL_unref(m_lua, LUA_REGISTRYINDEX, coRef);
        return fail(err::Runtime, msg);
    }
    QVariant result;
    if (lua_gettop(co) > 0 && !lua_isnil(co, -1))
        result = readVariant(co, -1);
    lua_settop(co, 0);
    luaL_unref(m_lua, LUA_REGISTRYINDEX, coRef);
    return Result<QVariant>(result);
}

// ===== 常规执行 =====

LuaEngine::LuaEngine(ScriptFacade &facade)
{
    m_lua = luaL_newstate();
    if (!m_lua)
        return;
    luaL_openlibs(m_lua);

    // 门面存 registry，脚本脚本本身拿不到指针。
    lua_pushlightuserdata(m_lua, &facade);
    lua_setfield(m_lua, LUA_REGISTRYINDEX, kFacadeKey);
    // 引擎指针同理：print/app.log 的 C 回调经它找到输出 sink。
    lua_pushlightuserdata(m_lua, this);
    lua_setfield(m_lua, LUA_REGISTRYINDEX, kEngineKey);

    // 覆盖内置 print：输出统一进 sink/stdout 通道，REPL 控制台才能捕获。
    lua_pushcfunction(m_lua, l_print);
    lua_setglobal(m_lua, "print");

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
        return fail(err::NoLua, QStringLiteral("Lua 状态创建失败"));
    const int status = luaL_loadbuffer(m_lua, source.constData(), source.size(),
                                       chunkName.toUtf8().constData());
    if (status != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本编译失败");
        lua_pop(m_lua, 1);
        return fail(err::Syntax, msg);
    }
    const int callStatus = lua_pcall(m_lua, 0, 0, 0);
    if (callStatus != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本运行失败");
        lua_pop(m_lua, 1);
        return fail(err::Runtime, msg);
    }
    return Result<void>();
}

Result<QVariant> LuaEngine::runStringResult(const QByteArray &source, const QString &chunkName)
{
    if (!m_lua)
        return fail(err::NoLua, QStringLiteral("Lua 状态创建失败"));
    const int status = luaL_loadbuffer(m_lua, source.constData(), source.size(),
                                       chunkName.toUtf8().constData());
    if (status != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本编译失败");
        lua_pop(m_lua, 1);
        return fail(err::Syntax, msg);
    }
    // nresults=1：chunk 的 return 值落在栈顶（无 return 为 nil）。
    const int callStatus = lua_pcall(m_lua, 0, 1, 0);
    if (callStatus != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本运行失败");
        lua_pop(m_lua, 1);
        return fail(err::Runtime, msg);
    }
    const QVariant out = readVariant(m_lua, -1);
    lua_pop(m_lua, 1);
    return Result<QVariant>(out);
}

Result<void> LuaEngine::runFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return fail(err::Io, QStringLiteral("无法读取脚本: %1").arg(path));
    return runString(f.readAll(), QStringLiteral("@%1").arg(path));
}

Result<void> LuaEngine::runStringDebug(const QByteArray &source, const QString &chunkName,
                                       const QSet<int> &breakpoints, PauseCallback onPause,
                                       bool stopOnEntry)
{
    if (!m_lua)
        return fail(err::NoLua, QStringLiteral("Lua 状态创建失败"));
    m_dbgBreakpoints = breakpoints;
    m_dbgChunk = chunkName;
    m_dbgPauseCb = std::move(onPause);
    m_dbgStep = StepMode::None;
    m_dbgStop = false;
    m_dbgPaused = false;
    m_dbgActive = true;
    m_dbgEntryPause = stopOnEntry;

    const int status = luaL_loadbuffer(m_lua, source.constData(), source.size(),
                                       chunkName.toUtf8().constData());
    if (status != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本编译失败");
        lua_pop(m_lua, 1);
        m_dbgActive = false;
        m_dbgPauseCb = nullptr;
        return fail(err::Syntax, msg);
    }
    lua_sethook(m_lua, ngLuaDebugHook, LUA_MASKLINE, 0);
    const int callStatus = lua_pcall(m_lua, 0, 0, 0);
    lua_sethook(m_lua, nullptr, 0, 0); // 无论成败都摘钩子
    m_dbgActive = false;
    m_dbgPaused = false;
    m_dbgPauseCb = nullptr;
    if (callStatus != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("脚本运行失败");
        lua_pop(m_lua, 1);
        return fail(err::Runtime, msg);
    }
    return Result<void>();
}

Result<QVariant> LuaEngine::evaluate(const QString &source)
{
    if (!m_lua)
        return fail(err::NoLua, QStringLiteral("Lua 状态创建失败"));

    const QByteArray src = source.toUtf8();
    const char *chunk = "=repl"; // 错误消息前缀 "repl:行号:"，控制台可定位

    // 先按表达式求值（`return (src)`），编译不过降级为整段语句。
    const QByteArray wrapped = "return (" + src + ")";
    int status = luaL_loadbuffer(m_lua, wrapped.constData(), wrapped.size(), chunk);
    if (status != LUA_OK)
    {
        lua_pop(m_lua, 1);
        status = luaL_loadbuffer(m_lua, src.constData(), src.size(), chunk);
    }
    if (status != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("编译失败");
        lua_pop(m_lua, 1);
        return fail(err::Syntax, msg);
    }

    if (lua_pcall(m_lua, 0, 1, 0) != LUA_OK)
    {
        const char *err = lua_tostring(m_lua, -1);
        const QString msg = err ? QString::fromUtf8(err) : QStringLiteral("运行失败");
        lua_pop(m_lua, 1);
        return fail(err::Runtime, msg);
    }

    QVariant result;
    if (lua_gettop(m_lua) > 0 && !lua_isnil(m_lua, -1))
        result = readVariant(m_lua, -1);
    lua_settop(m_lua, 0); // 清栈，REPL 下一条从干净栈开始
    return Result<QVariant>(result);
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

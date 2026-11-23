#include "FilterEngine.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "ErrorCodes.h"
using namespace nextpilot::plotscope::foundation;
namespace err = nextpilot::plotscope::foundation::err;

namespace nextpilot::plotscope::script
{

// ===== Lua <-> QJson 转换 =====

static QJsonValue readJson(lua_State *L, int idx);

static QJsonObject readJsonObject(lua_State *L, int idx)
{
    QJsonObject obj;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0)
    {
        const QString key = QString::fromUtf8(lua_tostring(L, -2));
        obj.insert(key, readJson(L, -1));
        lua_pop(L, 1);
    }
    return obj;
}

static QJsonValue readJson(lua_State *L, int idx)
{
    switch (lua_type(L, idx))
    {
    case LUA_TNIL:
        return QJsonValue::Null;
    case LUA_TBOOLEAN:
        return QJsonValue(lua_toboolean(L, idx) != 0);
    case LUA_TNUMBER:
        return QJsonValue(lua_tonumber(L, idx));
    case LUA_TSTRING:
        return QJsonValue(QString::fromUtf8(lua_tostring(L, idx)));
    case LUA_TTABLE:
    {
        // 判纯数组（1..n 连续且无其它键）。
        bool isArray = true;
        int n = 0;
        for (int i = 1; ; ++i)
        {
            lua_rawgeti(L, idx, i);
            const bool has = !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (!has)
                break;
            ++n;
        }
        lua_pushnil(L);
        while (lua_next(L, idx) != 0)
        {
            if (lua_type(L, -2) != LUA_TNUMBER)
                isArray = false;
            lua_pop(L, 1);
        }
        if (isArray)
        {
            QJsonArray arr;
            for (int i = 1; i <= n; ++i)
            {
                lua_rawgeti(L, idx, i);
                arr.append(readJson(L, -1));
                lua_pop(L, 1);
            }
            return QJsonArray(arr);
        }
        return QJsonObject(readJsonObject(L, idx));
    }
    default:
        return QJsonValue::Null;
    }
}

// QJsonValue -> Lua 值。
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

// 读表里的字符串字段（不弹栈）。
static QString fieldString(lua_State *L, int tableIdx, const char *key)
{
    lua_getfield(L, tableIdx, key);
    QString s;
    if (lua_isstring(L, -1))
        s = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);
    return s;
}

// ===== 看门狗：指令计数 hook =====

struct Watchdog
{
    int limit = 0;        // 最大 hook 次数
    int fuelUsed = 0;     // 已用 hook 次数
    bool tripped = false;
};

static void instructionHook(lua_State *L, lua_Debug *)
{
    // registry 里取 watchdog 指针。
    lua_getfield(L, LUA_REGISTRYINDEX, "ps.filter.watchdog");
    auto *wd = static_cast<Watchdog *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (wd && wd->limit > 0)
    {
        // 用计数触发：每次 hook 累加一个"燃料单位"，超过上限就报错中断。
        // LUA_MASKCOUNT 下每 count 条指令回调一次，这里把 count 设为 1 太慢；
        // 采用 LUA_MASKCOUNT + 每回调消耗固定燃料，limit 以"回调次数"计。
        ++wd->fuelUsed;
        if (wd->fuelUsed > wd->limit)
        {
            wd->tripped = true;
            luaL_error(L, "filter instruction budget exceeded");
        }
    }
}

// ===== FilterInstance 实现 =====

namespace
{
class LuaFilterInstance final : public FilterInstance
{
public:
    LuaFilterInstance(lua_State *L, int stateRef, bool stickyFailed, QString err)
        : m_lua(L), m_stateRef(stateRef), m_failed(stickyFailed), m_error(std::move(err)) {}

    ~LuaFilterInstance() override
    {
        if (m_lua)
        {
            luaL_unref(m_lua, LUA_REGISTRYINDEX, m_stateRef);
            // m_lua 由 holder 统一关闭（见下 FilterInstanceOwner）。
        }
    }

    FilterSampleResult calculate(double t, double v) override
    {
        FilterSampleResult out;
        if (m_failed)
        {
            out.suppress = true;
            return out;
        }
        if (!m_lua)
        {
            m_failed = true;
            m_error = QStringLiteral("Lua 状态不可用");
            out.suppress = true;
            return out;
        }

        // 重置燃料 + 装 hook。
        Watchdog *wd = watchdog();
        const int budget = m_budget;
        if (wd)
        {
            wd->fuelUsed = 0;
            wd->tripped = false;
            wd->limit = budget;
        }
        if (budget > 0)
            lua_sethook(m_lua, instructionHook, LUA_MASKCOUNT, 100);

        const int base = lua_gettop(m_lua);   // 调用前栈顶，用于精确恢复
        // 栈: [base...] state
        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_stateRef);
        // 栈: [base...] state, calculate（经 metatable __index 找到）
        lua_getfield(m_lua, -1, "calculate");
        if (!lua_isfunction(m_lua, -1))
        {
            lua_settop(m_lua, base);
            lua_sethook(m_lua, nullptr, 0, 0);
            m_failed = true;
            m_error = QStringLiteral("过滤器未定义 calculate");
            out.suppress = true;
            return out;
        }
        // 栈: [base...] state, calculate, self, t, v
        lua_pushvalue(m_lua, -2);        // self = state
        lua_pushnumber(m_lua, t);
        lua_pushnumber(m_lua, v);

        const int st = lua_pcall(m_lua, 3, LUA_MULTRET, 0);
        lua_sethook(m_lua, nullptr, 0, 0); // 卸 hook
        if (st != LUA_OK)
        {
            const char *e = lua_tostring(m_lua, -1);
            m_error = e ? QString::fromUtf8(e) : QStringLiteral("calculate 运行失败");
            lua_settop(m_lua, base);
            m_failed = true; // sticky
            out.suppress = true;
            return out;
        }
        // pcall 弹掉 func/self/t/v，压入返回值。
        // 栈: [base...] state, ret1..retN   （state 在 base+1）
        const int stateIndex = base + 1;
        const int nResults = lua_gettop(m_lua) - stateIndex;
        if (nResults >= 2 && lua_isnumber(m_lua, stateIndex + 1) && lua_isnumber(m_lua, stateIndex + 2))
        {
            // 显式时间形态 (t, v)：第一个返回值是数字时间。
            out.hasExplicitTime = true;
            out.outTime = lua_tonumber(m_lua, stateIndex + 1);
            out.value = lua_tonumber(m_lua, stateIndex + 2);
            out.suppress = false;
        }
        else if (nResults >= 1 && !lua_isnil(m_lua, stateIndex + 1))
        {
            out.value = lua_tonumber(m_lua, stateIndex + 1);
            out.suppress = false;
        }
        else
        {
            out.suppress = true; // 0 个返回值 / nil → 抑制
        }
        lua_settop(m_lua, base); // 恢复栈，不污染下次调用
        return out;
    }

    void reset() override
    {
        if (m_failed || !m_lua)
            return;
        const int base = lua_gettop(m_lua);
        lua_rawgeti(m_lua, LUA_REGISTRYINDEX, m_stateRef);
        lua_getfield(m_lua, -1, "reset");
        if (lua_isfunction(m_lua, -1))
        {
            lua_pushvalue(m_lua, -2); // self
            const int st = lua_pcall(m_lua, 1, 0, 0);
            if (st != LUA_OK)
            {
                const char *e = lua_tostring(m_lua, -1);
                m_error = e ? QString::fromUtf8(e) : QStringLiteral("reset 失败");
                m_failed = true;
            }
        }
        lua_settop(m_lua, base);
    }

    bool failed() const override { return m_failed; }
    QString error() const override { return m_error; }

    void setBudget(int b) { m_budget = b; }
    lua_State *lua() const { return m_lua; }

private:
    Watchdog *watchdog()
    {
        if (!m_lua)
            return nullptr;
        lua_getfield(m_lua, LUA_REGISTRYINDEX, "ps.filter.watchdog");
        auto *wd = static_cast<Watchdog *>(lua_touserdata(m_lua, -1));
        lua_pop(m_lua, 1);
        return wd;
    }

    lua_State *m_lua = nullptr;
    int m_stateRef = LUA_NOREF;
    bool m_failed = false;
    QString m_error;
    int m_budget = 200000;
};

// 拥有 lua_State 与 Watchdog 的包装，保证生命周期正确。
class OwnedFilterInstance final : public FilterInstance
{
public:
    explicit OwnedFilterInstance(std::unique_ptr<LuaFilterInstance> impl, lua_State *L, Watchdog *wd)
        : m_impl(std::move(impl)), m_lua(L), m_wd(wd) {}
    ~OwnedFilterInstance() override
    {
        // 必须先析构 m_impl：LuaFilterInstance::~ 里要 luaL_unref(m_lua)，
        // 而 C++ 成员按声明逆序析构，m_impl 是第一个声明的成员，会在析构
        // body 之后才析构——那样就会在 lua_close 之后 use-after-free。
        m_impl.reset();
        if (m_lua)
            lua_close(m_lua);
        delete m_wd;
    }
    FilterSampleResult calculate(double t, double v) override { return m_impl->calculate(t, v); }
    void reset() override { m_impl->reset(); }
    bool failed() const override { return m_impl->failed(); }
    QString error() const override { return m_impl->error(); }
    void setBudget(int b) { m_impl->setBudget(b); }
    bool setWatchdogLimit(int limit)
    {
        if (!m_impl || !m_impl->lua())
            return false;
        m_impl->setBudget(limit);
        return true;
    }

private:
    std::unique_ptr<LuaFilterInstance> m_impl;
    lua_State *m_lua = nullptr;
    Watchdog *m_wd = nullptr;
};

} // namespace

// ===== descriptor 读取 =====

static FilterParamType paramTypeFromString(const QString &s)
{
    if (s == QStringLiteral("integer")) return FilterParamType::Integer;
    if (s == QStringLiteral("boolean")) return FilterParamType::Boolean;
    if (s == QStringLiteral("enum")) return FilterParamType::Enum;
    if (s == QStringLiteral("string")) return FilterParamType::String;
    if (s == QStringLiteral("text")) return FilterParamType::Text;
    return FilterParamType::Number;
}

static FilterParamSpec readParamSpec(lua_State *L, int tableIdx)
{
    FilterParamSpec p;
    p.name = fieldString(L, tableIdx, "name");
    p.type = paramTypeFromString(fieldString(L, tableIdx, "type"));
    p.label = fieldString(L, tableIdx, "label");
    p.tooltip = fieldString(L, tableIdx, "tooltip");
    p.unit = fieldString(L, tableIdx, "unit");

    lua_getfield(L, tableIdx, "default");
    p.defaultValue = readJson(L, -1);
    lua_pop(L, 1);

    auto readNumField = [&](const char *key, bool &has, double &out) {
        lua_getfield(L, tableIdx, key);
        if (lua_isnumber(L, -1)) { has = true; out = lua_tonumber(L, -1); }
        lua_pop(L, 1);
    };
    readNumField("min", p.hasMin, p.min);
    readNumField("max", p.hasMax, p.max);
    readNumField("step", p.hasStep, p.step);
    lua_getfield(L, tableIdx, "decimals");
    if (lua_isnumber(L, -1)) { p.hasDecimals = true; p.decimals = lua_tointeger(L, -1); }
    lua_pop(L, 1);

    // values（enum 选项）。
    lua_getfield(L, tableIdx, "values");
    if (lua_istable(L, -1))
    {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            FilterEnumValue ev;
            if (lua_istable(L, -1))
            {
                lua_getfield(L, -1, "value");
                ev.value = readJson(L, -1);
                lua_pop(L, 1);
                ev.label = fieldString(L, -1, "label");
            }
            else
            {
                ev.value = readJson(L, -1);
                ev.label = ev.value.toString();
            }
            p.values.append(ev);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // visible_when = { param = "x", equals = <scalar> }
    lua_getfield(L, tableIdx, "visible_when");
    if (lua_istable(L, -1))
    {
        p.visibleWhenParam = fieldString(L, -1, "param");
        lua_getfield(L, -1, "equals");
        p.visibleWhenEquals = readJson(L, -1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return p;
}

static FilterDescriptor readDescriptor(lua_State *L, int tableIdx, const QByteArray &source, const QString &origin)
{
    FilterDescriptor d;
    d.id = fieldString(L, tableIdx, "id");
    d.name = fieldString(L, tableIdx, "name");
    if (d.name.isEmpty()) d.name = d.id;
    d.description = fieldString(L, tableIdx, "description");
    d.version = fieldString(L, tableIdx, "version");
    const QString out = fieldString(L, tableIdx, "output");
    if (!out.isEmpty()) d.outputKind = out;
    d.source = QString::fromUtf8(source);
    d.origin = origin;

    lua_getfield(L, tableIdx, "parameters");
    if (lua_istable(L, -1))
    {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            if (lua_istable(L, -1))
                d.parameters.append(readParamSpec(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return d;
}

// ===== FilterEngine =====

Result<QVector<FilterDescriptor>> FilterEngine::inspectModule(const QByteArray &source, const QString &origin) const
{
    lua_State *L = luaL_newstate();
    if (!L)
        return fail(err::NoLua, QStringLiteral("Lua 状态创建失败"));
    luaL_openlibs(L);

    const int st = luaL_loadbuffer(L, source.constData(), source.size(), "=filter_module");
    if (st != LUA_OK)
    {
        const char *e = lua_tostring(L, -1);
        const QString msg = e ? QString::fromUtf8(e) : QStringLiteral("过滤器模块编译失败");
        lua_close(L);
        return fail(err::Syntax, msg);
    }
    const int callSt = lua_pcall(L, 0, 1, 0);
    if (callSt != LUA_OK)
    {
        const char *e = lua_tostring(L, -1);
        const QString msg = e ? QString::fromUtf8(e) : QStringLiteral("过滤器模块运行失败");
        lua_close(L);
        return fail(err::Runtime, msg);
    }
    // 返回值：单个 class table 或 class 列表。
    QVector<FilterDescriptor> out;
    if (lua_istable(L, -1))
    {
        // 判断是 list（含 [1] 且是 table）还是单个 class（含 "id" 字段）。
        lua_getfield(L, -1, "id");
        const bool isSingleClass = lua_isstring(L, -1);
        lua_pop(L, 1);
        if (isSingleClass)
        {
            out.append(readDescriptor(L, -1, source, origin));
        }
        else
        {
            lua_pushnil(L);
            while (lua_next(L, -2) != 0)
            {
                if (lua_istable(L, -1))
                {
                    FilterDescriptor d = readDescriptor(L, -1, source, origin);
                    if (!d.id.isEmpty())
                        out.append(d);
                }
                lua_pop(L, 1);
            }
        }
    }
    lua_close(L);
    if (out.isEmpty())
        return fail(err::Shape, QStringLiteral("过滤器模块未声明任何 class"));
    return out;
}

Result<std::unique_ptr<FilterInstance>> FilterEngine::createInstance(const FilterDescriptor &klass,
                                                                      const QJsonObject &params) const
{
    lua_State *L = luaL_newstate();
    if (!L)
        return fail(err::NoLua, QStringLiteral("Lua 状态创建失败"));
    luaL_openlibs(L);

    // Watchdog 放 registry。
    auto *wd = new Watchdog();
    wd->limit = m_instructionBudget;
    lua_pushlightuserdata(L, wd);
    lua_setfield(L, LUA_REGISTRYINDEX, "ps.filter.watchdog");

    const int st = luaL_loadbuffer(L, klass.source.toUtf8().constData(), klass.source.toUtf8().size(), "=filter_instance");
    if (st != LUA_OK)
    {
        const char *e = lua_tostring(L, -1);
        const QString msg = e ? QString::fromUtf8(e) : QStringLiteral("过滤器源码编译失败");
        lua_close(L); delete wd;
        return fail(err::Syntax, msg);
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK)
    {
        const char *e = lua_tostring(L, -1);
        const QString msg = e ? QString::fromUtf8(e) : QStringLiteral("过滤器源码运行失败");
        lua_close(L); delete wd;
        return fail(err::Runtime, msg);
    }
    // 找到 id 匹配的 class。
    int classIdx = LUA_NOREF;
    auto findClass = [&](int idx) {
        QString id = fieldString(L, idx, "id");
        if (id == klass.id)
        {
            lua_pushvalue(L, idx);
            classIdx = luaL_ref(L, LUA_REGISTRYINDEX);
            return true;
        }
        return false;
    };
    lua_getfield(L, -1, "id");
    const bool single = lua_isstring(L, -1);
    lua_pop(L, 1);
    if (single)
    {
        findClass(lua_gettop(L));
    }
    else
    {
        lua_pushnil(L);
        while (lua_next(L, -2) != 0)
        {
            // 栈: [module] key value。value 在 -1。
            bool found = false;
            if (lua_istable(L, -1))
            {
                found = findClass(lua_gettop(L)); // 内部 push value + luaL_ref 弹出副本
            }
            lua_pop(L, 1); // 弹 value（key 留在栈上供 lua_next）
            if (found)
            {
                lua_pop(L, 1); // 弹 key
                break;
            }
        }
    }
    if (classIdx == LUA_NOREF)
    {
        lua_close(L); delete wd;
        return fail(err::NoClass, QStringLiteral("未找到过滤器 class: %1").arg(klass.id));
    }

    // 调 create(params)。契约（script-automation.md §2.7）：create 用点号定义，
    // 唯一参数就是 params 表；不能像 calculate 那样多压一个 self，否则脚本里
    // p.k / p.b / p.dt 全 nil（p 会拿到 class 表）。
    // 缺省参数先用 descriptor 的 default 补齐，保证"不传即用默认"。
    QJsonObject effective = params;
    for (const FilterParamSpec &spec : klass.parameters)
    {
        if (!effective.contains(spec.name) && !spec.defaultValue.isUndefined())
            effective.insert(spec.name, spec.defaultValue);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, classIdx);
    lua_getfield(L, -1, "create");
    if (!lua_isfunction(L, -1))
    {
        lua_close(L); delete wd;
        return fail(err::NoCreate, QStringLiteral("过滤器 %1 未定义 create").arg(klass.id));
    }
    pushJson(L, QJsonValue(effective));
    if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    {
        const char *e = lua_tostring(L, -1);
        const QString msg = e ? QString::fromUtf8(e) : QStringLiteral("create 失败");
        lua_close(L); delete wd;
        return fail(err::Create, msg);
    }
    // pcall 吃掉 create + params，压回 state；栈里还剩 class（在 state 下面）。
    // 栈顶是 state 实例，ref 它。
    const int stateRef = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_unref(L, LUA_REGISTRYINDEX, classIdx);
    lua_settop(L, 0); // 清掉残留的 class table，后续 calculate 从干净栈开始

    auto impl = std::make_unique<LuaFilterInstance>(L, stateRef, false, QString());
    impl->setBudget(m_instructionBudget);
    return std::unique_ptr<FilterInstance>(new OwnedFilterInstance(std::move(impl), L, wd));
}

std::shared_ptr<FilterEngine> makeLuaFilterEngine()
{
    return std::make_shared<FilterEngine>();
}

} // namespace nextpilot::plotscope::script

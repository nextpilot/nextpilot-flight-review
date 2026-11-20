#include "MarkerEngine.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace nextpilot::plotscope::foundation;

namespace nextpilot::plotscope::script
{

// ===== PlotMarker <-> JSON =====

namespace
{
QString kindName(MarkerKind k)
{
    switch (k)
    {
    case MarkerKind::Event:     return QStringLiteral("event");
    case MarkerKind::Region:    return QStringLiteral("region");
    case MarkerKind::ValueBand: return QStringLiteral("valueBand");
    }
    return QStringLiteral("event");
}
MarkerKind kindFromName(const QString &s)
{
    if (s == QStringLiteral("region")) return MarkerKind::Region;
    if (s == QStringLiteral("valueBand")) return MarkerKind::ValueBand;
    return MarkerKind::Event;
}
QString severityName(MarkerSeverity s)
{
    switch (s)
    {
    case MarkerSeverity::Info:     return QStringLiteral("info");
    case MarkerSeverity::Warning:  return QStringLiteral("warning");
    case MarkerSeverity::Error:    return QStringLiteral("error");
    case MarkerSeverity::Critical: return QStringLiteral("critical");
    }
    return QStringLiteral("info");
}
MarkerSeverity severityFromName(const QString &s)
{
    if (s == QStringLiteral("warning")) return MarkerSeverity::Warning;
    if (s == QStringLiteral("error")) return MarkerSeverity::Error;
    if (s == QStringLiteral("critical")) return MarkerSeverity::Critical;
    return MarkerSeverity::Info;
}
} // namespace

QJsonObject PlotMarker::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("kind"), kindName(kind));
    o.insert(QStringLiteral("tStart"), tStart);
    o.insert(QStringLiteral("tEnd"), tEnd);
    o.insert(QStringLiteral("valueLow"), valueLow);
    o.insert(QStringLiteral("valueHigh"), valueHigh);
    o.insert(QStringLiteral("hasValue"), hasValue);
    o.insert(QStringLiteral("severity"), severityName(severity));
    o.insert(QStringLiteral("label"), label);
    o.insert(QStringLiteral("description"), description);
    o.insert(QStringLiteral("category"), category);
    o.insert(QStringLiteral("status"), status);
    o.insert(QStringLiteral("color"), color);
    return o;
}

PlotMarker PlotMarker::fromJson(const QJsonObject &o)
{
    PlotMarker m;
    m.kind = kindFromName(o.value(QStringLiteral("kind")).toString());
    m.tStart = o.value(QStringLiteral("tStart")).toDouble();
    m.tEnd = o.value(QStringLiteral("tEnd")).toDouble();
    m.valueLow = o.value(QStringLiteral("valueLow")).toDouble();
    m.valueHigh = o.value(QStringLiteral("valueHigh")).toDouble();
    m.hasValue = o.value(QStringLiteral("hasValue")).toBool();
    m.severity = severityFromName(o.value(QStringLiteral("severity")).toString());
    m.label = o.value(QStringLiteral("label")).toString();
    m.description = o.value(QStringLiteral("description")).toString();
    m.category = o.value(QStringLiteral("category")).toString();
    m.status = o.value(QStringLiteral("status")).toString();
    m.color = o.value(QStringLiteral("color")).toString();
    return m;
}

QJsonArray markersToJson(const QVector<PlotMarker> &markers)
{
    QJsonArray arr;
    for (const PlotMarker &m : markers)
        arr.append(m.toJson());
    return arr;
}

QVector<PlotMarker> markersFromJson(const QJsonArray &arr)
{
    QVector<PlotMarker> out;
    for (const QJsonValue &v : arr)
        if (v.isObject())
            out.append(PlotMarker::fromJson(v.toObject()));
    return out;
}

double MarkerSeries::atTime(double t) const
{
    const int n = value.size();
    if (n == 0 || time.size() != n || !std::isfinite(t))
        return 0.0;
    if (t <= time.first()) return value.first();
    if (t >= time.last()) return value.last();
    int lo = 0, hi = n - 1;
    while (lo + 1 < hi)
    {
        const int mid = (lo + hi) / 2;
        if (time.at(mid) <= t) lo = mid; else hi = mid;
    }
    const double span = time.at(hi) - time.at(lo);
    const double frac = span > 0.0 ? (t - time.at(lo)) / span : 0.0;
    return value.at(lo) + frac * (value.at(hi) - value.at(lo));
}

// ===== 运行时上下文 =====

struct RunCtx
{
    const MarkerSeriesProvider *provider = nullptr;
    QVector<PlotMarker> emitted;
    int maxMarkers = 10000;
    bool overBudget = false;
    QString error;
};

static const char *kCtxKey = "ng.marker.ctx";
static const char *kSeriesMeta = "ng.marker.series";

static RunCtx *ctxOf(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kCtxKey);
    auto *c = static_cast<RunCtx *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return c;
}

static QString fieldString(lua_State *L, int idx, const char *key)
{
    lua_getfield(L, idx, key);
    QString s;
    if (lua_isstring(L, -1))
        s = QString::fromUtf8(lua_tostring(L, -1));
    lua_pop(L, 1);
    return s;
}

static bool fieldBool(lua_State *L, int idx, const char *key, bool dflt = false)
{
    lua_getfield(L, idx, key);
    bool v = dflt;
    if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}

static double fieldNum(lua_State *L, int idx, const char *key, double dflt = 0.0)
{
    lua_getfield(L, idx, key);
    double v = dflt;
    if (lua_isnumber(L, -1)) v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

// opts 表（若 idx 处不是 table 则沿用默认 severity）：填充 marker 公共字段。
static void applyOpts(lua_State *L, int idx, PlotMarker &m, MarkerSeverity dflt)
{
    if (!lua_istable(L, idx))
    {
        m.severity = dflt;
        return;
    }
    const QString sev = fieldString(L, idx, "severity");
    m.severity = sev.isEmpty() ? dflt : severityFromName(sev);
    m.label = fieldString(L, idx, "label");
    m.description = fieldString(L, idx, "description");
    m.category = fieldString(L, idx, "category");
    m.status = fieldString(L, idx, "status");
    m.color = fieldString(L, idx, "color");
}

static void emitMarker(lua_State *L, PlotMarker &&m)
{
    RunCtx *c = ctxOf(L);
    if (!c) return;
    if (c->emitted.size() >= c->maxMarkers)
    {
        c->overBudget = true;
        return;
    }
    c->emitted.push_back(std::move(m));
}

// ===== series(name) accessor =====

static const MarkerSeries *checkSeries(lua_State *L, int idx)
{
    void *ud = luaL_testudata(L, idx, kSeriesMeta);
    if (!ud) return nullptr;
    return *static_cast<const MarkerSeries **>(ud);
}

static int l_series_size(lua_State *L)
{
    const MarkerSeries *s = checkSeries(L, 1);
    lua_pushinteger(L, s ? static_cast<lua_Integer>(s->value.size()) : 0);
    return 1;
}

static int l_series_at(lua_State *L)
{
    const MarkerSeries *s = checkSeries(L, 1);
    const int i = static_cast<int>(luaL_checkinteger(L, 2)); // 0-based（PJ4 :at(i)）
    if (!s || i < 0 || i >= s->value.size())
    {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, s->time.at(i));
    lua_setfield(L, -2, "t");
    lua_pushnumber(L, s->value.at(i));
    lua_setfield(L, -2, "v");
    return 1;
}

static int l_series_atTime(lua_State *L)
{
    const MarkerSeries *s = checkSeries(L, 1);
    const double t = luaL_checknumber(L, 2);
    lua_pushnumber(L, s ? s->atTime(t) : 0.0);
    return 1;
}

static int l_series_index(lua_State *L)
{
    const char *key = luaL_checkstring(L, 2);
    if (std::strcmp(key, "size") == 0)    { lua_pushcfunction(L, l_series_size); return 1; }
    if (std::strcmp(key, "at") == 0)      { lua_pushcfunction(L, l_series_at); return 1; }
    if (std::strcmp(key, "atTime") == 0)  { lua_pushcfunction(L, l_series_atTime); return 1; }
    lua_pushnil(L);
    return 1;
}

static int l_series(lua_State *L)
{
    const QString name = QString::fromUtf8(luaL_checkstring(L, 1));
    RunCtx *c = ctxOf(L);
    const MarkerSeries *s = (c && c->provider) ? c->provider->get(name) : nullptr;
    auto **ud = static_cast<const MarkerSeries **>(
        lua_newuserdatauv(L, sizeof(const MarkerSeries *), 0));
    *ud = s;
    luaL_getmetatable(L, kSeriesMeta);
    lua_setmetatable(L, -2);
    return 1;
}

static int l_getSeriesNames(lua_State *L)
{
    lua_createtable(L, 0, 0);
    RunCtx *c = ctxOf(L);
    if (c && c->provider)
    {
        int i = 1;
        for (const QString &name : c->provider->names)
        {
            lua_pushstring(L, name.toUtf8().constData());
            lua_rawseti(L, -2, i++);
        }
    }
    return 1;
}

// ===== 标记创建原语 =====

static int l_createMarker(lua_State *L)
{
    int top = lua_gettop(L);
    // createMarker(x?, y?, opts?)：x/y 可为 nil。
    bool hasX = lua_isnumber(L, 1);
    bool hasY = top >= 2 && lua_isnumber(L, 2);
    int optsIdx = 0;
    if (top >= 1 && lua_istable(L, 1)) optsIdx = 1;
    else if (top >= 2 && lua_istable(L, 2)) optsIdx = 2;
    else if (top >= 3 && lua_istable(L, 3)) optsIdx = 3;

    PlotMarker m;
    if (hasX && hasY)
    {
        m.kind = MarkerKind::Event;
        m.hasValue = true;
        m.tStart = lua_tonumber(L, 1);
        m.valueLow = lua_tonumber(L, 2);
        applyOpts(L, optsIdx, m, MarkerSeverity::Info);
    }
    else if (hasX)
    {
        m.kind = MarkerKind::Event; // 竖线：带时间的事件，无值
        m.hasValue = false;
        m.tStart = lua_tonumber(L, 1);
        applyOpts(L, optsIdx, m, MarkerSeverity::Error);
    }
    else if (hasY)
    {
        m.kind = MarkerKind::ValueBand; // 横线：退化值带
        m.valueLow = m.valueHigh = lua_tonumber(L, 2);
        applyOpts(L, optsIdx, m, MarkerSeverity::Warning);
    }
    else
    {
        m.kind = MarkerKind::Event;
        m.tStart = 0.0;
        applyOpts(L, optsIdx, m, MarkerSeverity::Info);
    }
    emitMarker(L, std::move(m));
    return 0;
}

static int l_createVertical(lua_State *L)
{
    const double x = luaL_checknumber(L, 1);
    PlotMarker m;
    m.kind = MarkerKind::Event;
    m.hasValue = false;
    m.tStart = x;
    applyOpts(L, 2, m, MarkerSeverity::Error);
    emitMarker(L, std::move(m));
    return 0;
}

static int l_createHorizontal(lua_State *L)
{
    const double y = luaL_checknumber(L, 1);
    PlotMarker m;
    m.kind = MarkerKind::ValueBand;
    m.valueLow = m.valueHigh = y;
    applyOpts(L, 2, m, MarkerSeverity::Warning);
    emitMarker(L, std::move(m));
    return 0;
}

static int l_createPoint(lua_State *L)
{
    const double x = luaL_checknumber(L, 1);
    const double y = luaL_checknumber(L, 2);
    PlotMarker m;
    m.kind = MarkerKind::Event;
    m.hasValue = true;
    m.tStart = x;
    m.valueLow = y;
    applyOpts(L, 3, m, MarkerSeverity::Info);
    emitMarker(L, std::move(m));
    return 0;
}

static int l_createBand(lua_State *L)
{
    const double lo = luaL_checknumber(L, 1);
    const double hi = luaL_checknumber(L, 2);
    PlotMarker m;
    m.kind = MarkerKind::ValueBand;
    m.valueLow = lo;
    m.valueHigh = hi;
    applyOpts(L, 3, m, MarkerSeverity::Info);
    emitMarker(L, std::move(m));
    return 0;
}

static int l_startMarker(lua_State *L)
{
    const double t = luaL_checknumber(L, 1);
    // 用 upvalue 里的 RunCtx 记下开区间起点，塞进 registry 的一个临时字段。
    RunCtx *c = ctxOf(L);
    if (!c) return 0;
    lua_pushnumber(L, t);
    lua_setfield(L, LUA_REGISTRYINDEX, "ng.marker.openT");
    return 0;
}

static int l_closeMarker(lua_State *L)
{
    const double t = luaL_checknumber(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "ng.marker.openT");
    if (!lua_isnumber(L, -1))
    {
        lua_pop(L, 1);
        return 0;
    }
    const double t0 = lua_tonumber(L, -1);
    lua_pop(L, 1);
    PlotMarker m;
    m.kind = MarkerKind::Region;
    m.tStart = t0;
    m.tEnd = t;
    applyOpts(L, 2, m, MarkerSeverity::Info);
    emitMarker(L, std::move(m));
    return 0;
}

// 内部：when DSL 通过它发标记。kind="event"/"region"/"band"，参数见栈。
static int l_emitWhen(lua_State *L)
{
    // _emitWhen(kind, tStart, tEnd, low, high, opts)
    const QString kind = QString::fromUtf8(luaL_checkstring(L, 1));
    PlotMarker m;
    if (kind == QStringLiteral("region"))
    {
        m.kind = MarkerKind::Region;
        m.tStart = luaL_checknumber(L, 2);
        m.tEnd = luaL_checknumber(L, 3);
        applyOpts(L, 6, m, MarkerSeverity::Info);
    }
    else if (kind == QStringLiteral("band"))
    {
        m.kind = MarkerKind::ValueBand;
        m.valueLow = luaL_checknumber(L, 4);
        m.valueHigh = luaL_checknumber(L, 5);
        applyOpts(L, 6, m, MarkerSeverity::Info);
    }
    else // event
    {
        m.kind = MarkerKind::Event;
        m.hasValue = lua_isnumber(L, 4);
        m.tStart = luaL_checknumber(L, 2);
        if (m.hasValue) m.valueLow = lua_tonumber(L, 4);
        applyOpts(L, 6, m, MarkerSeverity::Info);
    }
    emitMarker(L, std::move(m));
    return 0;
}

// ===== 看门狗 =====

static void instructionHook(lua_State *L, lua_Debug *)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "ng.marker.wd");
    auto *fuel = static_cast<int *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (!fuel) return;
    if (--(*fuel) <= 0)
        luaL_error(L, "marker instruction budget exceeded");
}

// when DSL 的 Lua bootstrap：above/below/between/outside/equals/isTrue/changes
// + :backgroundColor/:event/:verticalLine。底层用 _emitWhen/_nativeSeriesValues。
// 一条规则脚本只执行一次；DSL 在这里定义成全局函数。
static const char *kBootstrap = R"LUA(
local function getSeries(s)
  if type(s) == "string" then return series(s) end
  return s
end

-- 取序列的 {t,v,...} 数组（走 accessor，避免暴露 userdata 内部）
local function values(s)
  local out = {}
  local n = s:size()
  for i = 0, n - 1 do
    local p = s:at(i)
    if p then out[#out + 1] = { t = p.t, v = p.v } end
  end
  return out
end

local function condFn(cond)
  if cond == nil then return function(v) return v ~= 0 and v == v end end -- 默认 truthy
  return cond
end

-- 条件构造器：返回 (v,t,prev,prev_t)->bool
function above(x)     return function(v) return v > x end end
function below(x)     return function(v) return v < x end end
function between(a,b) return function(v) return v >= a and v <= b end end
function outside(a,b) return function(v) return v < a or v > b end end
function equals(x)    return function(v) return v == x end end
local changesFns = setmetatable({}, {__mode = "k"})
function isTrue()     return function(v) return v ~= 0 and v == v end end
-- PJ4：changes 在首样本(prev=nil)不产出，从第二个有前值的样本起比较。
local function _changes(v, t, pv) return pv ~= nil and v ~= pv end
function changes()    local f = _changes; changesFns[f] = true; return f end

local function isFire(cond, pts, i)
  local p = pts[i]
  local prevV = i > 1 and pts[i-1].v or nil
  local prevT = i > 1 and pts[i-1].t or nil
  local ok = cond(p.v, p.t, prevV, prevT)
  if not ok then return false, p end
  if changesFns[cond] then return true, p end -- changes：每个变化点都触发
  -- 其它条件：起点首次成立才触发
  if i == 1 then return true, p end
  local prevOk = cond(prevV, prevT, i > 1 and pts[i-1].v or nil, nil)
  return not prevOk, p
end

local function severityOpt(opts)
  if type(opts) == "table" and opts.severity then return opts end
  local o = opts and { } or { }
  for k, v in pairs(type(opts) == "table" and opts or {}) do o[k] = v end
  o.severity = o.severity or nil
  return o
end

local function scanRuns(pts, cond)
  -- 返回连续满足 cond 的区间列表 {{i0,i1},...}（索引进 pts）
  local runs, cur = {}, nil
  for i, p in ipairs(pts) do
    local ok = cond(p.v, p.t, i > 1 and pts[i-1].v or nil, i > 1 and pts[i-1].t or nil)
    if ok then
      if not cur then cur = { i0 = i, i1 = i } else cur.i1 = i end
    else
      if cur then runs[#runs+1] = cur; cur = nil end
    end
  end
  if cur then runs[#runs+1] = cur end
  return runs
end

-- when(series|name, cond?):backgroundColor(color?, opts?) / :event(opts?) / :verticalLine(opts?)
local WhenMt = {}
WhenMt.__index = WhenMt

function WhenMt:backgroundColor(color, opts)
  local pts = values(self.s)
  local cond = condFn(self.cond)
  local o = type(opts) == "table" and opts or {}
  local minDur = o.minDuration or 0
  -- 无 cond（默认 truthy）且 color 是 table/function：每个等值 run 一个 region。
  for _, r in ipairs(scanRuns(pts, cond)) do
    local t0, t1 = pts[r.i0].t, pts[r.i1].t
    if (t1 - t0) >= minDur then
      local col = type(color) == "function" and color(pts[r.i0].v, t0) or color
      local oo = {}
      for k, v in pairs(o) do oo[k] = v end
      if type(col) == "table" then
        for k, v in pairs(col) do oo[k] = v end
      elseif type(col) == "string" then
        oo.color = col
      end
      oo.severity = oo.severity or "warning"
      _emitWhen("region", t0, t1, 0, 0, oo)
    end
  end
end

function WhenMt:event(opts)
  local pts = values(self.s)
  local cond = condFn(self.cond)
  local o = type(opts) == "table" and opts or {}
  local minGap = o.minGap or 0
  local every = o.every
  local lastT = -math.huge
  for i, p in ipairs(pts) do
    local ok = cond(p.v, p.t, i > 1 and pts[i-1].v or nil, i > 1 and pts[i-1].t or nil)
    local fire = every and ok or isFire(cond, pts, i)
    if fire and (p.t - lastT) >= minGap then
      local oo = {}
      for k, v in pairs(o) do oo[k] = v end
      if type(oo.label) == "function" then
        oo.label = oo.label(p.v, i > 1 and pts[i-1].v or nil)
      end
      oo.severity = oo.severity or "error"
      _emitWhen("event", p.t, 0, p.v, 0, oo)
      lastT = p.t
    end
  end
end

function WhenMt:verticalLine(opts)
  local pts = values(self.s)
  local cond = condFn(self.cond)
  local o = type(opts) == "table" and opts or {}
  local minGap = o.minGap or 0
  local every = o.every
  local lastT = -math.huge
  for i, p in ipairs(pts) do
    local ok = cond(p.v, p.t, i > 1 and pts[i-1].v or nil, i > 1 and pts[i-1].t or nil)
    local fire = every and ok or isFire(cond, pts, i)
    if fire and (p.t - lastT) >= minGap then
      local oo = {}
      for k, v in pairs(o) do oo[k] = v end
      if type(oo.label) == "function" then
        oo.label = oo.label(p.v, i > 1 and pts[i-1].v or nil)
      end
      oo.severity = oo.severity or "error"
      _emitWhen("event", p.t, 0, 0, 0, oo)
      lastT = p.t
    end
  end
end

function when(s, cond)
  return setmetatable({ s = getSeries(s), cond = cond }, WhenMt)
end
)LUA";

// ===== MarkerEngine =====

MarkerEngine::MarkerEngine() = default;
MarkerEngine::MarkerEngine(Limits limits) : m_limits(limits) {}
MarkerEngine::~MarkerEngine() = default;

Result<QVector<PlotMarker>> MarkerEngine::run(const QByteArray &code,
                                               const MarkerSeriesProvider &provider) const
{
    lua_State *L = luaL_newstate();
    if (!L)
        return fail(QStringLiteral("enomem"), QStringLiteral("无法创建 Lua 状态"));
    luaL_openlibs(L);

    RunCtx ctx;
    ctx.provider = &provider;
    ctx.maxMarkers = m_limits.maxMarkers;
    lua_pushlightuserdata(L, &ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, kCtxKey);

    // series metatable
    luaL_newmetatable(L, kSeriesMeta);
    lua_pushcfunction(L, l_series_index);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    // native 原语
    lua_pushcfunction(L, l_series);           lua_setglobal(L, "series");
    lua_pushcfunction(L, l_getSeriesNames);   lua_setglobal(L, "GetSeriesNames");
    lua_pushcfunction(L, l_createMarker);     lua_setglobal(L, "createMarker");
    lua_pushcfunction(L, l_createVertical);   lua_setglobal(L, "createVerticalMarker");
    lua_pushcfunction(L, l_createHorizontal); lua_setglobal(L, "createHorizontalMarker");
    lua_pushcfunction(L, l_createPoint);      lua_setglobal(L, "createPointMarker");
    lua_pushcfunction(L, l_createBand);       lua_setglobal(L, "createBandMarker");
    lua_pushcfunction(L, l_startMarker);      lua_setglobal(L, "startMarker");
    lua_pushcfunction(L, l_closeMarker);      lua_setglobal(L, "closeMarker");
    lua_pushcfunction(L, l_emitWhen);         lua_setglobal(L, "_emitWhen");

    // when DSL bootstrap
    if (luaL_dostring(L, kBootstrap) != LUA_OK)
    {
        const char *e = lua_tostring(L, -1);
        const QString msg = e ? QString::fromUtf8(e) : QStringLiteral("marker DSL 初始化失败");
        lua_close(L);
        return fail(QStringLiteral("ebootstrap"), msg);
    }

    // 看门狗
    int fuel = m_limits.instructionBudget;
    lua_pushlightuserdata(L, &fuel);
    lua_setfield(L, LUA_REGISTRYINDEX, "ng.marker.wd");
    lua_sethook(L, instructionHook, LUA_MASKCOUNT, 100);

    // 编译 + 执行规则
    if (luaL_loadbuffer(L, code.constData(), code.size(), "=marker.rule") != LUA_OK
        || lua_pcall(L, 0, 0, 0) != LUA_OK)
    {
        const char *e = lua_tostring(L, -1);
        QString msg = e ? QString::fromUtf8(e) : QStringLiteral("marker 规则执行失败");
        if (msg.contains(QStringLiteral("budget exceeded")))
            msg = QStringLiteral("marker 指令预算超限");
        lua_sethook(L, nullptr, 0, 0);
        lua_close(L);
        return fail(QStringLiteral("erule"), msg);
    }
    lua_sethook(L, nullptr, 0, 0);
    lua_close(L);

    if (ctx.overBudget)
        return fail(QStringLiteral("eoutput"), QStringLiteral("marker 输出数量超限"));
    if (!ctx.error.isEmpty())
        return fail(QStringLiteral("erule"), ctx.error);
    return Result<QVector<PlotMarker>>(std::move(ctx.emitted));
}

} // namespace nextpilot::plotscope::script

#include "LuaCalcApi.h"

#include "ScriptHostService.h"
#include "ps_kernel.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <QVector>

#include <cmath>
#include <cstdio>

namespace ps::scripting
{
namespace
{

// 往 Lua 堆里搬的点数上限：千万点一个 table 约 256 MB，且每次运算再产一份。
const int kMaxTablePoints = 5000000;

// 通道句柄判定：ps.app 造出来的 SourceRef 带 __src（源 id）与 name
bool isChannelRef(lua_State *L, int idx)
{
    if (!lua_istable(L, idx))
        return false;
    idx = lua_absindex(L, idx);
    lua_getfield(L, idx, "__src");
    const bool ok = lua_isnumber(L, -1);
    lua_pop(L, 1);
    return ok;
}

struct Series
{
    const double *t = nullptr;
    const double *v = nullptr;
    quint32 n = 0;
    QVector<double> bufT; // Lua 数组输入时持有拷贝，保证指针在 ABI 调用期间有效
    QVector<double> bufV;
};

// 通道句柄走内核指针（零拷贝）；Lua 数组拷贝一次，时间轴取 0..n-1。
bool readSeries(lua_State *L, int idx, Series *s, QString *err)
{
    idx = lua_absindex(L, idx);

    if (isChannelRef(L, idx))
    {
        HostService *svc = hostService();
        if (!svc)
        {
            *err = QStringLiteral("eneedhost: 数据面未注册（控制台外没有数据源）");
            return false;
        }
        lua_getfield(L, idx, "__src");
        const int id = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, idx, "name");
        const QString name = QString::fromUtf8(lua_tostring(L, -1));
        lua_pop(L, 1);

        HostService::SeriesView view;
        const Result r = svc->seriesView(id, name, &view);
        if (!r.ok)
        {
            *err = QStringLiteral("%1: %2").arg(r.code, r.message);
            return false;
        }
        s->t = view.time;
        s->v = view.value;
        s->n = view.count;
        return true;
    }

    if (!lua_istable(L, idx))
    {
        *err = QStringLiteral("eformat: 需要通道句柄或数字数组");
        return false;
    }

    const lua_Integer len = luaL_len(L, idx);
    if (len <= 0)
    {
        *err = QStringLiteral("eformat: 空数组");
        return false;
    }
    const int n = static_cast<int>(len);
    s->bufT.resize(n);
    s->bufV.resize(n);
    for (int i = 0; i < n; ++i)
    {
        lua_rawgeti(L, idx, static_cast<lua_Integer>(i) + 1);
        s->bufT[i] = static_cast<double>(i);
        s->bufV[i] = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
    }
    s->t = s->bufT.constData();
    s->v = s->bufV.constData();
    s->n = static_cast<quint32>(n);
    return true;
}

void pushNumbers(lua_State *L, const double *v, quint32 n)
{
    lua_newtable(L);
    for (quint32 i = 0; i < n; ++i)
    {
        lua_pushnumber(L, v[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
    }
}

// 返回 {t=..., v=...}
void pushSeriesTable(lua_State *L, const double *t, const double *v, quint32 n)
{
    lua_newtable(L);
    lua_pushstring(L, "t");
    if (t)
        pushNumbers(L, t, n);
    else
    {
        QVector<double> idx(static_cast<int>(n));
        for (int i = 0; i < static_cast<int>(n); ++i)
            idx[i] = i;
        pushNumbers(L, idx.constData(), n);
    }
    lua_settable(L, -3);
    lua_pushstring(L, "v");
    pushNumbers(L, v, n);
    lua_settable(L, -3);
}

bool tooLarge(lua_State *L, quint32 n, QString *err)
{
    if (static_cast<int>(n) <= kMaxTablePoints)
        return false;
    *err = QStringLiteral("etoolarge: %1 点超过 %2，先 decimate").arg(n).arg(kMaxTablePoints);
    return true;
}

double optNumber(lua_State *L, int tableIdx, const char *key, double def)
{
    if (!lua_istable(L, tableIdx))
        return def;
    tableIdx = lua_absindex(L, tableIdx);
    lua_getfield(L, tableIdx, key);
    const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

int optInt(lua_State *L, int tableIdx, const char *key, int def)
{
    return static_cast<int>(optNumber(L, tableIdx, key, def));
}

bool optBool(lua_State *L, int tableIdx, const char *key, bool def)
{
    if (!lua_istable(L, tableIdx))
        return def;
    tableIdx = lua_absindex(L, tableIdx);
    lua_getfield(L, tableIdx, key);
    const bool v = lua_isnil(L, -1) ? def : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

double inferFs(const Series &s)
{
    if (s.t && s.n > 1)
    {
        const double span = s.t[s.n - 1] - s.t[0];
        if (span > 0.0)
            return static_cast<double>(s.n - 1) / span;
    }
    return 1.0;
}

int fail(lua_State *L, const QString &err)
{
    lua_pushnil(L);
    lua_pushstring(L, err.toUtf8().constData());
    return 2;
}

// ---- ps.stats(v, {median=false}) ----
int l_stats(lua_State *L)
{
    Series s;
    QString err;
    if (!readSeries(L, 1, &s, &err))
        return fail(L, err);
    const bool withMedian = optBool(L, 2, "median", false);

    PsStats st{};
    char buf[256] = {0};
    if (ps_stats_series(s.v, s.n, withMedian ? 1 : 0, &st, buf, sizeof(buf)) != 0)
        return fail(L, QString::fromUtf8(buf));

    lua_newtable(L);
    lua_pushinteger(L, static_cast<lua_Integer>(st.count));
    lua_setfield(L, -2, "count");
    lua_pushnumber(L, st.mean);
    lua_setfield(L, -2, "mean");
    lua_pushnumber(L, st.std_dev);
    lua_setfield(L, -2, "std");
    lua_pushnumber(L, st.min);
    lua_setfield(L, -2, "min");
    lua_pushnumber(L, st.max);
    lua_setfield(L, -2, "max");
    lua_pushnumber(L, st.rms);
    lua_setfield(L, -2, "rms");
    if (withMedian)
    {
        lua_pushnumber(L, st.median);
        lua_setfield(L, -2, "median");
    }
    return 1;
}

// ---- ps.rms(v) ----
int l_rms(lua_State *L)
{
    Series s;
    QString err;
    if (!readSeries(L, 1, &s, &err))
        return fail(L, err);
    PsStats st{};
    char buf[256] = {0};
    if (ps_stats_series(s.v, s.n, 0, &st, buf, sizeof(buf)) != 0)
        return fail(L, QString::fromUtf8(buf));
    lua_pushnumber(L, st.rms);
    return 1;
}

// ---- ps.fft(v, {fs=, window=1}) → {f=, mag=} ----
int l_fft(lua_State *L)
{
    Series s;
    QString err;
    if (!readSeries(L, 1, &s, &err))
        return fail(L, err);
    const double fs = optNumber(L, 2, "fs", inferFs(s));
    const int window = optInt(L, 2, "window", 1);

    char buf[256] = {0};
    uint32_t m = 0;
    ps_fft_series(s.v, s.n, fs, window, nullptr, nullptr, 0, &m, buf, sizeof(buf));
    if (m == 0)
        return fail(L, QString::fromUtf8(buf[0] ? buf : "eformat: FFT 无法计算"));

    QVector<double> f(static_cast<int>(m));
    QVector<double> mag(static_cast<int>(m));
    if (ps_fft_series(s.v, s.n, fs, window, f.data(), mag.data(), m, &m, buf, sizeof(buf)) != 0)
        return fail(L, QString::fromUtf8(buf));

    lua_newtable(L);
    lua_pushstring(L, "f");
    pushNumbers(L, f.constData(), m);
    lua_settable(L, -3);
    lua_pushstring(L, "mag");
    pushNumbers(L, mag.constData(), m);
    lua_settable(L, -3);
    return 1;
}

// ---- ps.psd(v, {fs=, nfft=256, overlap=0.5, window=1}) → {f=, psd=} ----
int l_psd(lua_State *L)
{
    Series s;
    QString err;
    if (!readSeries(L, 1, &s, &err))
        return fail(L, err);
    const double fs = optNumber(L, 2, "fs", inferFs(s));
    const int nfft = optInt(L, 2, "nfft", 256);
    const double overlap = optNumber(L, 2, "overlap", 0.5);
    const int window = optInt(L, 2, "window", 1);

    char buf[256] = {0};
    uint32_t m = 0;
    ps_psd_series(s.v, s.n, fs, static_cast<uint32_t>(nfft), overlap, window, nullptr, nullptr, 0, &m,
                  buf, sizeof(buf));
    if (m == 0)
        return fail(L, QString::fromUtf8(buf[0] ? buf : "eformat: PSD 无法计算"));

    QVector<double> f(static_cast<int>(m));
    QVector<double> psd(static_cast<int>(m));
    if (ps_psd_series(s.v, s.n, fs, static_cast<uint32_t>(nfft), overlap, window, f.data(),
                      psd.data(), m, &m, buf, sizeof(buf)) != 0)
        return fail(L, QString::fromUtf8(buf));

    lua_newtable(L);
    lua_pushstring(L, "f");
    pushNumbers(L, f.constData(), m);
    lua_settable(L, -3);
    lua_pushstring(L, "psd");
    pushNumbers(L, psd.constData(), m);
    lua_settable(L, -3);
    return 1;
}

// ---- ps.filter(v, {kind="lowpass", fc, fs, order=4, bandwidth=0.1}) → {t=, v=} ----
int l_filter(lua_State *L)
{
    Series s;
    QString err;
    if (!readSeries(L, 1, &s, &err))
        return fail(L, err);
    if (tooLarge(L, s.n, &err))
        return fail(L, err);

    const double fs = optNumber(L, 2, "fs", inferFs(s));
    const double fc = optNumber(L, 2, "fc", 10.0);
    const int order = optInt(L, 2, "order", 4);
    const double bandwidth = optNumber(L, 2, "bandwidth", 0.1);

    int kind = 0;
    if (lua_istable(L, 2))
    {
        lua_getfield(L, 2, "kind");
        if (lua_isstring(L, -1))
        {
            const QString k = QString::fromUtf8(lua_tostring(L, -1));
            if (k == QLatin1String("highpass"))
                kind = 1;
            else if (k == QLatin1String("bandpass"))
                kind = 2;
        }
        lua_pop(L, 1);
    }

    char buf[256] = {0};
    QVector<double> out(static_cast<int>(s.n));
    uint32_t on = 0;
    const int rc = ps_iir_series(s.v, s.n, fs, fc, static_cast<uint32_t>(order), kind, bandwidth,
                                 out.data(), static_cast<uint32_t>(out.size()), &on, buf,
                                 sizeof(buf));
    if (rc != 0)
        return fail(L, QString::fromUtf8(buf));
    out.resize(static_cast<int>(on));
    pushSeriesTable(L, s.t, out.constData(), on);
    return 1;
}

// ---- ps.decimate(v, factor) → {t=, v=} ----
int l_decimate(lua_State *L)
{
    Series s;
    QString err;
    if (!readSeries(L, 1, &s, &err))
        return fail(L, err);
    const int factor = static_cast<int>(luaL_optinteger(L, 2, 1));

    char buf[256] = {0};
    QVector<double> ot(static_cast<int>(s.n));
    QVector<double> ov(static_cast<int>(s.n));
    uint32_t on = 0;
    const int rc = ps_decimate_series(s.t, s.v, s.n, static_cast<uint32_t>(factor), ot.data(),
                                      ov.data(), static_cast<uint32_t>(ot.size()), &on, buf,
                                      sizeof(buf));
    if (rc != 0)
        return fail(L, QString::fromUtf8(buf));
    ot.resize(static_cast<int>(on));
    ov.resize(static_cast<int>(on));
    if (tooLarge(L, on, &err))
        return fail(L, err);
    pushSeriesTable(L, ot.constData(), ov.constData(), on);
    return 1;
}

// 复用 PJ4 过滤器链（derivative / integral / moving_average 都在里面）。
// 成功时压入 {t,v} 并返回空串；失败返回错误文本，栈不变。
QString runFilter(lua_State *L, const char *filterId, const char *paramsJson)
{
    Series s;
    QString err;
    if (!readSeries(L, 1, &s, &err))
        return err;
    if (tooLarge(L, s.n, &err))
        return err;

    char buf[256] = {0};
    QVector<double> ot(static_cast<int>(s.n));
    QVector<double> ov(static_cast<int>(s.n));
    uint32_t on = 0;
    const int rc =
        ps_filter_apply_series(filterId, paramsJson, s.t, s.v, s.n, ot.data(), ov.data(),
                               static_cast<uint32_t>(ot.size()), &on, buf, sizeof(buf));
    if (rc != 0)
        return QString::fromUtf8(buf);

    ot.resize(static_cast<int>(on));
    ov.resize(static_cast<int>(on));
    pushSeriesTable(L, ot.constData(), ov.constData(), on);
    return {};
}

int l_movingAvg(lua_State *L)
{
    const int window = static_cast<int>(luaL_optinteger(L, 2, 100));
    char params[64];
    std::snprintf(params, sizeof(params), "{\"window\":%d}", window);
    const QString err = runFilter(L, "moving_average", params);
    if (err.isEmpty())
        return 1;
    return fail(L, err);
}

int l_derivative(lua_State *L)
{
    const QString err = runFilter(L, "derivative", "{}");
    if (err.isEmpty())
        return 1;
    return fail(L, err);
}

int l_integral(lua_State *L)
{
    const QString err = runFilter(L, "integral", "{}");
    if (err.isEmpty())
        return 1;
    return fail(L, err);
}

} // namespace

void installCalcApi(lua_State *L)
{
    lua_getglobal(L, "ps");
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "ps");
    }
    struct Entry
    {
        const char *name;
        lua_CFunction fn;
    };
    const Entry entries[] = {
        {"stats", l_stats},       {"rms", l_rms},
        {"fft", l_fft},           {"psd", l_psd},
        {"filter", l_filter},     {"decimate", l_decimate},
        {"movingAvg", l_movingAvg}, {"derivative", l_derivative},
        {"integral", l_integral},
    };
    for (const Entry &e : entries)
    {
        lua_pushcfunction(L, e.fn);
        lua_setfield(L, -2, e.name);
    }
    lua_pop(L, 1); // 弹掉 ps
}

} // namespace ps::scripting

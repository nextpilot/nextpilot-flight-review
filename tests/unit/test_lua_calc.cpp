// 计算库 Lua 面：数组输入下的数值与表结构。通道句柄路径在 CLI 冒烟里覆盖。

#include "scripting/ScriptConsole.h"

#include <QString>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ps::scripting::ScriptConsole;

namespace
{
int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

QString evalText(ScriptConsole &c, const QString &code)
{
    const auto r = c.eval(code);
    return r.text;
}

double evalNum(ScriptConsole &c, const QString &code)
{
    return evalText(c, code).toDouble();
}
} // namespace

int main()
{
    ScriptConsole c;

    // 正弦：10 Hz @ 1000 Hz，1000 点正好 10 个整周期，峰值不泄漏
    // 1024 点 10 Hz @ 1024 Hz：2 的幂 + 整周期，FFT 不补零也不泄漏
    check(c.eval(QStringLiteral(
                     "w = {} for i=0,1023 do w[i+1] = math.sin(2*math.pi*10*i/1024) end"))
              .kind != ScriptConsole::Result::Error,
          "构造正弦数组");

    // ---- stats ----
    check(std::fabs(evalNum(c, QStringLiteral("ps.stats({1,2,3,4}).mean")) - 2.5) < 1e-12,
          "stats mean 2.5");
    check(std::fabs(evalNum(c, QStringLiteral("ps.stats({1,2,3,4}).count")) - 4) < 1e-12,
          "stats count 4");
    check(std::fabs(evalNum(c, QStringLiteral("ps.stats({1,2,3,4}).std")) - 1.11803) < 1e-4,
          "stats std 1.118");
    check(std::fabs(evalNum(c, QStringLiteral("ps.stats({1,2,3,4}).rms")) - 2.73861) < 1e-4,
          "stats rms 2.739");
    check(std::fabs(evalNum(c, QStringLiteral("ps.stats({1,2,3,4}).max")) - 4) < 1e-12,
          "stats max 4");

    // median 单独开关：4 点中位数 2.5
    check(std::fabs(evalNum(c, QStringLiteral("ps.stats({1,2,3,4},{median=true}).median")) - 2.5) <
              1e-12,
          "stats median 2.5");

    // ---- rms ----
    check(std::fabs(evalNum(c, QStringLiteral("ps.rms({1,2,3,4})")) - 2.73861) < 1e-4, "rms 2.739");

    // ---- fft：峰值频率与幅值 ----
    evalText(c, QStringLiteral("spec = ps.fft(w, {fs=1024})"));
    check(evalText(c, QStringLiteral("type(spec)")).contains(QLatin1String("table")), "fft 返回表");
    evalText(c, QStringLiteral("b = 1; for i=2,#spec.f do if spec.mag[i] > spec.mag[b] then b = i end end"));
    check(std::fabs(evalNum(c, QStringLiteral("spec.f[b]")) - 10.0) < 1e-9, "fft 峰值 10 Hz");
    check(std::fabs(evalNum(c, QStringLiteral("spec.mag[b]")) - 1.0) < 0.02, "fft 幅值 1");
    check(std::fabs(evalNum(c, QStringLiteral("#spec.f")) - 513) < 1e-9, "fft 单边 501 bin");

    // ---- psd ----
    evalText(c, QStringLiteral("pd = ps.psd(w, {fs=1024, nfft=512})"));
    evalText(c, QStringLiteral("pb = 1; for i=2,#pd.f do if pd.psd[i] > pd.psd[pb] then pb = i end end"));
    check(std::fabs(evalNum(c, QStringLiteral("pd.f[pb]")) - 10.0) < 1e-9, "psd 峰值 10 Hz");

    // ---- filter / decimate / 过滤器链 ----
    evalText(c, QStringLiteral("lp = ps.filter(w, {kind='lowpass', fc=50, fs=1024, order=4})"));
    check(std::fabs(evalNum(c, QStringLiteral("#lp.v")) - 1024) < 1e-9, "filter 输出原长");
    check(std::fabs(evalNum(c, QStringLiteral("#lp.t")) - 1024) < 1e-9, "filter 时间轴同步");

    evalText(c, QStringLiteral("dc = ps.decimate(w, 8)"));
    check(std::fabs(evalNum(c, QStringLiteral("#dc.v")) - 128) < 1e-9, "decimate 1024→128");

    evalText(c, QStringLiteral("mv = ps.movingAvg(w, 10)"));
    check(std::fabs(evalNum(c, QStringLiteral("#mv.v")) - 1024) < 1e-9, "movingAvg 原长");

    evalText(c, QStringLiteral("dv = ps.derivative(w)"));
    check(std::fabs(evalNum(c, QStringLiteral("#dv.v")) - 1023) < 1e-9, "derivative 少一点");

    // ---- 错误路径 ----
    const auto empty = c.eval(QStringLiteral("ps.stats({})"));
    check(empty.kind == ScriptConsole::Result::Output, "空数组返回 nil");
    check(empty.text.contains(QLatin1String("eformat")), "空数组报 eformat");

    const auto bad = c.eval(QStringLiteral("ps.stats(42)"));
    check(bad.text.contains(QLatin1String("eformat")), "非数组非句柄报 eformat");

    // 入口全部挂在 ps 上
    check(evalText(c, QStringLiteral("type(ps.stats)")).contains(QLatin1String("function")), "ps.stats");
    check(evalText(c, QStringLiteral("type(ps.fft)")).contains(QLatin1String("function")), "ps.fft");
    check(evalText(c, QStringLiteral("type(ps.psd)")).contains(QLatin1String("function")), "ps.psd");
    check(evalText(c, QStringLiteral("type(ps.filter)")).contains(QLatin1String("function")), "ps.filter");
    check(evalText(c, QStringLiteral("type(ps.decimate)")).contains(QLatin1String("function")),
          "ps.decimate");

    if (failures == 0)
        std::printf("lua calc ok\n");
    return failures == 0 ? 0 : 1;
}

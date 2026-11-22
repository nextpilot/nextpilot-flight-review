#include "fastplot/ScaleDiv.h"

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::fastplot
{

namespace
{

// Heckbert《Nice Numbers for Graph Labels》的取整规则。
double niceNum(double range, bool round)
{
    if (range <= 0.0)
        return 1.0;
    const double exponent = std::floor(std::log10(range));
    const double fraction = range / std::pow(10.0, exponent);
    double niceFraction = 0.0;
    if (round)
        niceFraction = fraction < 1.5 ? 1.0 : fraction < 3.0 ? 2.0 : fraction < 7.0 ? 5.0 : 10.0;
    else
        niceFraction = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0;
    return niceFraction * std::pow(10.0, exponent);
}

} // namespace

ScaleDiv ScaleDiv::linear(double lo, double hi, int maxMajorTicks)
{
    ScaleDiv div;
    if (lo > hi)
        std::swap(lo, hi);
    if (lo == hi)
    {
        const double pad = std::fabs(lo) > 1.0 ? std::fabs(lo) * 0.05 : 1.0;
        lo -= pad;
        hi += pad;
    }
    if (maxMajorTicks < 2)
        maxMajorTicks = 2;

    // 先按"刻度数量"求步长，再向外扩到整步长的边界，保证首尾都有主刻度。
    const double step = niceNum((hi - lo) / (maxMajorTicks - 1), true);
    div.majorStep = step;
    div.lower = std::floor(lo / step) * step;
    div.upper = std::ceil(hi / step) * step;

    // 次刻度：默认每主刻度 5 等分；步长太小（相对量程）时退回 2 等分，避免挤成一片。
    div.minorPerMajor = 5;
    const double minorStep = step / div.minorPerMajor;
    const int majorCount = static_cast<int>(std::llround((div.upper - div.lower) / step)) + 1;
    if (majorCount * div.minorPerMajor > 400)
        div.minorPerMajor = 2;

    auto pushTick = [&div](double v, bool major) {
        Tick t;
        t.value = v;
        t.major = major;
        t.label = major ? ScaleDiv::formatValue(v, div.majorStep) : QString();
        div.ticks.push_back(t);
    };

    // 主刻度
    const int steps = static_cast<int>(std::llround((div.upper - div.lower) / div.majorStep));
    for (int i = 0; i <= steps; ++i)
    {
        double v = div.lower + i * div.majorStep;
        // 浮点累计误差归整到步长网格
        v = std::round(v / div.majorStep) * div.majorStep;
        if (std::fabs(v) < div.majorStep * 1e-9)
            v = 0.0;   // 避免 -0
        pushTick(v, true);
    }

    // 次刻度：插在主刻度之间
    const double mStep = div.majorStep / div.minorPerMajor;
    for (int i = 0; i < steps; ++i)
    {
        const double base = div.lower + i * div.majorStep;
        for (int k = 1; k < div.minorPerMajor; ++k)
            pushTick(base + k * mStep, false);
    }

    std::sort(div.ticks.begin(), div.ticks.end(),
              [](const Tick &a, const Tick &b) { return a.value < b.value; });
    return div;
}

int ScaleDiv::nearestMajorIndex(double value) const
{
    int best = -1;
    double bestDist = 0.0;
    for (int i = 0; i < ticks.size(); ++i)
    {
        if (!ticks.at(i).major)
            continue;
        const double d = std::fabs(ticks.at(i).value - value);
        if (best < 0 || d < bestDist)
        {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

QString ScaleDiv::formatValue(double value, double step)
{
    const double a = std::fabs(value);
    if (a >= 1e7 || (a > 0.0 && a < 1e-4))
        return QString::number(value, 'g', 6);

    int decimals = 0;
    if (step > 0.0)
        decimals = static_cast<int>(std::ceil(-std::log10(step))) + 1;
    decimals = std::clamp(decimals, 0, 9);

    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.')))
    {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    if (text == QLatin1String("-0"))
        text = QStringLiteral("0");
    return text;
}

} // namespace nextpilot::plotscope::fastplot

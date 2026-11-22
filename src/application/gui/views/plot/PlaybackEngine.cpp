#include "PlaybackEngine.h"

#include <algorithm>
#include <cmath>

namespace nextpilot::plotscope::gui
{

namespace
{
// ~30 Hz：视频/曲线读数都不需要 60 Hz，减半 tick 就减半所有订阅者的下游绘制量。
// 推进按墙钟 elapsed 计算，与 tick 次数无关，故速率不受 tick 频率影响。
constexpr int kTickIntervalMs = 33;
} // namespace

PlaybackEngine::PlaybackEngine(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(kTickIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &PlaybackEngine::onTick);
}

PlaybackEngine::~PlaybackEngine() = default;

bool PlaybackEngine::finite(double v) const
{
    return std::isfinite(v);
}

double PlaybackEngine::snapRate(double rate)
{
    if (!std::isfinite(rate))
        return 1.0;
    const double snapped = std::round(rate / kRateStep) * kRateStep;
    return std::clamp(snapped, kMinRate, kMaxRate);
}

double PlaybackEngine::clampTickTime(double next, double min, double max,
                                     bool holdAtMax, bool looping, bool *reachedEnd)
{
    if (reachedEnd)
        *reachedEnd = false;
    // 钉在实时尾：不按墙钟自由前进，直接停在 max；循环开关被忽略。
    if (holdAtMax)
        return max;
    if (next > max)
    {
        if (looping)
        {
            const double span = max - min;
            return span > 0.0 && std::isfinite(next) ? min + std::fmod(next - min, span) : min;
        }
        if (reachedEnd)
            *reachedEnd = true;
        return max;
    }
    if (next < min)
        return min;
    return next;
}

double PlaybackEngine::clamped(double t) const
{
    return std::clamp(t, m_min, m_max);
}

void PlaybackEngine::applyRange(double min, double max)
{
    m_min = min;
    m_max = max;
    emit rangeChanged(min, max);
}

void PlaybackEngine::setRange(double min, double max)
{
    if (!finite(min) || !finite(max))
        return;
    applyRange(min, max);
    const double c = clamped(m_time);
    if (c != m_time)
    {
        m_time = c;
        emit currentTimeChanged(m_time);
    }
}

void PlaybackEngine::setRangeAndCurrentTime(double min, double max, double t)
{
    if (!finite(min) || !finite(max) || !finite(t))
        return;
    applyRange(min, max);
    const double c = clamped(t);
    if (c != m_time)
    {
        m_time = c;
        emit currentTimeChanged(m_time);
    }
}

void PlaybackEngine::setCurrentTime(double t)
{
    if (!finite(t))
        return;
    const double c = clamped(t);
    if (c != m_time)
    {
        m_time = c;
        emit currentTimeChanged(m_time);
    }
}

void PlaybackEngine::setRate(double rate)
{
    if (!std::isfinite(rate))
        return;
    const double snapped = snapRate(rate);
    if (snapped != m_rate)
    {
        m_rate = snapped;
        emit rateChanged(m_rate);
    }
}

void PlaybackEngine::setStep(double step)
{
    if (!std::isfinite(step))
        return;
    m_step = std::clamp(std::abs(step), 0.001, 100.0);
}

void PlaybackEngine::setLooping(bool looping)
{
    if (looping == m_looping)
        return;
    m_looping = looping;
    emit loopingChanged(looping);
}

void PlaybackEngine::setHoldAtMax(bool hold)
{
    m_hold = hold;
}

void PlaybackEngine::play()
{
    if (m_playing)
        return;
    // 游标已在终点时按播放 = 从头重放（钉尾/循环除外）。
    if (!m_hold && !m_looping && m_max > m_min && m_time >= m_max)
    {
        m_time = m_min;
        emit currentTimeChanged(m_time);
    }
    m_playing = true;
    m_elapsed.restart();
    m_timer.start();
    emit playingChanged(m_playing);
}

void PlaybackEngine::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_timer.stop();
    emit playingChanged(m_playing);
}

void PlaybackEngine::togglePlay()
{
    if (m_playing)
        pause();
    else
        play();
}

void PlaybackEngine::onTick()
{
    const qint64 dtMs = m_elapsed.restart();
    const double dt = static_cast<double>(dtMs) * 0.001;
    const double rawNext = m_time + dt * m_rate;

    bool reachedEnd = false;
    const double next = clampTickTime(rawNext, m_min, m_max, m_hold, m_looping, &reachedEnd);
    if (reachedEnd)
        pause();
    if (next != m_time)
    {
        m_time = next;
        emit currentTimeChanged(m_time);
    }
}

} // namespace nextpilot::plotscope::gui

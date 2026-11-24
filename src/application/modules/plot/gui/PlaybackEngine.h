#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace nextpilot::plotscope::gui
{

// 回放引擎：会话级时间游标 + 播放/暂停/循环/速率/步进。
//
// 为什么要一个独立引擎（而不是让时间条自己持游标）：
//   - 底部时间条、所有画布的播放头、脚本/CLI 的 playback.* 意图都读同一个游标；
//   - 谁推进游标都必须走这里，才能一次广播给所有订阅者，避免各画布各自计时漂移。
//
// 时间语义：秒（显示相对时间）。数据流式追加时 range_max 会增长；holdAtMax
// 用于把游标钉在最新样本（live 语义），此时忽略循环与墙钟推进。
//
// 放在 GUI 层而非 AppContext：它持 QTimer、是展示态，不进 headless 上下文。
class PlaybackEngine : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;

    PlaybackEngine(const PlaybackEngine &) = delete;
    PlaybackEngine &operator=(const PlaybackEngine &) = delete;

    double currentTime() const { return m_time; }
    double rangeMin() const { return m_min; }
    double rangeMax() const { return m_max; }
    double rate() const { return m_rate; }
    double step() const { return m_step; }
    bool isPlaying() const { return m_playing; }
    bool isLooping() const { return m_looping; }
    bool holdsAtMax() const { return m_hold; }

    static constexpr double kMinRate = 0.1;
    static constexpr double kMaxRate = 10.0;
    static constexpr double kRateStep = 0.1;

    // 速度吸附：四舍五入到 kRateStep 再夹到 [min,max]。
    static double snapRate(double rate);

    // 纯函数：给定这一步要到的时间，返回夹取后的游标位置。
    // reachedEnd 非空时，仅在"到终点且不循环不钉尾"时置 true（调用方据此暂停）。
    static double clampTickTime(double next, double min, double max,
                                bool holdAtMax, bool looping, bool *reachedEnd = nullptr);

public slots:
    void setRange(double min, double max);
    void setRangeAndCurrentTime(double min, double max, double t);
    void setCurrentTime(double t);
    void setRate(double rate);
    void setStep(double step);
    void setLooping(bool looping);
    void setHoldAtMax(bool hold);
    void play();
    void pause();
    void togglePlay();

signals:
    void currentTimeChanged(double t);
    void rangeChanged(double min, double max);
    void playingChanged(bool playing);
    void rateChanged(double rate);
    void loopingChanged(bool looping);

private slots:
    void onTick();

private:
    bool finite(double v) const;
    void applyRange(double min, double max);
    double clamped(double t) const;

    double m_time = 0.0;
    double m_min = 0.0;
    double m_max = 1.0;
    double m_rate = 1.0;
    double m_step = 1.0;
    bool m_playing = false;
    bool m_looping = false;
    bool m_hold = false;

    QTimer m_timer;
    QElapsedTimer m_elapsed;
};

} // namespace nextpilot::plotscope::gui

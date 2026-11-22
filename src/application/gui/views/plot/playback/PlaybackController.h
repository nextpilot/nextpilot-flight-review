#pragma once

#include <QObject>

class QWidget;

namespace nextpilot::plotscope::gui
{

class PlaybackEngine;
class TimelineWidget;

// 回放控制：持有引擎与底部时间条，并把播放头变化广播给画布。
// 画布区不再直接摸引擎/时间条，播放状态只有一个属主。
class PlaybackController : public QObject
{
    Q_OBJECT // NOLINT
public:
    explicit PlaybackController(QObject *parent = nullptr);
    ~PlaybackController() override;

    PlaybackEngine *engine() const { return m_engine; }
    // 时间条是控制器的一部分，宿主只负责把它装进布局。
    QWidget *timelineWidget() const;
    // 有数据时把回放区间设为 [lo,hi] 并亮出时间条。
    void setRange(double lo, double hi);

signals:
    void playheadChanged(double t);

private:
    PlaybackEngine *m_engine = nullptr;
    TimelineWidget *m_timeline = nullptr;
};

} // namespace nextpilot::plotscope::gui

#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QHBoxLayout;

namespace nextpilot::plotscope::gui
{

class PlaybackEngine;

// 底部时间条（对齐 PJ4 TimelineWidget）：一个只读引擎的视图。
// 手势（播放/暂停/循环/拖动/速度/步进）调用引擎；引擎信号回写显示。
// 所有画布的播放头由 CanvasWorkspace 订阅同一引擎的 currentTimeChanged 同步。
class TimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TimelineWidget(PlaybackEngine *engine, QWidget *parent = nullptr);
    ~TimelineWidget() override;

    void setEngine(PlaybackEngine *engine);

private slots:
    void onTimeChanged(double t);
    void onRangeChanged(double min, double max);
    void onPlayingChanged(bool playing);
    void onRateChanged(double rate);
    void onLoopingChanged(bool looping);

    void onSliderMoved(int value);
    void onPlayToggled(bool checked);
    void onLoopToggled(bool checked);
    void onRateEdited(double value);
    void onStepClicked();
    void onReplayClicked();
    void togglePlay();

private:
    void applyPlayIcon(bool checked);
    void updateEnabled();
    static QString formatTime(double t);

    QPushButton *m_replay = nullptr;
    QPushButton *m_loop = nullptr;
    QPushButton *m_play = nullptr;
    QLabel *m_readout = nullptr;
    QSlider *m_slider = nullptr;
    QDoubleSpinBox *m_rate = nullptr;
    QPushButton *m_step = nullptr;

    PlaybackEngine *m_engine = nullptr;
    bool m_updating = false; // 引擎回写期间抑制信号回环
};

} // namespace nextpilot::plotscope::gui

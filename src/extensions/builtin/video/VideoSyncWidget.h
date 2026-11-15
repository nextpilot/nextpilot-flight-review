#pragma once

#include <QWidget>
#include <QTemporaryDir>

class QLabel;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QTimer;
class QElapsedTimer;

// 视频同步：ffmpeg 抽帧到临时目录，按时间轴播放，向画布发 playhead。
class VideoSyncWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoSyncWidget(QWidget *parent = nullptr);
    double duration() const { return m_duration; }
    double offset() const;
    bool isLoaded() const { return m_frameCount > 0; }
    bool loadFile(const QString &path);

signals:
    void timeChanged(double seconds);
    void loaded(double duration);

private:
    void openVideo();
    void togglePlay();
    void showFrame(int index);
    void setTime(double sec);

    QLabel *m_view = nullptr;
    QLabel *m_info = nullptr;
    QPushButton *m_play = nullptr;
    QSlider *m_slider = nullptr;
    QDoubleSpinBox *m_offset = nullptr;
    QTimer *m_timer = nullptr;
    QElapsedTimer *m_clock = nullptr;
    QTemporaryDir m_dir;
    double m_duration = 0.0;
    double m_playingFrom = 0.0;
    int m_frameCount = 0;
    int m_frameIndex = -1;
};

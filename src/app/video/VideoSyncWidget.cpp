#include "VideoSyncWidget.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

VideoSyncWidget::VideoSyncWidget(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto *bar = new QHBoxLayout;
    auto *open = new QPushButton(QStringLiteral("打开视频…"), this);
    m_play = new QPushButton(QStringLiteral("播放"), this);
    m_play->setEnabled(false);
    m_offset = new QDoubleSpinBox(this);
    m_offset->setRange(-3600, 3600);
    m_offset->setDecimals(2);
    m_offset->setSuffix(QStringLiteral(" s"));
    m_offset->setValue(0);
    bar->addWidget(open);
    bar->addWidget(m_play);
    bar->addWidget(new QLabel(QStringLiteral("时间偏移"), this));
    bar->addWidget(m_offset);
    bar->addStretch(1);
    root->addLayout(bar);

    m_view = new QLabel(QStringLiteral("未加载视频"), this);
    m_view->setAlignment(Qt::AlignCenter);
    m_view->setMinimumHeight(180);
    m_view->setStyleSheet(QStringLiteral("background:#0b0f13;color:#8b949e;border:1px solid #26313a;"));
    root->addWidget(m_view, 1);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 0);
    root->addWidget(m_slider);
    m_info = new QLabel(QStringLiteral("0.00 / 0.00 s   0 帧"), this);
    root->addWidget(m_info);

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    m_clock = new QElapsedTimer;

    connect(open, &QPushButton::clicked, this, [this] { openVideo(); });
    connect(m_play, &QPushButton::clicked, this, [this] { togglePlay(); });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int v) {
        setTime(v / 1000.0);
    });
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!m_clock->isValid())
            return;
        const double sec = m_playingFrom + m_clock->elapsed() / 1000.0;
        if (sec >= m_duration)
        {
            setTime(m_duration);
            togglePlay();
            return;
        }
        setTime(sec);
    });
}

double VideoSyncWidget::offset() const
{
    return m_offset->value();
}

void VideoSyncWidget::openVideo()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择视频"), QString(),
        QStringLiteral("视频 (*.mp4 *.mov *.avi *.mkv *.webm);;全部 (*)"));
    if (path.isEmpty())
        return;
    loadFile(path);
}

bool VideoSyncWidget::loadFile(const QString &path)
{
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
    {
        m_info->setText(QStringLiteral("未找到 ffmpeg"));
        return false;
    }
    if (!ffprobe.isEmpty())
    {
        QProcess p;
        p.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-show_entries"),
                          QStringLiteral("format=duration"), QStringLiteral("-of"),
                          QStringLiteral("default=nw=1:nk=1"), path});
        p.waitForFinished(10000);
        m_duration = p.readAllStandardOutput().trimmed().toDouble();
    }
    if (m_duration <= 0)
        m_duration = 60.0;

    if (!m_dir.isValid())
        return false;
    QDir(m_dir.path()).removeRecursively();
    QDir().mkpath(m_dir.path());
    const QString pattern = m_dir.path() + QStringLiteral("/f_%05d.jpg");
    QProcess ex;
    ex.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-i"), path,
                      QStringLiteral("-vf"), QStringLiteral("fps=10,scale=640:-1"),
                      QStringLiteral("-q:v"), QStringLiteral("4"), pattern});
    if (!ex.waitForStarted(5000) || !ex.waitForFinished(60000))
    {
        ex.kill();
        m_info->setText(QStringLiteral("抽帧失败或超时"));
        return false;
    }
    m_frameCount = 0;
    for (int i = 1; i <= 100000; ++i)
    {
        const QString f = m_dir.path() + QStringLiteral("/f_%1.jpg").arg(i, 5, 10, QLatin1Char('0'));
        if (!QFileInfo::exists(f))
            break;
        m_frameCount = i;
    }
    if (m_frameCount == 0)
    {
        m_info->setText(QStringLiteral("视频没有可用帧"));
        return false;
    }
    m_play->setEnabled(true);
    m_slider->setRange(0, static_cast<int>(m_duration * 1000));
    m_frameIndex = -1;
    setTime(0);
    emit loaded(m_duration);
    return true;
}

void VideoSyncWidget::togglePlay()
{
    if (!m_frameCount)
        return;
    if (m_timer->isActive())
    {
        m_timer->stop();
        m_play->setText(QStringLiteral("播放"));
        return;
    }
    if (m_slider->value() >= m_slider->maximum())
        m_slider->setValue(0);
    m_playingFrom = m_slider->value() / 1000.0;
    m_clock->start();
    m_timer->start();
    m_play->setText(QStringLiteral("暂停"));
}

void VideoSyncWidget::setTime(double sec)
{
    if (!m_frameCount)
        return;
    sec = qBound(0.0, sec, m_duration);
    m_slider->setValue(static_cast<int>(sec * 1000));
    const int idx = qBound(0, static_cast<int>(sec * 10.0), m_frameCount - 1);
    showFrame(idx);
    m_info->setText(QStringLiteral("%1 / %2 s   %3/%4 帧")
                        .arg(sec, 0, 'f', 2)
                        .arg(m_duration, 0, 'f', 2)
                        .arg(idx + 1)
                        .arg(m_frameCount));
    emit timeChanged(sec + offset());
}

void VideoSyncWidget::showFrame(int index)
{
    if (index == m_frameIndex)
        return;
    m_frameIndex = index;
    const QString f = m_dir.path() + QStringLiteral("/f_%1.jpg").arg(index + 1, 5, 10, QLatin1Char('0'));
    QImage img(f);
    if (img.isNull())
        return;
    m_view->setPixmap(QPixmap::fromImage(img).scaled(m_view->size(), Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
}

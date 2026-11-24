#include "TimelineWidget.h"

#include "PlaybackEngine.h"

#include <QDoubleSpinBox>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStyle>

namespace nextpilot::plotscope::gui
{

namespace
{
// 滑块用 1ms 步进的整数域映射秒，避免浮点 QSlider 的精度/取整问题。
constexpr int kSliderStepsPerSecond = 1000;

QIcon icon(const QString &name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}
} // namespace

TimelineWidget::TimelineWidget(PlaybackEngine *engine, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("timelineWidget"));
    setFixedHeight(30);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(4);

    const auto flatBtn = [this](const QString &tip) {
        auto *b = new QPushButton(this);
        b->setObjectName(QStringLiteral("timelineBtn"));
        b->setFixedSize(24, 24);
        b->setIconSize(QSize(18, 18));
        b->setFlat(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        return b;
    };

    m_replay = flatBtn(QStringLiteral("回到起点"));
    m_replay->setIcon(icon(QStringLiteral("replay")));

    m_loop = flatBtn(QStringLiteral("循环播放"));
    m_loop->setIcon(icon(QStringLiteral("loop")));
    m_loop->setCheckable(true);

    m_play = flatBtn(QStringLiteral("播放/暂停（空格）"));
    m_play->setCheckable(true);
    applyPlayIcon(false);

    m_readout = new QLabel(QStringLiteral("0.000"), this);
    m_readout->setObjectName(QStringLiteral("timelineReadout"));
    m_readout->setAlignment(Qt::AlignCenter);
    m_readout->setFixedWidth(70);
    QFont rf = m_readout->font();
    rf.setPointSizeF(10.5);
    m_readout->setFont(rf);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 1);
    m_slider->setFixedHeight(20);
    m_slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_slider->setFocusPolicy(Qt::WheelFocus);

    m_rate = new QDoubleSpinBox(this);
    m_rate->setRange(PlaybackEngine::kMinRate, PlaybackEngine::kMaxRate);
    m_rate->setSingleStep(PlaybackEngine::kRateStep);
    m_rate->setDecimals(1);
    m_rate->setValue(1.0);
    m_rate->setSuffix(QStringLiteral("×"));
    m_rate->setFixedWidth(64);
    m_rate->setToolTip(QStringLiteral("播放速度"));
    m_rate->setFocusPolicy(Qt::WheelFocus);

    m_step = flatBtn(QStringLiteral("单步前进"));

    lay->addWidget(m_replay);
    lay->addWidget(m_loop);
    lay->addWidget(m_play);
    lay->addWidget(m_readout);
    lay->addWidget(m_slider, 1);
    lay->addWidget(m_rate);
    lay->addWidget(m_step);

    // 空格切换播放。设窗口级快捷键，避免按钮抢占焦点后空格被吞。
    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space->setAutoRepeat(false);
    connect(space, &QShortcut::activated, this, &TimelineWidget::togglePlay);

    connect(m_replay, &QPushButton::clicked, this, &TimelineWidget::onReplayClicked);
    connect(m_loop, &QPushButton::toggled, this, &TimelineWidget::onLoopToggled);
    connect(m_play, &QPushButton::toggled, this, &TimelineWidget::onPlayToggled);
    connect(m_slider, &QSlider::sliderMoved, this, &TimelineWidget::onSliderMoved);
    connect(m_rate, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TimelineWidget::onRateEdited);
    connect(m_step, &QPushButton::clicked, this, &TimelineWidget::onStepClicked);

    setEngine(engine);
}

TimelineWidget::~TimelineWidget() = default;

void TimelineWidget::setEngine(PlaybackEngine *engine)
{
    if (m_engine)
        m_engine->disconnect(this);
    m_engine = engine;
    if (!m_engine)
    {
        updateEnabled();
        return;
    }
    connect(m_engine, &PlaybackEngine::currentTimeChanged, this, &TimelineWidget::onTimeChanged);
    connect(m_engine, &PlaybackEngine::rangeChanged, this, &TimelineWidget::onRangeChanged);
    connect(m_engine, &PlaybackEngine::playingChanged, this, &TimelineWidget::onPlayingChanged);
    connect(m_engine, &PlaybackEngine::rateChanged, this, &TimelineWidget::onRateChanged);
    connect(m_engine, &PlaybackEngine::loopingChanged, this, &TimelineWidget::onLoopingChanged);

    onRangeChanged(m_engine->rangeMin(), m_engine->rangeMax());
    onTimeChanged(m_engine->currentTime());
    onPlayingChanged(m_engine->isPlaying());
    onRateChanged(m_engine->rate());
    onLoopingChanged(m_engine->isLooping());
}

QString TimelineWidget::formatTime(double t)
{
    if (t < 0.0)
        t = 0.0;
    const int totalMs = static_cast<int>(t * 1000.0 + 0.5);
    const int ms = totalMs % 1000;
    const int totalSec = totalMs / 1000;
    const int sec = totalSec % 60;
    const int min = (totalSec / 60) % 60;
    const int hour = totalSec / 3600;
    if (hour > 0)
        return QStringLiteral("%1:%2:%3.%4")
            .arg(hour).arg(min, 2, 10, QLatin1Char('0'))
            .arg(sec, 2, 10, QLatin1Char('0')).arg(ms, 3, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2.%3")
        .arg(min, 2, 10, QLatin1Char('0'))
        .arg(sec, 2, 10, QLatin1Char('0')).arg(ms, 3, 10, QLatin1Char('0'));
}

void TimelineWidget::updateEnabled()
{
    const bool hasRange = m_engine && m_engine->rangeMax() > m_engine->rangeMin();
    const bool usable = hasRange || (m_engine && m_engine->isPlaying());
    m_play->setEnabled(usable);
    m_slider->setEnabled(hasRange);
    m_step->setEnabled(usable);
    m_replay->setEnabled(hasRange);
}

void TimelineWidget::applyPlayIcon(bool checked)
{
    m_play->setIcon(icon(checked ? QStringLiteral("pause") : QStringLiteral("play")));
    m_play->setToolTip(checked ? QStringLiteral("暂停（空格）") : QStringLiteral("播放（空格）"));
}

void TimelineWidget::onTimeChanged(double t)
{
    if (!m_engine)
        return;
    m_updating = true;
    const double span = m_engine->rangeMax() - m_engine->rangeMin();
    const int steps = m_slider->maximum();
    if (span > 0.0 && steps > 0)
    {
        const int v = static_cast<int>((t - m_engine->rangeMin()) / span * steps + 0.5);
        m_slider->setValue(qBound(0, v, steps));
    }
    m_readout->setText(formatTime(t));
    m_updating = false;
}

void TimelineWidget::onRangeChanged(double min, double max)
{
    m_updating = true;
    const int steps = qMax(1, static_cast<int>((max - min) * kSliderStepsPerSecond + 0.5));
    m_slider->setRange(0, steps);
    m_updating = false;
    updateEnabled();
}

void TimelineWidget::onPlayingChanged(bool playing)
{
    m_updating = true;
    m_play->setChecked(playing);
    applyPlayIcon(playing);
    m_updating = false;
    updateEnabled();
}

void TimelineWidget::onRateChanged(double rate)
{
    m_updating = true;
    m_rate->setValue(rate);
    m_updating = false;
}

void TimelineWidget::onLoopingChanged(bool looping)
{
    m_updating = true;
    m_loop->setChecked(looping);
    m_updating = false;
}

void TimelineWidget::onPlayToggled(bool checked)
{
    if (m_updating || !m_engine)
        return;
    applyPlayIcon(checked);
    // checked 已是目标状态，直接设，勿再 toggle（否则翻转两次）。
    if (checked)
        m_engine->play();
    else
        m_engine->pause();
}

void TimelineWidget::onLoopToggled(bool checked)
{
    if (m_updating || !m_engine)
        return;
    m_engine->setLooping(checked);
}

void TimelineWidget::onRateEdited(double value)
{
    if (m_updating || !m_engine)
        return;
    m_engine->setRate(value);
}

void TimelineWidget::onSliderMoved(int value)
{
    if (m_updating || !m_engine)
        return;
    const int steps = m_slider->maximum();
    const double span = m_engine->rangeMax() - m_engine->rangeMin();
    if (steps <= 0 || span <= 0.0)
        return;
    const double t = m_engine->rangeMin() + static_cast<double>(value) / steps * span;
    m_engine->setCurrentTime(t);
    m_readout->setText(formatTime(t));
}

void TimelineWidget::onStepClicked()
{
    if (!m_engine)
        return;
    m_engine->pause();
    m_engine->setCurrentTime(m_engine->currentTime() + m_engine->step());
}

void TimelineWidget::onReplayClicked()
{
    if (!m_engine)
        return;
    m_engine->setCurrentTime(m_engine->rangeMin());
}

void TimelineWidget::togglePlay()
{
    if (m_engine)
        m_engine->togglePlay();
}

} // namespace nextpilot::plotscope::gui

#include "PlaybackController.h"

#include "PlaybackEngine.h"
#include "TimelineWidget.h"

#include <QWidget>

namespace nextpilot::plotscope::gui
{

PlaybackController::PlaybackController(QObject *parent) : QObject(parent)
{
    m_engine = new PlaybackEngine(this);
    m_timeline = new TimelineWidget(m_engine);
    m_timeline->setVisible(false); // 有数据载入 range 后才显示
    connect(m_engine, &PlaybackEngine::currentTimeChanged, this, &PlaybackController::playheadChanged);
}

PlaybackController::~PlaybackController() = default;

QWidget *PlaybackController::timelineWidget() const
{
    return m_timeline;
}

void PlaybackController::setRange(double lo, double hi)
{
    m_engine->setRange(lo, hi);
    m_timeline->setVisible(true);
}

} // namespace nextpilot::plotscope::gui

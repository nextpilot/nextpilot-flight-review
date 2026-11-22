#include "CanvasWorkspace.h"

#include "AppContext.h"
#include "chrome/CellTitleBar.h"
#include "layout/PageTabs.h"
#include "layout/PageDock.h"
#include "layout/PlotCellDock.h"
#include "ChannelView.h"
#include "GridPickPopup.h"
#include "IIntentExecutor.h"
#include "InstrumentView.h"
#include "PanelFactoryHub.h"
#include "PlotCanvas.h"
#include "PlaybackEngine.h"
#include "TimelineWidget.h"
#include "DatasetHub.h"
#include "Dataset.h"
#include "SourceHub.h"
#include "PropertySchema.h"
#include "Result.h"
#include "ErrorCodes.h"
#include "Transform.h"
#include "TransformEditorDialog.h"
#include <fastplot/Axes.h>
#include <fastplot/Panner.h>
#include <fastplot/Zoomer.h>

#include <limits>

// 布局层：每格一个 ADS 停靠面板（拖动分屏 / 拖到中心合并 / 拖出浮动 / 拖回停靠全由 ADS 提供）。
#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "DockContainerWidget.h"
#include "DockManager.h"
#include "DockSplitter.h"
#include "DockWidget.h"

#include <QEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QToolTip>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QSet>
#include <QScrollBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QStringList>

#include <algorithm>

using namespace nextpilot::plotscope::foundation;
using namespace nextpilot::plotscope::dataset;
using namespace nextpilot::plotscope::intent;
using namespace nextpilot::plotscope::uischema;
using namespace nextpilot::plotscope::registry;
using namespace nextpilot::plotscope::parser;
using namespace nextpilot::plotscope::context;

// 播放域 view action：播放/暂停/步进/倍速/循环与状态查询。

namespace nextpilot::plotscope::gui
{
void CanvasWorkspace::registerPlaybackActions()
{

    // 回放引擎：会话级时间游标的播放/暂停/定位/速率/循环/步进。
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.play"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->play();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.pause"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->pause();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.toggle"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->togglePlay();
                                    return QVariant(m_playback->isPlaying());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.seek"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->setCurrentTime(params.value(QStringLiteral("t")).toDouble());
                                    return QVariant(m_playback->currentTime());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.rate"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    m_playback->setRate(params.value(QStringLiteral("rate")).toDouble());
                                    return QVariant(m_playback->rate());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.loop"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    const bool on = params.contains(QStringLiteral("on"))
                                        ? params.value(QStringLiteral("on")).toBool() : !m_playback->isLooping();
                                    m_playback->setLooping(on);
                                    return QVariant(m_playback->isLooping());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.step"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    bool dirOk = false;
                                    const int dir = params.value(QStringLiteral("dir")).toInt(&dirOk);
                                    const int stepDir = dirOk ? dir : 1;
                                    m_playback->setCurrentTime(m_playback->currentTime() + stepDir * m_playback->step());
                                    return QVariant(m_playback->currentTime());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("playback.state"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!m_playback) return QVariant(false);
                                    return QVariant(QVariantMap{
                                        {QStringLiteral("t"), m_playback->currentTime()},
                                        {QStringLiteral("min"), m_playback->rangeMin()},
                                        {QStringLiteral("max"), m_playback->rangeMax()},
                                        {QStringLiteral("rate"), m_playback->rate()},
                                        {QStringLiteral("playing"), m_playback->isPlaying()},
                                        {QStringLiteral("looping"), m_playback->isLooping()},
                                    });
                                });
}
} // namespace nextpilot::plotscope::gui

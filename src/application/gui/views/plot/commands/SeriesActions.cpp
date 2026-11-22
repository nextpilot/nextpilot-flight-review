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

// 曲线域 view action：清空、列举、轴归属、图表类型、可见/颜色/删除。

namespace nextpilot::plotscope::gui
{
void CanvasWorkspace::registerSeriesActions()
{

    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (activeChannelView())
                                        activeChannelView()->clearChannels();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.list"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantList list;
                                    if (PlotCanvas *c = activeCanvas())
                                        for (const PlotSeries &s : c->series())
                                            list.append(QVariantMap{
                                                {QStringLiteral("key"), s.key()},
                                                {QStringLiteral("dataset"), s.datasetId},
                                                {QStringLiteral("table"), s.table},
                                                {QStringLiteral("column"), s.column},
                                                {QStringLiteral("points"), s.lod.size()},
                                                {QStringLiteral("rightAxis"), s.rightAxis},
                                            });
                                    return QVariant(list);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.axis"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const bool right = params.value(QStringLiteral("right")).toBool();
                                    if (key.isEmpty())
                                        return fail(err::Key, QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesAxis(key, right))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });

    // —— plotting 全套操作意图 ——
    // 全部落到 active cell 的 PlotCanvas 真实能力，不造空按钮。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.visible"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const bool vis = params.value(QStringLiteral("visible"), true).toBool();
                                    if (key.isEmpty())
                                        return fail(err::Key, QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesVisible(key, vis))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.color"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    const QColor color = params.value(QStringLiteral("color")).value<QColor>();
                                    if (key.isEmpty() || !color.isValid())
                                        return fail(err::Key, QStringLiteral("缺少曲线 key 或颜色"));
                                    if (!activeCanvas() || !activeCanvas()->setSeriesInk(key, color))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.series.remove"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    if (key.isEmpty())
                                        return fail(err::Key, QStringLiteral("缺少曲线 key"));
                                    if (!activeCanvas() || !activeCanvas()->removeSeries(key))
                                        return fail(err::NoEnt, QStringLiteral("曲线不存在"));
                                    return QVariant(true);
                                });}
} // namespace nextpilot::plotscope::gui

#include "PlotWorkspace.h"

#include "AppContext.h"
#include "chrome/AxesToolbar.h"
#include "layout/TabbedFigure.h"
#include "layout/PlotLayout.h"
#include "layout/DockedAxes.h"
#include "ChannelView.h"
#include "GridPickPopup.h"
#include "IIntentExecutor.h"
#include "InstrumentView.h"
#include "PanelFactoryHub.h"
#include "AxesView.h"
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

// 标注域 view action：游标+A/B、书签、标记线、状态叠加项（set/clear）。

namespace nextpilot::plotscope::gui
{
void PlotWorkspace::registerMarkupActions()
{

    // 读数游标 / A-B / 书签（对齐老 PlottingCanvas 交互，view action 供菜单/快捷键/测试统一触发）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.cursor.pinned"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    AxesView *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    c->setCursorTime(c->cursorTime(), !c->cursorPinned());
                                    return QVariant(c->cursorPinned());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.cursor.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (AxesView *c = activeCanvas()) { c->clearCursor(); c->clearAB(); }
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.ab.toggle"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    AxesView *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    c->toggleAB();
                                    return QVariant(c->abActive());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.bookmark.add"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    // 「标记」：在**当前页所有画布**的同一时刻各画一条竖线
                                    // （用户要求：绘制时所有 axes 同步添加），而不是只加活动那格。
                                    AxesView *ref = activeCanvas();
                                    for (const Cell &cell : m_cells)
                                        if (cell.canvas && !ref)
                                            ref = cell.canvas;
                                    if (!ref)
                                        return QVariant(false);
                                    const double t = params.contains(QStringLiteral("t"))
                                                         ? params.value(QStringLiteral("t")).toDouble()
                                                         : ref->cursorTime();
                                    const QString label =
                                        params.value(QStringLiteral("label")).toString();
                                    int added = 0;
                                    for (const Cell &cell : m_cells)
                                    {
                                        if (!cell.canvas)
                                            continue;
                                        cell.canvas->addBookmark(t, label);
                                        ++added;
                                    }
                                    return QVariant(added);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.bookmark.list"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantList list;
                                    if (AxesView *c = activeCanvas())
                                        for (const AxesView::Bookmark &bm : c->bookmarks())
                                            list.append(QVariantMap{
                                                {QStringLiteral("id"), bm.id},
                                                {QStringLiteral("t"), bm.t},
                                                {QStringLiteral("label"), bm.label},
                                            });
                                    return QVariant(list);
                                });

    // Marker 规则产出：把 PlotMarker 列表画到活动画布（竖线/区间带/值带/label）。
    // params["markers"] 为 QVariantList<QVariantMap>（每项是 PlotMarker::toJson 的 map）。
    auto applyMarkers = [this](const QVariantMap &params) -> Result<QVariant> {
        AxesView *c = activeCanvas();
        if (!c)
            return QVariant(false);
        QJsonArray arr = QJsonArray::fromVariantList(
            params.value(QStringLiteral("markers")).toList());
        c->setMarkers(arr);
        return QVariant(c->markerCount());
    };
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.markers.set"), applyMarkers);
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.markers.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (AxesView *c = activeCanvas())
                                        c->setMarkers(QJsonArray());
                                    return QVariant(true);
                                });

    // 状态视图：rows = [{channel, segments:[…]}]，
    // 画到活动画布顶部的状态色带区（host 的 data.state intent 负责分段取数）。
    // params["mode"]=="append" 时按 channel 名合并（多通道多行叠加）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.set"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    AxesView *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    QJsonArray arr = QJsonArray::fromVariantList(
                                        params.value(QStringLiteral("rows")).toList());
                                    const bool append =
                                        params.value(QStringLiteral("mode")).toString() == QStringLiteral("append");
                                    const bool ok = c->setStateRows(arr, append);
                                    return QVariant(ok ? c->stateRowCount() : 0);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.clear"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (AxesView *c = activeCanvas())
                                        c->setStateRows(QJsonArray());
                                    return QVariant(true);
                                });}
} // namespace nextpilot::plotscope::gui

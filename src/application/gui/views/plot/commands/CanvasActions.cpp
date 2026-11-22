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

// 画布域 view action：时间刻度、导出、状态存取、图例、缩放撤销、自适应、网格/边框、工具、轴范围、叠加与多源对比、表格展开。

namespace nextpilot::plotscope::gui
{
void CanvasWorkspace::registerCanvasActions()
{
    // 所有 plot.* 只在这里注册一次，路由到 active cell —— 多画布不会被后建者劫持。
    // 时间轴刻度三态：设置保存后外壳派发一次，所有格的画布立即按新格式重绘。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.timeFormat"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    QString mode = params.value(QStringLiteral("mode")).toString();
                                    if (mode.isEmpty())
                                        mode = m_appCtx.settings()
                                                   .value(QStringLiteral("plot.time.format"),
                                                          QStringLiteral("relative"))
                                                   .toString();
                                    // 遍历**所有页**的 cell：非当前页的 cells 存在 Page 里，
                                    // 只扫 m_cells 会让切页后的画布沿用旧刻度（用户会当成"设置没生效"）。
                                    int applied = 0;
                                    QSet<PlotCanvas *> seen;
                                    auto applyOne = [&](const Cell &c) {
                                        if (!c.canvas || seen.contains(c.canvas))
                                            return;
                                        seen.insert(c.canvas);
                                        c.canvas->setTimeFormatMode(mode);
                                        c.canvas->update();
                                        ++applied;
                                    };
                                    for (int pi = 0; pi < m_pages.size(); ++pi)
                                    {
                                        const QVector<Cell> &cells =
                                            (pi == m_activePage) ? m_cells : m_pages.at(pi)->cells;
                                        for (const Cell &c : cells)
                                            applyOne(c);
                                    }
                                    for (const Cell &c : m_cells) // 防御：页表尚未写回时也算上
                                        applyOne(c);
                                    return QVariant(applied);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.export.png"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(err::Path, QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportPng(path))
                                        return fail(err::Io, QStringLiteral("PNG 导出失败"));
                                    return QVariant(path);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.export.csv"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString path = params.value(QStringLiteral("path")).toString();
                                    if (path.isEmpty())
                                        return fail(err::Path, QStringLiteral("缺少导出路径"));
                                    if (!activeCanvas() || !activeCanvas()->exportCsv(path))
                                        return fail(err::Io, QStringLiteral("CSV 导出失败"));
                                    return QVariant(path);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.save"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return activeCanvas() ? QVariant(activeCanvas()->saveViewState())
                                                          : QVariant(QVariantMap());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.state.restore"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const auto state = params.value(QStringLiteral("state")).toJsonObject();
                                    if (!activeCanvas() || !activeCanvas()->restoreViewState(state))
                                        return fail(err::State, QStringLiteral("视图状态复原失败"));
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.toggle"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!activeCanvas())
                                        return QVariant(false);
                                    activeCanvas()->setLegendVisible(!activeCanvas()->legendVisible());
                                    return QVariant(activeCanvas()->legendVisible());
                                });

    // === 图例交互（第二梯队 #205，
    // 点击条目切显隐 / 折叠 / 字号缩放的脚本与菜单入口（GUI 点击直接走画布命中）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.toggleItem"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    const QString key = params.value(QStringLiteral("key")).toString();
                                    if (!c || key.isEmpty())
                                        return QVariant(false);
                                    return QVariant(c->toggleSeriesVisible(key));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.collapse"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    const bool target = params.contains(QStringLiteral("collapsed"))
                                                            ? params.value(QStringLiteral("collapsed")).toBool()
                                                            : !c->legendCollapsed(); // 无参 = 切换
                                    c->setLegendCollapsed(target);
                                    return QVariant(target);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.legend.fontZoom"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c)
                                        return QVariant(false);
                                    const int steps = params.value(QStringLiteral("steps")).toInt();
                                    c->zoomLegendFont(steps);
                                    return QVariant(c->legendFontScale());
                                });

    // === 多源时间对齐（第二梯队 #204，
    // 源级显示时间偏移（秒）：施加在"曲线 x + 源 offset"，不改数据。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.offset.set"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    bool okId = false;
                                    const int src = params.value(QStringLiteral("sourceId")).toInt(&okId);
                                    if (!okId || src < 0)
                                        return Error{err::Args,
                                                     QStringLiteral("需要 sourceId 参数")};
                                    const double off = params.value(QStringLiteral("offsetSec")).toDouble();
                                    m_appCtx.setSourceTimeOffset(src, off);
                                    return QVariant(off);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.offset.clear"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    if (params.contains(QStringLiteral("sourceId")))
                                    {
                                        m_appCtx.setSourceTimeOffset(
                                            params.value(QStringLiteral("sourceId")).toInt(), 0.0);
                                    }
                                    else
                                    {
                                        for (int src : m_appCtx.sourceTimeOffsetIds())
                                            m_appCtx.setSourceTimeOffset(src, 0.0);
                                    }
                                    return QVariant(true);
                                });
    // 一键对齐：mode = start|center|end。按各源时间范围平移，使所有源
    // 的开始/中点/结束落在同一时刻（基准取全局极值），返回 {sourceId: offset}。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.align"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString mode =
                                        params.value(QStringLiteral("mode")).toString();
                                    if (mode != QStringLiteral("start")
                                        && mode != QStringLiteral("center")
                                        && mode != QStringLiteral("end"))
                                        return Error{err::Args,
                                                     QStringLiteral("mode 需为 start/center/end")};
                                    // 每源聚合时间范围（数据集首尾时间）。
                                    QHash<int, QPair<double, double>> ranges; // src -> [tMin,tMax]
                                    for (const auto dsId : m_appCtx.data().ids())
                                    {
                                        const Dataset *ds = m_appCtx.data().get(dsId);
                                        if (!ds || ds->tableCount() == 0)
                                            continue;
                                        const int src = m_appCtx.sources().sourceOfDataset(dsId);
                                        if (src < 0)
                                            continue;
                                        const double lo = ds->timeMin();
                                        const double hi = ds->timeMax();
                                        if (!qIsFinite(lo) || !qIsFinite(hi))
                                            continue;
                                        auto it = ranges.find(src);
                                        if (it == ranges.end())
                                            ranges.insert(src, {lo, hi});
                                        else
                                        {
                                            it.value().first = qMin(it.value().first, lo);
                                            it.value().second = qMax(it.value().second, hi);
                                        }
                                    }
                                    if (ranges.isEmpty())
                                        return Error{err::NoData,
                                                     QStringLiteral("无可用数据源")};
                                    double gLo = std::numeric_limits<double>::max();
                                    double gHi = std::numeric_limits<double>::lowest();
                                    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it)
                                    {
                                        gLo = qMin(gLo, it.value().first);
                                        gHi = qMax(gHi, it.value().second);
                                    }
                                    QVariantMap applied;
                                    for (auto it = ranges.constBegin(); it != ranges.constEnd(); ++it)
                                    {
                                        const double anchor =
                                            mode == QStringLiteral("start") ? it.value().first
                                          : mode == QStringLiteral("end")   ? it.value().second
                                                                            : (it.value().first + it.value().second) / 2.0;
                                        const double target =
                                            mode == QStringLiteral("start") ? gLo
                                          : mode == QStringLiteral("end")   ? gHi
                                                                            : (gLo + gHi) / 2.0;
                                        const double off = target - anchor;
                                        m_appCtx.setSourceTimeOffset(it.key(), off);
                                        applied.insert(QString::number(it.key()), off);
                                    }
                                    return QVariant(applied);
                                });
    // 图型切换：workspace 直接处理（曲线在画布切、专用图型在 cell 内切页），
    // 不再需要外壳 requestPanel 替换整个中央——多画布形态得以保留。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.setType"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const QString kind = params.value(QStringLiteral("type")).toString();
                                    const bool handled = setCellType(m_active, kind);
                                    QVariantMap r;
                                    r.insert(QStringLiteral("handled"), handled);
                                    r.insert(QStringLiteral("kind"), kind);
                                    return QVariant(r);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.zoom"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const double factor = params.value(QStringLiteral("factor"), 0.5).toDouble();
                                    if (!activeCanvas())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    activeCanvas()->zoomRange(factor <= 0.0 ? 0.5 : factor);
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.view.undo"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->canUndoView())
                                        return QVariant(false);
                                    c->undoView();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.autoscale"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!activeCanvas())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    activeCanvas()->autoScale();
                                    return QVariant(true);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.grid"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    // axis 省略=整体开关；"x"=竖网格（Bottom 轴刻度）；"y"=横网格。
                                    auto *ax = c->plotWidget();
                                    const QString axis = params.value(QStringLiteral("axis")).toString();
                                    const bool on = params.contains(QStringLiteral("on"))
                                                        ? params.value(QStringLiteral("on")).toBool()
                                                        : !ax->gridVisible();
                                    if (axis.compare(QStringLiteral("x"), Qt::CaseInsensitive) == 0)
                                        ax->setAxisGridVisible(fastplot::AxisSide::Bottom, on);
                                    else if (axis.compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0)
                                    {
                                        ax->setAxisGridVisible(fastplot::AxisSide::Left, on);
                                        ax->setAxisGridVisible(fastplot::AxisSide::Right, on);
                                    }
                                    else
                                        ax->grid(on);
                                    return QVariant(on);
                                });
    // 显示组「网格」下拉的勾选态：返回当前活动画布的 X/Y 网格线可见性。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.grid.query"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    auto *ax = c->plotWidget();
                                    QVariantMap out{{QStringLiteral("x"),
                                                     ax->axisGridVisible(fastplot::AxisSide::Bottom)},
                                                    {QStringLiteral("y"),
                                                     ax->axisGridVisible(fastplot::AxisSide::Left)
                                                         || ax->axisGridVisible(fastplot::AxisSide::Right)}};
                                    return QVariant(out);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.box"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    const bool on = params.contains(QStringLiteral("on"))
                                                        ? params.value(QStringLiteral("on")).toBool()
                                                        : !c->plotWidget()->boxVisible();
                                    c->plotWidget()->box(on);
                                    return QVariant(on);
                                });
    // 老 buildPanelsToolbar 的工具模式：移动/放大/缩小左键互斥（-1=恢复默认）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.tool"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int tool = params.value(QStringLiteral("tool"), -1).toInt();
                                    for (const Cell &cell : m_cells)
                                    {
                                        PlotCanvas *c = cell.canvas;
                                        if (!c || !c->zoomer() || !c->panner())
                                            continue;
                                        auto *z = c->zoomer();
                                        auto *pn = c->panner();
                                        z->setLeftBand(tool == 1 || tool == 2);
                                        z->setBandZoomOut(tool == 2);
                                        pn->setEnabled(tool == 0 || tool == -1);
                                    }
                                    return QVariant(tool);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.axis.range"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    const QString axis = params.value(QStringLiteral("axis"), QStringLiteral("x")).toString();
                                    const double lo = params.value(QStringLiteral("lo")).toDouble();
                                    const double hi = params.value(QStringLiteral("hi")).toDouble();
                                    if (hi <= lo)
                                        return fail(err::Range, QStringLiteral("范围非法"));
                                    if (axis == QStringLiteral("y2"))
                                        c->plotWidget()->setYRange2(lo, hi);
                                    else if (axis == QStringLiteral("y"))
                                        c->plotWidget()->setYRange(lo, hi);
                                    else
                                        c->plotWidget()->setXRange(lo, hi);
                                    c->plotWidget()->update();
                                    return QVariant(true);
                                });
    // 文本读数覆盖层：时间标记 / A-B 测量 / 播放头共用，诚实呈现为读数条
    // （交互式拖拽游标未实现，此处仅做读数条）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.overlay"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    PlotCanvas *c = activeCanvas();
                                    if (!c || !c->plotWidget())
                                        return fail(err::NoEnt, QStringLiteral("无活动画布"));
                                    const QString text = params.value(QStringLiteral("text")).toString();
                                    if (text.isEmpty())
                                        c->plotWidget()->clearOverlay();
                                    else
                                        c->plotWidget()->setOverlayText(text);
                                    return QVariant(true);
                                });

    // —— 展开数据列——
    // 表节点右键一键把整张表的数值列画进当前画布；上限 8 条（再多就成毛线团），
    // bool/文本列默认不画（bool 有专门的状态视图）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.expandColumns"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            PlotCanvas *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            const int dsId = params.value(QStringLiteral("dataset"), m_appCtx.activeDatasetId()).toInt();
            const QString tableName = params.value(QStringLiteral("table")).toString();
            const int limit = params.value(QStringLiteral("limit"), 8).toInt();
            if (dsId < 0 || tableName.isEmpty())
                return fail(err::Key, QStringLiteral("缺少 dataset / table"));
            const nextpilot::plotscope::dataset::Dataset *ds = m_appCtx.data().get(dsId);
            if (!ds)
                return fail(err::NoEnt, QStringLiteral("数据集不存在"));
            const nextpilot::plotscope::dataset::Table *t = ds->table(tableName);
            if (!t)
                return fail(err::NoEnt,
                            QStringLiteral("表不存在: %1").arg(tableName));

            QStringList numeric;
            for (const nextpilot::plotscope::dataset::Column &col : t->columns)
                if (col.type != nextpilot::plotscope::dataset::ValueType::Bool
                    && col.type != nextpilot::plotscope::dataset::ValueType::Text)
                    numeric << col.name;
            if (numeric.isEmpty())
                return fail(err::NoEnt,
                            QStringLiteral("表 %1 没有可画的数值列").arg(tableName));

            const int n = limit > 0 ? qMin(limit, int(numeric.size())) : int(numeric.size());
            for (int i = 0; i < n; ++i)
                if (activeChannelView())
                    activeChannelView()->addChannel(dsId, tableName, numeric.at(i),
                                                    nullptr,
                                                    QStringLiteral("%1.%2")
                                                        .arg(tableName, numeric.at(i)));
            QVariantMap out;
            out.insert(QStringLiteral("drawn"), n);
            out.insert(QStringLiteral("total"), int(numeric.size()));
            out.insert(QStringLiteral("truncated"), n < numeric.size());
            out.insert(QStringLiteral("names"), numeric.mid(0, n));
            return QVariant(out);
        });

    // —— 多源同名叠加/对比——
    //
    // 判定口径：同名 = **列名相同**（表可以不同；一个源里多表更常见）。
    // 曲线名统一「源名:table.column」，否则 N 条曲线挤在一起分不清谁是谁。
    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.overlaySame"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            PlotCanvas *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            int dsId = params.value(QStringLiteral("dataset"), m_appCtx.activeDatasetId()).toInt();
            const QString column = params.value(QStringLiteral("column"),
                                                m_appCtx.activeColumn()).toString();
            if (column.isEmpty() || dsId < 0)
                return fail(err::NoEnt, QStringLiteral("先在数据集树里选一个字段"));

            // 源名可能重复（同一份文件被加载两次、或同名不同路径），
            // 重名时补 #id —— 曲线名必须唯一，否则 N 条曲线在图上分不清。
            auto uniqSrc = [this](int srcId, const QString &name) -> QString {
                int dup = 0;
                for (int s : m_appCtx.sources().ids())
                {
                    const nextpilot::plotscope::dataset::DataSourceInfo *i =
                        m_appCtx.sources().get(s);
                    if (i && i->name == name)
                        ++dup;
                }
                return dup > 1 ? QStringLiteral("%1#%2").arg(name).arg(srcId) : name;
            };

            QVariantList addedNames;
            QString firstErr;
            int matched = 0;
            for (int srcId : m_appCtx.sources().ids())
            {
                const nextpilot::plotscope::dataset::DataSourceInfo *info =
                    m_appCtx.sources().get(srcId);
                if (!info)
                    continue;
                const QString srcName = uniqSrc(srcId,
                                                info->name.isEmpty()
                                                    ? QStringLiteral("src%1").arg(srcId)
                                                    : info->name);
                for (int d : info->datasetIds)
                {
                    const nextpilot::plotscope::dataset::Dataset *ds = m_appCtx.data().get(d);
                    if (!ds)
                        continue;
                    // 一个数据集里取第一张含该列的表（多表格式里同名通道通常只出现一次）。
                    QString hitTable;
                    for (const QString &tn : ds->tableNames())
                    {
                        const nextpilot::plotscope::dataset::Table *t = ds->table(tn);
                        if (!t)
                            continue;
                        for (const nextpilot::plotscope::dataset::Column &col : t->columns)
                            if (col.name == column)
                            {
                                hitTable = tn;
                                break;
                            }
                        if (!hitTable.isEmpty())
                            break;
                    }
                    if (hitTable.isEmpty())
                        continue;
                    ++matched;
                    const QString title =
                        QStringLiteral("%1:%2.%3").arg(srcName, hitTable, column);
                    QString addErr;
                    if (activeChannelView())
                    {
                        if (!activeChannelView()->addChannel(d, hitTable, column, &addErr, title))
                        {
                            // 取数失败不能静默：名字里有源和列，用户才追得住。
                            if (firstErr.isEmpty())
                                firstErr = QStringLiteral("%1.%2：%3")
                                               .arg(hitTable, column, addErr);
                            ++matched;
                            continue;
                        }
                    }
                    addedNames.append(title);
                }
            }
            if (addedNames.isEmpty())
                return fail(err::NoEnt,
                            firstErr.isEmpty()
                                ? QStringLiteral("没有其它源包含同名通道: %1").arg(column)
                                : firstErr);
            QVariantMap out;
            out.insert(QStringLiteral("matched"), matched);
            out.insert(QStringLiteral("added"), int(addedNames.size()));
            out.insert(QStringLiteral("names"), addedNames);
            return QVariant(out);
        });

    m_appCtx.interactions().registerViewAction(QStringLiteral("plot.compareSame"),
        [this](const QVariantMap &params) -> Result<QVariant> {
            PlotCanvas *c = activeCanvas();
            if (!c)
                return fail(err::NoEnt, QStringLiteral("无活动画布"));
            const int dsId = params.value(QStringLiteral("dataset"), m_appCtx.activeDatasetId()).toInt();
            const QString column = params.value(QStringLiteral("column"),
                                                m_appCtx.activeColumn()).toString();
            if (column.isEmpty() || dsId < 0)
                return fail(err::NoEnt, QStringLiteral("先在数据集树里选一个字段"));

            // 只取两条：当前通道 + 第一个能匹配上的**别的源**。
            auto uniqSrc = [this](int srcId, const QString &name) -> QString {
                int dup = 0;
                for (int s : m_appCtx.sources().ids())
                {
                    const nextpilot::plotscope::dataset::DataSourceInfo *i =
                        m_appCtx.sources().get(s);
                    if (i && i->name == name)
                        ++dup;
                }
                return dup > 1 ? QStringLiteral("%1#%2").arg(name).arg(srcId) : name;
            };
            struct Hit { int datasetId; QString table; QString srcName; };
            Hit self{-1, QString(), QString()};
            Hit other{-1, QString(), QString()};
            bool hasOther = false;
            for (int srcId : m_appCtx.sources().ids())
            {
                const nextpilot::plotscope::dataset::DataSourceInfo *info =
                    m_appCtx.sources().get(srcId);
                if (!info)
                    continue;
                const QString srcName = uniqSrc(srcId,
                                                info->name.isEmpty()
                                                    ? QStringLiteral("src%1").arg(srcId)
                                                    : info->name);
                for (int d : info->datasetIds)
                {
                    const nextpilot::plotscope::dataset::Dataset *ds = m_appCtx.data().get(d);
                    if (!ds)
                        continue;
                    QString hitTable;
                    for (const QString &tn : ds->tableNames())
                    {
                        const nextpilot::plotscope::dataset::Table *t = ds->table(tn);
                        if (!t)
                            continue;
                        for (const nextpilot::plotscope::dataset::Column &col : t->columns)
                            if (col.name == column)
                            {
                                hitTable = tn;
                                break;
                            }
                        if (!hitTable.isEmpty())
                            break;
                    }
                    if (hitTable.isEmpty())
                        continue;
                    if (d == dsId)
                    {
                        self = Hit{d, hitTable, srcName};
                    }
                    else if (!hasOther && d != self.datasetId)
                    {
                        other = Hit{d, hitTable, srcName};
                        hasOther = true;
                        break; // 只取一条对照，够了
                    }
                }
                if (hasOther)
                    break;
            }
            if (self.datasetId < 0 || !hasOther)
                return fail(err::NoEnt,
                            QStringLiteral("没有别的源包含同名通道: %1").arg(column));

            const nextpilot::plotscope::dataset::Dataset *base = m_appCtx.data().get(self.datasetId);
            const nextpilot::plotscope::dataset::Dataset *peer = m_appCtx.data().get(other.datasetId);
            if (!base || !peer)
                return fail(err::NoEnt, QStringLiteral("数据集已失效"));

            // 对比前先清掉当前画布的曲线：AC-19 要求"恰两条"，残留会让条数不对。
            ChannelView *view = activeChannelView();
            if (!view)
                return fail(err::NoEnt, QStringLiteral("无活动画布视图"));
            view->clearChannels();
            QString e1, e2;
            const QString baseName = QStringLiteral("%1:%2.%3").arg(self.srcName, self.table, column);
            const QString peerName = QStringLiteral("%1:%2.%3").arg(other.srcName, other.table, column);
            if (!view->addChannel(self.datasetId, self.table, column, &e1, baseName))
                return fail(err::NoEnt, QStringLiteral("基准曲线没画上：%1").arg(e1));
            if (!view->addChannel(other.datasetId, other.table, column, &e2, peerName))
                return fail(err::NoEnt, QStringLiteral("对照曲线没画上：%1").arg(e2));

            // offset 建议与 T3 同一口径：**终点对齐**（baseEnd - peerEnd）。
            const qint64 offsetUs =
                qRound64((base->timeMax() - peer->timeMax()) * 1000000.0);
            QVariantMap out;
            out.insert(QStringLiteral("series"), 2);
            out.insert(QStringLiteral("offset_us"), QVariant::fromValue<qlonglong>(offsetUs));
            out.insert(QStringLiteral("base"), QStringLiteral("%1:%2.%3").arg(self.srcName, self.table, column));
            out.insert(QStringLiteral("peer"), QStringLiteral("%1:%2.%3").arg(other.srcName, other.table, column));
            out.insert(QStringLiteral("x_lo"), qMin(base->timeMin(), peer->timeMin()));
            out.insert(QStringLiteral("x_hi"), qMax(base->timeMax(), peer->timeMax()));
            return QVariant(out);
        });}
} // namespace nextpilot::plotscope::gui

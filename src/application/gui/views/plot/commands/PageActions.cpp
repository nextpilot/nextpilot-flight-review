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

// 页域 view action：页新建/关闭/改名/选中/编号、网格与复制、整页全屏、拆分、增删画布。

namespace nextpilot::plotscope::gui
{
void CanvasWorkspace::registerPageActions()
{

    // —— page.*：页面/画布布局操作 ——
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.addCanvas"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(addCell());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.removeCanvas"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(removeAt(m_active));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.splitHorizontal"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(split(true));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.splitVertical"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(split(false));
                                });
    // 页签复制：对齐老 PlottingTabs 的"复制页"——整页搬走（页内每一格的曲线与视图状态一起）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.duplicate"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    if (!duplicatePage())
                                        return fail(err::Page, QStringLiteral("复制页失败"));
                                    return QVariant(pageCount());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.fullscreen"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    toggleFullscreen();
                                    return QVariant(m_fullscreenCell >= 0);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.grid"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int r = params.value(QStringLiteral("rows")).toInt();
                                    const int c = params.value(QStringLiteral("cols")).toInt();
                                    // 上限 8×8（与 main 的选格一致）：也挡住脚本传 100×100
                                    // 造出上万个画布这种"参数合法但不合理"的输入。
                                    if (r < 1 || r > 8 || c < 1 || c > 8)
                                        return fail(QStringLiteral("egrid"), QStringLiteral("行/列须在 1~8 之间"));
                                    if (!setGrid(r, c))
                                        return fail(err::Grid, QStringLiteral("网格规格非法"));
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("rows"), m_rows},
                                                    {QStringLiteral("cols"), m_cols}};
                                    return QVariant(out);
                                });
    // 老 buildLayoutPicker 的 m×n 选择器：点「网格」弹 8×8 悬停选格（抄 main LayoutPickGrid），
    // 选中即走 page.grid 重排；取消返回 false，不动当前布局。
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.grid.pick"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    // 无窗口平台（ctest 的 offscreen）弹不出弹层：明确失败并指路
                                    // page.grid {rows,cols}，绝不阻塞事件循环（否则脚本/测试会挂死）。
                                    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
                                        return fail(QStringLiteral("egui"),
                                                    QStringLiteral("无窗口环境下无法选格，请用 page.grid {rows, cols}"));
                                    // 锚点取鼠标位置：点「网格」大按钮时光标正好在按钮上，
                                    // 等价于"贴按钮弹出"，不必让 MainWindow 传控件指针（少一层耦合）。
                                    const QPoint anchor = params.contains(QStringLiteral("anchor"))
                                        ? params.value(QStringLiteral("anchor")).toPoint()
                                        : QCursor::pos();
                                    int r = 0;
                                    int c = 0;
                                    if (!execGridPick(window(), anchor, m_rows, m_cols, &r, &c))
                                        return QVariant(false); // 用户取消
                                    if (!setGrid(r, c))
                                        return fail(err::Grid, QStringLiteral("网格规格非法"));
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("rows"), r}, {QStringLiteral("cols"), c}};
                                    return QVariant(out);
                                });
    // 老 PlottingRibbon"页面"组的"新建页"：真加一个页签，页内先给一块空画布
    //（对齐老 appendPlusTab 的"+"页行为，而不是把当前页重置）。
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.new"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    const int index = addPage();
                                    m_tabs->setCurrentPage(index);    // 触发 switchPage 载入新页
                                    switchPage(index);                // 兜底（同索引无副作用）
                                    if (m_cells.isEmpty())
                                        addCell();                    // 新页先给一格空画布
                                    return QVariant(pageCount());
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.closeCurrent"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    return QVariant(removePage(m_activePage));
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.rename"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int index = params.contains(QStringLiteral("index"))
                                        ? params.value(QStringLiteral("index")).toInt()
                                        : m_activePage;
                                    const QString title = params.value(QStringLiteral("title")).toString();
                                    if (index < 0 || index >= m_pages.size() || title.isEmpty())
                                        return fail(err::Page, QStringLiteral("页签重命名参数非法"));
                                    m_pages.at(index)->title = title;
                                    m_tabs->setPageTitle(index, title);
                                    return QVariant(title);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.select"),
                                [this](const QVariantMap &params) -> Result<QVariant> {
                                    const int index = params.value(QStringLiteral("index")).toInt();
                                    if (index < 0 || index >= m_pages.size())
                                        return fail(err::Page, QStringLiteral("页索引越界"));
                                    m_tabs->setCurrentPage(index);
                                    switchPage(index);
                                    return QVariant(index);
                                });
    m_appCtx.interactions().registerViewAction(QStringLiteral("page.state"),
                                [this](const QVariantMap &) -> Result<QVariant> {
                                    QVariantMap out{{QStringLiteral("cells"), m_cells.size()},
                                                    {QStringLiteral("active"), m_active},
                                                    {QStringLiteral("rows"), m_rows},
                                                    {QStringLiteral("cols"), m_cols},
                                                    {QStringLiteral("pages"), pageCount()},
                                                    {QStringLiteral("activePage"), m_activePage}};
                                    return QVariant(out);
                                });}
} // namespace nextpilot::plotscope::gui

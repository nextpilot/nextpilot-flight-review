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

// 属性面板 provider 与 setter：曲线/轴/画布三类，按 kind 路由到活动画布。

namespace nextpilot::plotscope::gui
{
void PlotWorkspace::registerPropertyProviders()
{
    // 图例四角 ↔ 属性面板枚举值：0=右上 1=左上 2=左下 3=右下（Legend::Corner 序）。
    static const QStringList kCornerLabels = {QStringLiteral("右上"), QStringLiteral("左上"),
                                              QStringLiteral("左下"), QStringLiteral("右下")};
    auto cornerLabel = [](int corner) {
        return kCornerLabels.value(qBound(0, corner, 3), kCornerLabels.first());
    };
    auto row = [](const QString &key, const QString &label, const QString &value,
                  PropKind kind = PropKind::ReadOnly, bool editable = false) {
        PropRow r;
        r.key = key;
        r.label = label;
        r.value = value;
        r.kind = kind;
        r.editable = editable;
        return r;
    };

    // --- 曲线 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Series, [this, row](const Selection &sel) {
        const QString key = sel.ids.value(QStringLiteral("seriesKey")).toString();
        AxesView *canvas = activeCanvas();
        const PlotSeries *s = canvas ? canvas->seriesByKey(key) : nullptr;
        PropertySchema schema;
        schema.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("series");
        o.label = QStringLiteral("曲线");
        o.rows << row(QStringLiteral("title"), QStringLiteral("标题"), s ? s->title : sel.label);
        o.rows << row(QStringLiteral("channel"), QStringLiteral("通道"),
                      s ? s->legendName() : sel.label);
        if (s)
        {
            o.rows << row(QStringLiteral("visible"), QStringLiteral("显示"),
                          s->visible ? QStringLiteral("true") : QStringLiteral("false"),
                          PropKind::Bool, true);
            o.rows << row(QStringLiteral("color"), QStringLiteral("颜色"), s->color.name(),
                          PropKind::Color, true);
            o.rows << row(QStringLiteral("width"), QStringLiteral("线宽"),
                          QString::number(s->lineWidth, 'f', 2), PropKind::Number, true);
            o.rows << row(QStringLiteral("axis"), QStringLiteral("坐标轴"),
                          s->rightAxis ? QStringLiteral("右 Y 轴") : QStringLiteral("左 Y 轴"),
                          PropKind::Enum, true);
            o.rows[o.rows.size() - 1].options = {QStringLiteral("左 Y 轴"), QStringLiteral("右 Y 轴")};
        }
        schema.objects << o;

        // 变换组：X 轴时间 offset + Y 轴变换（算子/表达式/Lua），按能力过滤，防止选错。
        PropObject tr;
        tr.id = QStringLiteral("transform");
        tr.label = QStringLiteral("变换");
        tr.rows << row(QStringLiteral("xTransform"), QStringLiteral("X 轴时间偏移(s)"),
                       s ? QString::number(s->xTimeOffset, 'f', 3) : QStringLiteral("0.000"),
                       PropKind::Number, true);
        const QString yDesc = s && !s->yTransformId.isEmpty()
            ? m_appCtx.transforms().get(s->yTransformId).label : QStringLiteral("无");
        tr.rows << row(QStringLiteral("yTransform"), QStringLiteral("Y 轴变换"), yDesc,
                       PropKind::Text, true);
        tr.rows.last().tooltip = QStringLiteral("可选算子/表达式/Lua；在右键菜单或脚本里用 "
                                                "plot.series.transform 设置（能力：轴Y）。可选清单见 "
                                                "transforms.idsByCapability(CapAxisY)");
        schema.objects << tr;
        return schema.toJson();
    });

    // --- 轴 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Axis, [this, row](const Selection &sel) {
        const QString axis = sel.ids.value(QStringLiteral("axis")).toString();
        AxesView *canvas = activeCanvas();
        PropertySchema schema;
        schema.title = sel.label;
        PropObject o;
        o.id = QStringLiteral("axis");
        o.label = sel.label;
        o.rows << row(QStringLiteral("id"), QStringLiteral("轴"), axis);
        if (canvas && canvas->plotWidget())
        {
            o.rows << row(QStringLiteral("grid"), QStringLiteral("网格"),
                          canvas->plotWidget()->gridVisible() ? QStringLiteral("true") : QStringLiteral("false"),
                          PropKind::Bool, true);
        }
        const QString tfId = canvas ? canvas->axisTransform(axis) : QString();
        const QString tfLabel = tfId.isEmpty()
            ? QStringLiteral("无（填 Transform id 或算子/表达式/Lua）")
            : m_appCtx.transforms().get(tfId).label;
        if (axis == QStringLiteral("x"))
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("X 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("X 轴变换 id。可选：transforms.idsByCapability(CapAxisX)，"
                                                    "如 builtin.scale 的 time_offset_sec / expression");
        }
        else
        {
            o.rows << row(QStringLiteral("transform"), QStringLiteral("Y 变换"), tfLabel,
                          PropKind::Text, true);
            o.rows.last().tooltip = QStringLiteral("Y 轴变换 id。可选：transforms.idsByCapability(CapAxisY)");
        }
        schema.objects << o;
        return schema.toJson();
    });

    // --- 画布 ---
    m_appCtx.interactions().registerPropertyProvider(SelectionKind::Canvas, [this, row,
                                                                             cornerLabel](const Selection &sel) {
        AxesView *canvas = activeCanvas();
        PropertySchema schema;
        schema.title = QStringLiteral("画布属性");
        PropObject o;
        o.id = QStringLiteral("canvas");
        o.label = QStringLiteral("画布");
        o.rows << row(QStringLiteral("kind"), QStringLiteral("图型"),
                      canvas ? canvas->plotKind() : QString());
        o.rows << row(QStringLiteral("series"), QStringLiteral("曲线数"),
                      QString::number(canvas ? canvas->seriesCount() : 0));
        o.rows << row(QStringLiteral("legend"), QStringLiteral("图例"),
                      canvas && canvas->legendVisible() ? QStringLiteral("true") : QStringLiteral("false"),
                      PropKind::Bool, true);
        o.rows << row(QStringLiteral("legendCorner"), QStringLiteral("图例位置"),
                      canvas ? cornerLabel(canvas->legendCorner()) : cornerLabel(0),
                      PropKind::Enum, true);
        if (canvas)
            o.rows[o.rows.size() - 1].options = {QStringLiteral("右上"), QStringLiteral("左上"),
                                                 QStringLiteral("左下"), QStringLiteral("右下")};
        schema.objects << o;
        return schema.toJson();
    });

    // --- setter：把属性写回活动画布 ---
    m_appCtx.interactions().registerPropertySetter(
        SelectionKind::Series,
        [this](const Selection &sel, const QString &, const QString &key, const QString &value, QString *err) {
            AxesView *canvas = activeCanvas();
            if (!canvas)
            {
                if (err)
                    *err = QStringLiteral("没有活动画布");
                return false;
            }
            const QString k = sel.ids.value(QStringLiteral("seriesKey")).toString();
            if (key == QStringLiteral("visible"))
                return canvas->setSeriesVisible(k, value == QStringLiteral("true"));
            if (key == QStringLiteral("color"))
                return canvas->setSeriesInk(k, QColor(value));
            if (key == QStringLiteral("width"))
                return canvas->setSeriesWidth(k, value.toDouble());
            if (key == QStringLiteral("axis"))
                return canvas->setSeriesAxis(k, value == QStringLiteral("右 Y 轴"));
            if (key == QStringLiteral("xTransform"))
                return canvas->setSeriesXOffset(k, value.toDouble());
            if (key == QStringLiteral("yTransform"))
                return canvas->setSeriesYTransform(k, value);
            if (err)
                *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });

    m_appCtx.interactions().registerPropertySetter(
        SelectionKind::Axis,
        [this](const Selection &sel, const QString &, const QString &key, const QString &value, QString *err) {
            AxesView *canvas = activeCanvas();
            if (!canvas || !canvas->plotWidget())
            {
                if (err)
                    *err = QStringLiteral("没有活动画布");
                return false;
            }
            if (key == QStringLiteral("grid"))
            {
                canvas->plotWidget()->grid(value == QStringLiteral("true"));
                return true;
            }
            if (key == QStringLiteral("transform"))
            {
                // 轴变换通过 TransformHub 的 id 生效；实际执行在曲线内（Task #123）。
                if (!value.isEmpty() && !m_appCtx.transforms().contains(value))
                {
                    if (err)
                        *err = QStringLiteral("未知变换 id: %1").arg(value);
                    return false;
                }
                canvas->setAxisTransform(sel.ids.value(QStringLiteral("axis")).toString(), value);
                return true;
            }
            if (err)
                *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });

    m_appCtx.interactions().registerPropertySetter(
        SelectionKind::Canvas, [this, cornerLabel](const Selection &, const QString &, const QString &key,
                                                   const QString &value, QString *err) {
            AxesView *canvas = activeCanvas();
            if (!canvas)
            {
                if (err)
                    *err = QStringLiteral("没有活动画布");
                return false;
            }
            if (key == QStringLiteral("legend"))
            {
                canvas->setLegendVisible(value == QStringLiteral("true"));
                return true;
            }
            if (key == QStringLiteral("legendCorner"))
            {
                const int idx = kCornerLabels.indexOf(value);
                if (idx < 0)
                {
                    if (err)
                        *err = QStringLiteral("属性未生效：未知图例位置 %1").arg(value);
                    return false;
                }
                canvas->setLegendCorner(idx);
                return true;
            }
            if (err)
                *err = QStringLiteral("未知属性: %1").arg(key);
            return false;
        });
}
} // namespace nextpilot::plotscope::gui

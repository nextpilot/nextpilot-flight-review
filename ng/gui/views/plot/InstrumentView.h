#pragma once

#include "AppContext.h"
#include "IIntentExecutor.h"
#include "Dataset.h"
#include "fastplot/PlotData.h"

#include <QHash>
#include <QVector>
#include <QWidget>
#include <optional>

class QStackedLayout;

namespace nextpilot::plotscope::fastplot
{
class AttitudeIndicator;
class Bar;
class CompassRose;
class DialGauge;
class Histogram;
class Spectrum;
class TrackPlot;
class Trajectory3D;
class WaterfallPlot;
}

namespace nextpilot::plotscope::core
{
}

namespace nextpilot::plotscope::gui
{

// 仪表视图：按 kind 在六个 fastplot 仪表之间切换，取数走 `plot.instrument` 意图。
//
// 分工和 PlotCanvas 一致：
//   - 本类只做"意图 → 填仪表"的适配，不知道数据怎么解析、怎么抽稀；
//   - 仪表在 fastplot 库里，本类不画圆、不画刻度；
//   - 数组缓冲由本类持有，仪表只持指针（零拷贝），缓冲地址在视图存活期内稳定。
class InstrumentView : public QWidget
{
    Q_OBJECT
public:
    InstrumentView(intent::IIntentExecutor &executor, QWidget *parent = nullptr);

    // 发起 plot.instrument 意图，把结果填进对应仪表。
    void renderInstrument(dataset::DatasetId datasetId, const QString &kind,
                          const QVariantMap &roles, const QVariantList &channels,
                          const QVariantList &range);

    QString currentKind() const { return m_kind; }
    QImage grabInstrument();

    // 按 kind 切换当前仪表（供图库 view action 调用）。
    void switchKind(const QString &kind);

    // 从 AppContext 的当前活动通道推断角色并喂数据（gallery 切换后的数据绑定）。
    // 无活动数据集/通道时返回 false，仪表保持空态——不猜、不造假数据。
    bool renderFromActive(const QString &kind);

    // 工厂注入运行态上下文（含数据仓），供 renderFromActive 读列名做角色推断。
    void setAppContext(context::AppContext *ctx) { m_appCtx = ctx; }

private:
    struct RoleBuffer
    {
        QVector<double> time;
        QVector<double> value;
        std::optional<nextpilot::plotscope::fastplot::ArrayData> data;
    };

    const nextpilot::plotscope::fastplot::ArrayData *bindRole(const QString &role, const QVariantMap &series);

    intent::IIntentExecutor &m_executor;
    context::AppContext *m_appCtx = nullptr; // 非拥有；由工厂注入
    QStackedLayout *m_stack = nullptr;

    nextpilot::plotscope::fastplot::AttitudeIndicator *m_attitude = nullptr;
    nextpilot::plotscope::fastplot::Bar *m_bar = nullptr;
    nextpilot::plotscope::fastplot::CompassRose *m_compass = nullptr;
    nextpilot::plotscope::fastplot::DialGauge *m_gauge = nullptr;
    nextpilot::plotscope::fastplot::Histogram *m_histogram = nullptr;
    nextpilot::plotscope::fastplot::Spectrum *m_spectrum = nullptr;
    nextpilot::plotscope::fastplot::TrackPlot *m_track = nullptr;
    nextpilot::plotscope::fastplot::Trajectory3D *m_traj = nullptr;
    nextpilot::plotscope::fastplot::WaterfallPlot *m_waterfall = nullptr;

    QString m_kind;
    quint64 m_revision = 0;
    QHash<QString, RoleBuffer> m_buffers;

    // 瀑布序列的缓冲：reserve 后只 resize，容量不变，数组地址稳定（扩容搬移会让指针变野）。
    QVector<RoleBuffer> m_wfBuffers;
    QVector<nextpilot::plotscope::fastplot::ArrayData> m_wfData;
};

// 工厂：视图层自注册进部件表，器官按 id 请求即可，外壳不认识这个类。
QWidget *createInstrumentPanel(context::AppContext &appCtx, intent::IIntentExecutor &executor,
                               QWidget *parent);

} // namespace nextpilot::plotscope::gui

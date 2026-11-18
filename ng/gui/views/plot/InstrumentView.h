#pragma once

#include "Dataset.h"
#include "fastplot/PlotData.h"

#include <QHash>
#include <QVector>
#include <QWidget>
#include <optional>

class QStackedLayout;

namespace fastplot
{
class AttitudeIndicator;
class CompassRose;
class DialGauge;
class TrackPlot;
class Trajectory3D;
class WaterfallPlot;
}

namespace ng::core
{
class AppContext;
class IIntentExecutor;
}

namespace ng::gui
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
    InstrumentView(core::IIntentExecutor &executor, QWidget *parent = nullptr);

    // 发起 plot.instrument 意图，把结果填进对应仪表。
    void renderInstrument(core::DatasetId datasetId, const QString &kind,
                          const QVariantMap &roles, const QVariantList &channels,
                          const QVariantList &range);

    QString currentKind() const { return m_kind; }
    QImage grabInstrument();

private:
    struct RoleBuffer
    {
        QVector<double> time;
        QVector<double> value;
        std::optional<fastplot::ArrayData> data;
    };

    void switchKind(const QString &kind);
    const fastplot::ArrayData *bindRole(const QString &role, const QVariantMap &series);

    core::IIntentExecutor &m_executor;
    QStackedLayout *m_stack = nullptr;

    fastplot::AttitudeIndicator *m_attitude = nullptr;
    fastplot::CompassRose *m_compass = nullptr;
    fastplot::DialGauge *m_gauge = nullptr;
    fastplot::TrackPlot *m_track = nullptr;
    fastplot::Trajectory3D *m_traj = nullptr;
    fastplot::WaterfallPlot *m_waterfall = nullptr;

    QString m_kind;
    quint64 m_revision = 0;
    QHash<QString, RoleBuffer> m_buffers;

    // 瀑布序列的缓冲：reserve 后只 resize，容量不变，数组地址稳定（扩容搬移会让指针变野）。
    QVector<RoleBuffer> m_wfBuffers;
    QVector<fastplot::ArrayData> m_wfData;
};

// 工厂：视图层自注册进部件表，器官按 id 请求即可，外壳不认识这个类。
QWidget *createInstrumentPanel(core::AppContext &appCtx, core::IIntentExecutor &executor,
                               QWidget *parent);

} // namespace ng::gui

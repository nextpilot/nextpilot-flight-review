#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "PlotSeries.h"

namespace nextpilot::plotscope::fastplot
{
class Axes;
}

namespace nextpilot::plotscope::gui
{

class SeriesStore;

// 读数与测量叠加层的视图对象：把"游标落在哪个时间、A/B 量程、书签标记"
// 换算成 fastplot 的读数框/直角三角形/书签 chip。
// 只管展示与采样，不碰鼠标状态机（那属于 Interaction）。
class CursorReadout : public QObject
{
    Q_OBJECT

public:
    struct SampledValue
    {
        bool ok = false;
        double value = 0.0;
    };
    // 书签在读数层只需要"位置 + 文案 + 是否选中"。
    struct Mark
    {
        QString id;
        double t = 0.0;
        QString label;
        bool selected = false;
    };

    explicit CursorReadout(SeriesStore *store, QObject *parent = nullptr);

    // 绑定的绘图对象（宿主构造完 Axes 后注入）。
    void setPlot(nextpilot::plotscope::fastplot::Axes *plot);

    // ---- 游标 ----
    double cursorTime() const { return m_t; }
    bool active() const { return m_active; }
    bool pinned() const { return m_pinned; }
    void setCursorTime(double t) { m_t = t; }
    void setActive(bool on) { m_active = on; }
    void setPinned(bool on) { m_pinned = on; }

    // ---- A/B 量程 ----
    bool abEnabled() const { return m_abEnabled; }
    double abA() const { return m_abA; }
    double abB() const { return m_abB; }
    bool abHasB() const { return m_abHasB; }
    void setABEnabled(bool on) { m_abEnabled = on; }
    void setABStart(double a) { m_abA = a; }
    void setABEnd(double b) { m_abB = b; }
    void setABHasB(bool on) { m_abHasB = on; }
    void setABRange(double a, double b, bool hasB)
    {
        m_abA = a;
        m_abB = b;
        m_abHasB = hasB;
    }

    // 从一条曲线取 t 时刻的最近样本（有界窗口，不插值）。
    SampledValue sample(const PlotSeries &s, double t) const;

    // 重写叠加层：游标读数框 + A/B 读数与斜率 + 书签 chip。
    void refresh(const QVector<Mark> &marks = {});

private:
    SeriesStore *m_store = nullptr;
    nextpilot::plotscope::fastplot::Axes *m_plot = nullptr;

    double m_t = 0.0;
    bool m_active = false;
    bool m_pinned = false;

    bool m_abEnabled = false;
    double m_abA = 0.0;
    double m_abB = 0.0;
    bool m_abHasB = false;
};

} // namespace nextpilot::plotscope::gui

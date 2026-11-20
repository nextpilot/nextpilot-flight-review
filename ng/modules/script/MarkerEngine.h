#pragma once

#include "Result.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::foundation::Result;

// PlotMarker 种类（对齐 PJ4 sdk::MarkerKind）。
enum class MarkerKind
{
    Event,      // 时间点/竖线/带值点
    Region,     // 时间区间 [t_start, t_end]
    ValueBand,  // 值区间 [value_low, value_high]（横线是其退化形）
};

enum class MarkerSeverity
{
    Info,
    Warning,
    Error,
    Critical,
};

// 一条标记（对齐 PJ4 sdk::PlotMarker；时间单位为秒——NG 全局契约）。
struct PlotMarker
{
    MarkerKind kind = MarkerKind::Event;
    double tStart = 0.0;
    double tEnd = 0.0;         // Region 用
    double valueLow = 0.0;
    double valueHigh = 0.0;    // ValueBand 用
    bool hasValue = false;     // Event 是否携带值（点 vs 竖线）
    MarkerSeverity severity = MarkerSeverity::Info;
    QString label;
    QString description;
    QString category;
    QString status;            // pass/fail/空
    QString color;             // "#rrggbb" / 颜色名；空=用 severity 色

    QJsonObject toJson() const;
    static PlotMarker fromJson(const QJsonObject &o);
};

QJsonArray markersToJson(const QVector<PlotMarker> &markers);
QVector<PlotMarker> markersFromJson(const QJsonArray &arr);

// 只读序列视图：脚本经 series("name") 拿到它（:size()/:at(i)/:atTime(t)）。
struct MarkerSeries
{
    QString name;
    QVector<double> time;   // 秒
    QVector<double> value;

    double atTime(double t) const; // 线性插值，端点钳制；NaN/空→0
};

// 序列提供者：调用方（host）按名字给出序列；引擎不依赖数据源实现。
struct MarkerSeriesProvider
{
    QStringList names;
    std::function<const MarkerSeries *(const QString &name)> get;
};

// 整序列 -> markers 的 Luau/Lua 规则引擎（对齐 PJ4 runMarkerScript）。
// 脚本一次运行扫完整条序列，可前后 lookback/lookforward，产出 PlotMarker。
// 绑定原语见实现：series/GetSeriesNames/createMarker 家族/startMarker/closeMarker/
// bandPower 占位/when DSL(above/below/between/outside/equals/isTrue/changes +
// backgroundColor/event/verticalLine)。
class MarkerEngine
{
public:
    struct Limits
    {
        int instructionBudget = 200000; // 看门狗：hook 次数上限
        int maxMarkers = 10000;         // 单规则最多产出标记数，防灌爆
    };

    MarkerEngine();
    explicit MarkerEngine(Limits limits);
    ~MarkerEngine();

    // 跑一条规则。失败返回空 markers + 错误码/消息（编译错/运行错/看门狗/输出超限）。
    Result<QVector<PlotMarker>> run(const QByteArray &code,
                                    const MarkerSeriesProvider &provider) const;

private:
    Limits m_limits;
};

} // namespace nextpilot::plotscope::script

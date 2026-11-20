#pragma once

#include "Result.h"
#include "Transform.h"

#include <QJsonObject>
#include <QVector>

namespace nextpilot::plotscope::script
{

// 统一变换求值器：三场景（数据源衍生 / 曲线轴变换 / 协议 checksum）共用的
// 唯一入口。调用方只给 TransformRequest（descriptorId + 载荷 + 参数），
// 不关心在哪执行。
//
// 路由（用户定稿）：
//   · Builtin / Expression -> Rust kernel 的 ps_filter_apply_series
//     （数据源侧的算子与数学表达式都在 Rust，有缓存、高效）；
//   · Lua                  -> C++ LuaTransform
//     （checksum / 曲线的 Lua 走 C++，便于在线调试单点/批量）。
//
// 能力校验：求值前检查 descriptor 是否支持目标能力（cap），不匹配直接报错——
// 与 UI 的按能力过滤形成双保险，"选错了就死了"在 UI 和运行时都被挡住。
class TransformEvaluator
{
public:
    // 单点求值：live 数据源 / checksum / 在线调试单点输入。
    // cap: 目标能力（如 CapAxisY / CapChecksum），用于运行时防错配。
    static nextpilot::plotscope::foundation::Result<double> evalScalar(
        const nextpilot::plotscope::uischema::TransformRegistry &transforms,
        const QJsonObject &requestJson, uint32_t cap,
        double x, double t, int index);

    // 批量求值：offline 数据源 / 在线调试批量输入 / 曲线整段 Y 变换。
    static nextpilot::plotscope::foundation::Result<QVector<double>> evalSeries(
        const nextpilot::plotscope::uischema::TransformRegistry &transforms,
        const QJsonObject &requestJson, uint32_t cap,
        const QVector<double> &t, const QVector<double> &v);

    // 便捷：构造一份 TransformRequest JSON。
    static QJsonObject makeRequest(const nextpilot::plotscope::uischema::TransformRegistry &transforms,
                                   const QString &descriptorId,
                                   const QString &expression,
                                   const QString &luaSource,
                                   const QString &scriptId,
                                   const QJsonObject &params);
};

} // namespace nextpilot::plotscope::script

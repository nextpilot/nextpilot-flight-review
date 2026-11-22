#pragma once

#include "Result.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include <functional>

namespace nextpilot::plotscope::uischema
{

using nextpilot::plotscope::foundation::Result;

// 有状态过滤器能力契约。
// script 模块实现并注入 InteractionHub；host(data.filter)/gui(过滤器编辑) 只用这组 JSON 回调，
// 从而不反向依赖 script 模块。全部纯 Qt/core 类型，headless 可测。
//
// descriptor JSON 形状（供 GUI 生成表单，字段与 ParamSpec 对齐）：
//   { id, name, description, version, outputKind,
//     parameters: [ { name, type: "number|int|bool|enum|string|text",
//                     label, tooltip, default, min, max, step, decimals, unit,
//                     values: [{value,label}], visibleWhenParam, visibleWhenEquals } ] }
struct FilterOps
{
    // 列出全部已编目过滤器（descriptor JSON 数组）。
    using ListFn = std::function<QJsonArray()>;
    // 单个过滤器 descriptor；不存在时返回 fail("enoent")。
    using DescribeFn = std::function<Result<QJsonObject>(const QString &id)>;
    // 对一条序列逐样本应用过滤器。t/v 为绝对时间/值；实现内部按首样本重基为相对时间。
    // 返回 { time:[f64...], value:[f64...] }，已处理 suppress/显式时间。
    using ApplyFn = std::function<Result<QJsonObject>(const QString &id, const QJsonObject &params,
                                                      const QVector<double> &t, const QVector<double> &v)>;

    ListFn list;
    DescribeFn describe;
    ApplyFn applySeries;

    bool valid() const { return static_cast<bool>(list) && static_cast<bool>(describe) && static_cast<bool>(applySeries); }
};

} // namespace nextpilot::plotscope::uischema

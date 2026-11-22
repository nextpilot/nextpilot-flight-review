#pragma once

#include "Result.h"

#include <QJsonArray>
#include <QJsonObject>

#include <functional>

namespace nextpilot::plotscope::uischema
{

using nextpilot::plotscope::foundation::Result;

// Marker 规则能力契约。script 模块实现并注入；
// host(data.marker)/脚本/app 只用这组 JSON 回调，不反向依赖 script 模块。
// seriesJson 形状：[ { name, time:[f64秒...], value:[f64...] }, ... ]
// 返回 markers JSON 数组（PlotMarker::toJson 形状）。
struct MarkerOps
{
    using RunFn = std::function<Result<QJsonArray>(const QByteArray &code, const QJsonArray &seriesJson)>;

    RunFn run;

    bool valid() const { return static_cast<bool>(run); }
};

} // namespace nextpilot::plotscope::uischema

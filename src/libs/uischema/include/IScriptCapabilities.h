#pragma once

// A1b §1.2.1（IKXUTD 评论 52435597）：InteractionHub 的脚本能力面抽接口。
// 服务 id "script.capabilities"；装配层 adapter 持有 InteractionHub& + AppContext& 只转发，
// 不新增逻辑。参数类型全部是 uischema/foundation 既有类型。
//
// 模块侧只依赖本接口，不接触 InteractionHub / AppContext / RuntimeHost。

#include "Result.h"
#include "FilterOps.h"
#include "MarkerOps.h"
#include "Transform.h"
#include "IIntentExecutor.h"
#include "JobRecorder.h"

#include <QJsonObject>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

namespace nextpilot::plotscope::dataset {
class DatasetHub;
class SourceHub;
} // namespace nextpilot::plotscope::dataset
namespace nextpilot::plotscope::context {
class InteractionHub;
} // namespace nextpilot::plotscope::context

namespace nextpilot::plotscope::uischema {

using nextpilot::plotscope::foundation::Result;

class IScriptCapabilities
{
public:
    virtual ~IScriptCapabilities() = default;

    using TransformEvalFn = std::function<Result<QVector<double>>(const QJsonObject &request, uint32_t cap,
                                                                  const QVector<double> &t, const QVector<double> &v)>;
    using OpCatalogueFn = std::function<QStringList()>; // 与 InteractionHub.h:92 同签名

    virtual void registerTransformEvaluator(TransformEvalFn fn) = 0;
    virtual bool hasTransformEvaluator() const = 0;
    virtual bool evalTransform(const QJsonObject &req, uint32_t cap, const QVector<double> &t, const QVector<double> &v,
                               Result<QVector<double>> &out) const = 0;

    virtual void registerFilterOps(const FilterOps &ops) = 0;
    virtual bool hasFilterOps() const = 0;
    virtual const FilterOps &filterOps() const = 0;

    virtual void registerMarkerOps(const MarkerOps &ops) = 0;
    virtual bool hasMarkerOps() const = 0;
    virtual const MarkerOps &markerOps() const = 0;

    virtual void registerTransformScriptOps(const TransformScriptOps &ops) = 0;
    virtual bool hasTransformScriptOps() const = 0;
    virtual const TransformScriptOps &transformScriptOps() const = 0;

    virtual void registerOpCatalogue(OpCatalogueFn fn) = 0;
    virtual QStringList opCatalogue() const = 0;
    virtual TransformHub &transforms() = 0;

    // 视图动作与消息流：script 模块的 registerWorkbenchActions 在 initialize 路径里
    // 注册 view action（GUI 侧仍经 dispatchViewAction 消费，A2b 才迁到 command），
    // 并写结构化消息流。按名给出，adapter 纯转发，避免模块 TU 出现 host.app()。
    using ViewActionHandler = std::function<Result<QVariant>(const QVariantMap &params)>;
    virtual void registerViewAction(const QString &id, ViewActionHandler handler) = 0;
    // 注销：模块卸载/面板销毁时调用，命中返回 true。
    virtual bool unregisterViewAction(const QString &id) = 0;
    virtual bool dispatchViewAction(const QString &id, const QVariantMap &params, Result<QVariant> &out) const = 0;
    virtual void appendMessage(const QString &level, const QString &source, const QString &text,
                               const QString &locator = QString()) = 0;

    // 脚本执行器的两个基础依赖：脚本 ps.call 走宿主执行器（全部意图池）；
    // 录制器用于 script.run 录制业务意图。adapter 转发到既有实例，不新建逻辑。
    virtual nextpilot::plotscope::intent::IIntentExecutor &executor() = 0;
    virtual nextpilot::plotscope::intent::JobRecorder *recorder() = 0;

    // BatchHandler/PythonHandler 的构造仍收具体 Hub 指针（B 组冻结、逻辑不动）。
    // 这里按名给出这些实例的引用，模块 TU 不必写 host.app()、也不 include AppContext.h。
    virtual nextpilot::plotscope::dataset::DatasetHub &dataHub() = 0;
    virtual nextpilot::plotscope::dataset::SourceHub &sourcesHub() = 0;
    virtual nextpilot::plotscope::context::InteractionHub &interactionsHub() = 0;
};

} // namespace nextpilot::plotscope::uischema

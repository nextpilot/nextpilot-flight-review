#pragma once

#include "Dataset.h"
#include "IIntentExecutor.h"
#include "Intent.h"
#include "JobRecorder.h"
#include "Result.h"

#include <optional>
#include <QString>
#include <QVariant>

namespace nextpilot::plotscope::context
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::dataset::Dataset;
using nextpilot::plotscope::dataset::DatasetId;
using nextpilot::plotscope::dataset::kInvalidDataset;
using nextpilot::plotscope::intent::IIntentExecutor;
using nextpilot::plotscope::intent::JobRecorder;
using nextpilot::plotscope::intent::Intent;
using nextpilot::plotscope::intent::IntentResult;
using nextpilot::plotscope::foundation::fail;


// 脚本门面：脚本唯一能拿到对象。它把 ps.* 调用翻译成 Intent 交给执行器，
// 并可选地把业务意图录进 JobRecorder。脚本永远碰不到 QWidget 和模块内部对象，
// 因此天然 headless-safe。
class ScriptFacade
{
public:
    ScriptFacade(IIntentExecutor &executor, JobRecorder *recorder = nullptr);

    // 通用意图提交：op + 参数表 -> 结果 payload。
    Result<QVariant> call(const QString &op, const QVariantMap &params);

    // 语义化门面（脚本里写起来更像 API，本质仍是生成同样的 Intent）。
    Result<DatasetId> loadDataset(const QString &path, const QString &format = QStringLiteral("csv"),
                                  std::optional<DatasetId> stableId = std::nullopt);
    Result<QVariantMap> renderPlot(DatasetId id, const QString &channel = QString());

private:
    IIntentExecutor &m_executor;
    JobRecorder *m_recorder = nullptr;
};

} // namespace nextpilot::plotscope::context

#pragma once

// A1b §1.2：摄取管道强类型接缝。方法集按施工令冻结：feed/bindParser/finish/channels。
// 通道类型 dataset::IngestChannel（见 dataset/IngestChannel.h），签名只出现 libs 类型。

#include "Dataset.h"      // DatasetId / kInvalidDataset
#include "Subscription.h"
#include "IngestChannel.h"
#include "Result.h"       // Result<int>
#include "Source.h"       // ParserBinding

#include <QVector>

#include <functional>

namespace nextpilot::plotscope::dataset
{

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::workbench::Subscription;

class IIngest
{
public:
    virtual ~IIngest() = default;

    virtual Result<int> bindParser(int sourceId, const ParserBinding &binding,
                                   DatasetId preferredDatasetId = kInvalidDataset) = 0;
    virtual qint64 feed(int sourceId, const uint8_t *data, qint64 size) = 0;
    virtual void finish(int sourceId) = 0;
    virtual QVector<IngestChannel> channels(int sourceId) const = 0;

    // §1.3.1 事件（VSCode Event 模式）：装配层从摄取管道/AppContext 转发。
    // 加载生命周期属摄取域，不抬到 workspace 全局（评论 52436915 裁定）。
    virtual Subscription onDidStartLoad(std::function<void()> l) = 0;
    virtual Subscription onDidFinishLoad(std::function<void(bool ok)> l) = 0;
    // 进度：done/total 折算成 0-100 与文案。
    virtual Subscription onDidReportProgress(
        std::function<void(int, const QString &)> l) = 0;
};

} // namespace nextpilot::plotscope::dataset

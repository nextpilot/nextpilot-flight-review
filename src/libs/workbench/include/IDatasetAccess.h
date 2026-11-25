#pragma once

// A1b §1.2：数据集只读/受限可变的强类型接缝（VSCode workspace 文档视图对位）。
// 模块只认这个接口，拿不到 DatasetHub 容器本身（装卸不牵连）。
// 签名只出现 libs 类型；本头在 libs/workbench，不依赖 context。

#include "Dataset.h"
#include "Subscription.h"

#include <QVector>

#include <functional>

namespace nextpilot::plotscope::dataset
{

class IDatasetAccess
{
public:
    virtual ~IDatasetAccess() = default;

    virtual const Dataset *get(DatasetId id) const = 0;
    virtual QVector<DatasetId> ids() const = 0;
    virtual int count() const = 0;
    // 可变访问仅限宿主级维护（派生刷新/回放 appendFrame）；模块正常路径不用。
    virtual Dataset *getMutable(DatasetId id) = 0;
    virtual bool remove(DatasetId id) = 0;

    // §1.3.1 数据变更事件（VSCode Event 模式）：返回可撤销句柄。
    virtual nextpilot::plotscope::workbench::Subscription onDidAddDocument(
        std::function<void(const QString &id)> l) = 0;
    virtual nextpilot::plotscope::workbench::Subscription onDidRemoveDocument(
        std::function<void(const QString &id)> l) = 0;
};

} // namespace nextpilot::plotscope::dataset

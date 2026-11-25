#pragma once

// A1b §1.2：数据源运行态强类型接缝。方法集按施工令冻结的实测调用面，一个不多一个不少。
// 签名只出现 libs 类型；不依赖 context。

#include "DataSource.h"
#include "Subscription.h"
#include "Source.h"

#include <QString>
#include <QVector>

#include <functional>

namespace nextpilot::plotscope::dataset {

class ISourceAccess
{
public:
    virtual ~ISourceAccess() = default;

    virtual const DataSourceInfo *get(int id) const = 0;
    virtual int sourceOfDataset(int datasetId) const = 0;
    virtual QVector<int> ids() const = 0;
    virtual bool remove(int id, QVector<int> *datasetIds = nullptr) = 0;
    virtual int sourceByOrigin(const QString &origin) const = 0;
    virtual bool setState(int id, SourceState state, const QString &error = QString()) = 0;
    virtual void releaseOrigin(const QString &origin) = 0;
    virtual bool addDataset(int id, int datasetId) = 0;
    virtual int add(DataSourceInfo info) = 0;
    virtual bool update(int id, const DataSourceInfo &info) = 0;
    virtual void demandOrigin(const QString &origin) = 0;
    virtual int count() const = 0;

    // §1.3.1 数据变更事件（VSCode Event 模式）。
    virtual nextpilot::plotscope::workbench::Subscription onDidAddSource(std::function<void(const QString &id)> l) = 0;
    virtual nextpilot::plotscope::workbench::Subscription
    onDidChangeSource(std::function<void(const QString &id)> l) = 0;
};

} // namespace nextpilot::plotscope::dataset

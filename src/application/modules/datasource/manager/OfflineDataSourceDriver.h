#pragma once

#include "DataSourceConfig.h"
#include "DataSourceDriver.h"
#include <QVector>

#include <cstdint>
#include <functional>

struct PsDataset;
struct PsChannelInfo;

namespace ps::data_source
{

// 离线源驱动：负责 Dataset 的所有权、加载与关闭。
// 选项对话框/进度窗口仍属于 UI 层，这里只接收进度回调。
class OfflineDataSourceDriver : public DataSourceDriver
{
public:
    using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;

    explicit OfflineDataSourceDriver(PsDataset *ds = nullptr);
    ~OfflineDataSourceDriver() override;

    bool open(DataSource &src, QString *err) override;
    void close(DataSource &src) override;
    bool poll(DataSource &src, QString *err) override;
    bool reload(DataSource &src, QString *err) override;

    // 同步加载：先尝试解析缓存，再走 loader；optionJson 由 UI 选择。
    PsDataset *load(const DataSourceConfig &config, const QString &optionJson,
                    bool *cacheHit, QString *err, const ProgressFn &progress = {});
    void adopt(PsDataset *ds);

    PsDataset *dataset() const { return m_ds; }

private:
    PsDataset *m_ds = nullptr;
};

} // namespace ps::data_source

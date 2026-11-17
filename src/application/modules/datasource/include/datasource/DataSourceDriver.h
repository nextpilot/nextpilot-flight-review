#pragma once

#include <QString>

namespace ps::data_source
{

struct DataSource;

// 驱动只负责“打开/关闭/推进一步/重载”，不负责解析后的 UI 状态。
// 离线驱动包装 loader；实时驱动包装扩展 handle + sink。
class DataSourceDriver
{
public:
    virtual ~DataSourceDriver() = default;

    virtual bool open(DataSource &src, QString *err) = 0;
    virtual void close(DataSource &src) = 0;
    virtual bool poll(DataSource &src, QString *err) = 0;
    virtual bool reload(DataSource &src, QString *err) = 0;
};

} // namespace ps::data_source

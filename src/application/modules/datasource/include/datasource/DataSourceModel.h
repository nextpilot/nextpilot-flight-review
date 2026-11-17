#pragma once

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <memory>

#include "DataSourceConfig.h"
#include "DataSourceDriver.h"

struct PsDataset;
struct PsChannelInfo;
class LiveFrameSink;

namespace ps::data_source
{

// 变换配方：写入 .psl，重启/导入时按序重放
struct TransformRecipe
{
    QString kind;             // filter / quat
    QString input;
    QString filter;
    QJsonObject params;
    QString output;           // filter 输出名 / quat 输出主题
    QString quat[4];
    bool degrees = true;
    bool unwrap = true;
};

// 单个数据源实例：配置 + Dataset + 驱动。
// 运行期解析状态/UI 行/实时句柄留在实例内，但不进入 DataSourceConfig。
struct DataSource
{
    DataSourceConfig config;

    // 实际数据；删除源时释放。
    PsDataset *ds = nullptr;
    QVector<const PsChannelInfo *> channels;
    QVector<QPair<QString, QString>> params;
    QVector<QStringList> msgs;
    QStringList derived;       // 本源由变换产生的通道名，右键才允许删除
    QVector<TransformRecipe> recipes;
    QWidget *row = nullptr;

    // 实时运行期：handle 归扩展所有，生命周期由 LiveDataSourceDriver 管。
    bool paused = false;       // 仅实时源：暂停轮询
    bool plotted = false;
    void *handle = nullptr;
    std::shared_ptr<LiveFrameSink> liveSink;
    std::shared_ptr<DataSourceDriver> driver;
    QVector<QPair<QString, void *>> fanout; // 共享 transport 下的外部解析器句柄

    // 每次重载/关闭递增；Canvas 用它判断旧 Table/Column 是否已失效。
    uint64_t generation = 0;

    bool isLive() const { return config.isLive(); }
};

} // namespace ps::data_source

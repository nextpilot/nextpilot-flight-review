#pragma once

#include <QJsonObject>
#include <QString>

namespace ps::data_source
{

enum class DataSourceMode
{
    Offline,
    Live
};

// 数据源的可持久化描述：路径/扩展/选项/显示偏移/稳定 key。
// 运行期句柄、Dataset、解析状态不放在这里，避免配置对象变成大杂烩。
struct DataSourceConfig
{
    int id = -1;
    QString name;
    QString alias;             // 图例前缀，默认文件名，可重命名
    QString path;
    QString fmt;
    QString sourceKey;         // 持久化稳定 key：路径 / 设备身份 / 用户别名
    double offsetSec = 0.0;    // 显示时间 = 原始时间 - offset；多源对齐用
    DataSourceMode mode = DataSourceMode::Offline;
    QString extensionId;       // 实时源/loader 扩展 id
    QString configJson;        // 扩展连接/选项 JSON

    bool isLive() const { return mode == DataSourceMode::Live; }

    QJsonObject toJson() const;
    static DataSourceConfig fromJson(const QJsonObject &o);
};

} // namespace ps::data_source

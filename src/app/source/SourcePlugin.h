#pragma once

#include "ps_plugin_api.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <functional>

// data_source 的宿主侧描述。和 LoaderPlugin 对等：能力、schema、生命周期
// 都由插件描述符提供，app 不认识 MAVLink / ROS / OEM7 的具体配置字段。
struct SourcePlugin
{
    QString id;
    QString version;
    QString label;
    QString kind;
    QString transport;
    bool recordable = false;
    uint32_t capabilities = 0;
    QJsonObject schema;

    std::function<QString()> defaultConfig;
    std::function<int(const QString &config, const PsSourceSink *sink, void **handle,
                      QString *err)> open;
    std::function<int(void *handle, int timeoutMs, QString *err)> poll;
    std::function<void(void *handle)> close;
    std::function<QString(void *handle)> status;
};


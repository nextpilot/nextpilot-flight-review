#pragma once

#include "ps_extension_api.h"

#include <QJsonObject>
#include <QString>
#include <QByteArray>
#include <QStringList>

#include <cstdint>
#include <functional>

struct PsDataset;
class QDialog;
class QWidget;

// 数据输入扩展的统一宿主描述。
//
// 文件加载器（data_loader）和实时源（data_source）共享 id/version/label/schema/
// capabilities 这些公共元数据；差异只留在各自的回调组：
//   loader 组：probe / defaultOptions / paramsJson / load
//   source 组：defaultConfig / open / poll / close / status / send
// 调用侧统一使用 DataSourceEntry；通过 family / 回调组区分 loader 与 source。
struct DataSourceEntry
{
    // ---- 公共元数据 ----
    QString id;
    QString version;                // manifest version，缓存命名空间用
    QString family;                 // data_loader / data_source 等
    QString label;
    uint32_t capabilities = 0;      // PS_CAP_* / PS_INPUT_*
    QJsonObject schema;             // 空对象 = 无选项，直接加载

    // ---- 文件加载器 ----
    QString cacheNamespace;         // id@version；扩展升级后旧缓存自然失效
    QString filter;                 // QFileDialog 过滤器
    QStringList extensions;         // 扩展声明的扩展名
    QString formatTag;              // 数据源列表显示名
    bool tree = false;              // 是否有 topic/ID 层级
    bool hasParams = false;         // 是否有 P/Q、PARM 参数表
    bool cacheable = false;         // 是否允许写解析缓存
    QString uiDialog;               // 扩展自带 .ui 绝对路径，空 = 用通用 schema 对话框
    // 组件自带的配置编辑器；base UI 只调用，不 include 组件头。
    std::function<QDialog *(const QString &path, QWidget *parent)> edit;

    std::function<QJsonObject(const QString &path, const QJsonObject &opts)> probe;
    std::function<QString(const QString &path)> defaultOptions;
    std::function<QString(const QString &path)> paramsJson;
    std::function<QString(const QString &widget)> uiData;
    std::function<int(const QString &path, const QString &opts, PsDataset **ds,
                      char *err, unsigned int errCap)> load;

    // ---- 实时 / 回放数据源 ----
    QString kind;
    QString transport;
    bool recordable = false;
    bool replayable = false;
    QString replayExtensions;
    std::function<QString()> defaultConfig;
    std::function<int(const QString &config, const PsSourceSink *sink, void **handle,
                      QString *err)> open;
    std::function<int(void *handle, int timeoutMs, QString *err)> poll;
    std::function<void(void *handle)> close;
    std::function<QString(void *handle)> status;
    std::function<QString(void *handle, const QString &command)> send; // 可选：设备调试命令
    std::function<int(void *handle, const QString &topic, const QString &encoding,
                      qint64 timestampNs, const QByteArray &data, QString *err)> feed; // 可选：共享 transport 喂字节
};

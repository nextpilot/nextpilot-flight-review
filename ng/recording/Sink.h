#pragma once

#include "RecordingBackend.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace ng::rec
{

// 录制输出端（后端）：把**已经成帧的字节**送到目的地。
//
// 这是「格式 × 后端」正交组合的后端一半：
//   格式（raw / mcap / 插件自定义）决定字节长什么样；
//   后端（file / udp / 插件自定义）决定字节送到哪。
//
// 于是同一份格式可以送到不同目的地，同一个目的地也可以收不同格式——
// 「同时录制多种格式、多种后端」不是特例分支，而是两个独立注册表的组合。
//
// 后端只搬运字节，不理解内容；格式只负责成帧，不关心去向。
class IByteSink
{
public:
    virtual ~IByteSink() = default;

    // 打开目的地。endpoint 语义由后端自己解释：
    //   file → 文件路径；udp → "host:port"。
    virtual bool open(const QString &endpoint, QString *err) = 0;

    // 追加字节。
    virtual bool write(const char *data, qint64 size, QString *err) = 0;

    // 追加一段字节。
    bool write(const QByteArray &data, QString *err)
    {
        if (data.isEmpty())
            return true;
        return write(data.constData(), data.size(), err);
    }

    // 冲刷缓冲（网络后端用于尽快送出）。
    virtual bool flush(QString *err) { (void)err; return true; }

    // 关闭并收尾。
    virtual bool close(QString *err) = 0;

    // 已写出的字节数。
    virtual qint64 bytesWritten() const = 0;
};

// 按后端 id 创建输出端（内置 file / udp）。
// 未知 id 返回 nullptr（调用方据此报"后端未注册"）。
std::unique_ptr<IByteSink> createByteSink(const QString &backendId, QString *err);

} // namespace ng::rec

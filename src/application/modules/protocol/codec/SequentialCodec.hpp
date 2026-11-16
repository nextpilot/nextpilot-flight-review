#pragma once

#include "ps_extension_api.h"
#include "protocol/ProtocolDocument.h"
#include <QString>
#include <QStringList>

#include <QByteArray>

#include <cstdint>

/* 顺序帧协议（layout:"sequential"）解码入口。
 * 模板 JSON + 原始帧字节 -> 通用 dataset。 */
bool looksSequentialProtocol(const QByteArray &json);

struct SequentialFramePreview
{
    bool ok = false;
    QString error;
    QStringList lines;
};

/* 编辑器试算：用与 loader 相同的段/变体/校验逻辑解码单帧。 */
SequentialFramePreview previewSequentialFrame(const QByteArray &frame,
                                              const ps::protocol::ProtocolDocument &doc);

int32_t decodeSequentialFrames(const QByteArray &bytes, const QByteArray &json,
                               const PsHostApi *host, void **dataset,
                               char *err, uint32_t errCap);

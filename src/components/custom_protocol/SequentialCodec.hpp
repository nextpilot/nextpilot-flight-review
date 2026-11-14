#pragma once

#include "ps_plugin_api.h"

#include <QByteArray>

#include <cstdint>

/* 顺序帧协议（layout:"sequential"）解码入口。
 * 模板 JSON + 原始帧字节 -> 通用 dataset。 */
bool looksSequentialProtocol(const QByteArray &json);

int32_t decodeSequentialFrames(const QByteArray &bytes, const QByteArray &json,
                               const PsHostApi *host, void **dataset,
                               char *err, uint32_t errCap);

#pragma once

#include "ps_extension_api.h"

// 宿主函数表：扩展只拿到这张表，不直接链接 kernel。
extern "C" const PsHostApi *ps_host_api(void);

/* 宿主在单次 load 调用期间安装进度接收器；扩展通过 host->progress 上报。 */
typedef void (*PsHostProgressFn)(void *ctx, uint64_t done, uint64_t total);
extern "C" void ps_host_progress_begin(void *ctx, PsHostProgressFn fn);
extern "C" void ps_host_progress_end(void);

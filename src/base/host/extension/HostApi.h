#pragma once

#include "ps_extension_api.h"

// 宿主函数表：扩展只拿到这张表，不直接链接 kernel。
extern "C" const PsHostApi *ps_host_api(void);

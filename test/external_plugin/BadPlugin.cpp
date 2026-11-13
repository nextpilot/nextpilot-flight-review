// 故意损坏的插件：入口存在但 ABI 版本非法。宿主必须丢弃它而不是崩溃。
#include "plugin_sdk/ps_plugin_api.h"

#include <cstdint>

namespace {

int32_t badLoad(const char *, const char *, void **, char *, uint32_t)
{
    return -1;
}

const PsLoaderVtable kBadLoader = {
    999,
    "bad",
    "Bad",
    "Bad (*.bad);;全部 (*)",
    "Bad",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    badLoad,
};

const PsPluginDescriptor kBadPlugin = {
    999,
    "{}",
    &kBadLoader,
    nullptr,
};

} // namespace

extern "C" __attribute__((visibility("default"))) const PsPluginDescriptor *ps_plugin_entry(void)
{
    return &kBadPlugin;
}

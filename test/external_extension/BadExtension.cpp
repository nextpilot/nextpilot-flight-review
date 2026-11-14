// 故意损坏的插件：入口存在但 ABI 版本非法。宿主必须丢弃它而不是崩溃。
#include "ps_extension_api.h"

#include <cstdint>

namespace {

int32_t badLoad(const char *, const char *, const PsHostApi *, void **, char *, uint32_t)
{
    return -1;
}

const PsLoaderVtable kBadLoader = {
    999,
    "data_loader_bad",
    "Bad",
    "Bad (*.bad);;全部 (*)",
    ".bad",
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

const PsExtensionDescriptor kBadExtension = {
    999,
    "{}",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE,
    &kBadLoader,
    nullptr,
};

} // namespace

extern "C" PS_EXTENSION_EXPORT const PsExtensionDescriptor *ps_extension_entry(void)
{
    return &kBadExtension;
}

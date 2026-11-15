// 外部扩展样例：只导出 ps_extension_entry，不链接内核，也不碰 Qt。
#include "ps_extension_api.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int32_t demoLoad(const char *, const char *, const PsHostApi *, void **, char *err, uint32_t cap)
{
    std::snprintf(err, cap, "demo 扩展只验证装载，不提供数据");
    return -1;
}

int32_t demoGetData(const char *, char *out, uint32_t cap)
{
    const char *json = R"({"note":"demo"})";
    const uint32_t n = static_cast<uint32_t>(std::strlen(json));
    if (n >= cap)
        return -1;
    std::memcpy(out, json, n + 1);
    return static_cast<int32_t>(n);
}

int32_t demoOnEvent(const char *, const char *, char *out, uint32_t cap)
{
    const char *json = R"({"ok":true,"message":"demo ribbon"})";
    const uint32_t n = static_cast<uint32_t>(std::strlen(json));
    if (n >= cap)
        return -1;
    std::memcpy(out, json, n + 1);
    return static_cast<int32_t>(n);
}

const char *kDemoRibbon = R"({"page":"数据","panel":"Demo 扩展","items":[{"action":"demo.hello","label":"Demo 命令","large":true}]})";

const PsLoaderVtable kDemoLoader = {
    PS_EXTENSION_ABI_VERSION,
    "data_loader_demo",
    "Demo",
    "Demo (*.demo);;全部 (*)",
    ".demo",
    "Demo",
    0,
    0,
    {0, 0, 0, 0, 0, 0},
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    demoLoad,
};

const PsUiVtable kDemoUi = {
    PS_EXTENSION_ABI_VERSION,
    "dialog.ui",
    nullptr,
    kDemoRibbon,
    demoGetData,
    demoOnEvent,
    nullptr,
};

const PsExtensionDescriptor kDemoExtension = {
    PS_EXTENSION_ABI_VERSION,
    R"({"id":"data_loader_demo","name":"Demo","version":"1.0.0","sdk":"1.0","family":"data_loader","kind":"file","entry":"ps_extension_entry"})",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE | PS_CAP_UI,
    &kDemoLoader,
    &kDemoUi,
};

} // namespace

extern "C" PS_EXTENSION_EXPORT const PsExtensionDescriptor *ps_extension_entry(void)
{
    return &kDemoExtension;
}

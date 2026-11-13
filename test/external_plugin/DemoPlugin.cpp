// 外部插件样例：只导出 ps_plugin_entry，不链接内核，也不碰 Qt。
#include "ps_plugin_api.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int32_t demoLoad(const char *, const char *, void **, char *err, uint32_t cap)
{
    std::snprintf(err, cap, "demo 插件只验证装载，不提供数据");
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

const PsLoaderVtable kDemoLoader = {
    PS_PLUGIN_ABI_VERSION,
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
    PS_PLUGIN_ABI_VERSION,
    "dialog.ui",
    nullptr,
    nullptr,
    demoGetData,
    nullptr,
    nullptr,
};

const PsPluginDescriptor kDemoPlugin = {
    PS_PLUGIN_ABI_VERSION,
    R"({"id":"data_loader_demo","name":"Demo","version":"1.0.0","sdk":"1.0","family":"data_loader","kind":"file","entry":"ps_plugin_entry"})",
    PS_FAMILY_DATA_LOADER,
    PS_CAP_FILE | PS_CAP_UI,
    &kDemoLoader,
    &kDemoUi,
};

} // namespace

extern "C" __attribute__((visibility("default"))) const PsPluginDescriptor *ps_plugin_entry(void)
{
    return &kDemoPlugin;
}

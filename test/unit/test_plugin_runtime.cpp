#include "runtime/PluginRuntime.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    PluginRuntime &rt = PluginRuntime::instance();
    rt.clear();
    rt.scanDirectory(PS_PLUGIN_TEST_DIR);

    int good = 0;
    for (const PsLoadedPlugin &p : rt.plugins())
    {
        if (std::strcmp(p.descriptor->loader->id, "demo") == 0)
        {
            ++good;
            assert(!p.manifest.empty());
            assert(p.handle != nullptr);
        }
    }
    if (good != 1)
    {
        std::cerr << "demo plugins=" << good << "\n";
        return 1;
    }
    if (rt.errors().empty())
    {
        std::cerr << "bad plugin was not rejected\n";
        return 1;
    }
    rt.clear();
    return 0;
}

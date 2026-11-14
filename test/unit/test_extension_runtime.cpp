#include "host/extension/ExtensionRuntime.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    ExtensionRuntime &rt = ExtensionRuntime::instance();
    rt.clear();
    rt.scanDirectory(PS_EXTENSION_TEST_DIR);

    int good = 0;
    for (const LoadedExtension &p : rt.loadedExtensions())
    {
        if (std::strcmp(p.descriptor->loader->id, "data_loader_demo") == 0)
        {
            ++good;
            assert(!p.manifest.empty());
            assert(p.handle != nullptr);
            assert(p.descriptor->family == PS_FAMILY_DATA_LOADER);
            assert(p.descriptor->capabilities & PS_CAP_UI);
        }
    }
    if (good != 1)
    {
        std::cerr << "demo extensions=" << good << "\n";
        return 1;
    }
    if (rt.errors().empty())
    {
        std::cerr << "bad extension was not rejected\n";
        return 1;
    }
    rt.clear();
    return 0;
}

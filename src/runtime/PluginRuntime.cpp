#include "runtime/PluginRuntime.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

using EntryFn = const PsPluginDescriptor *(*)();

bool descriptorOk(const PsPluginDescriptor *d)
{
    if (!d || d->abi_version != PS_PLUGIN_ABI_VERSION || !d->manifest_json ||
        d->family < PS_FAMILY_DATA_LOADER || d->family > PS_FAMILY_PANEL || d->capabilities == 0 ||
        !d->loader || d->loader->abi_version != PS_PLUGIN_ABI_VERSION || !d->loader->id ||
        !d->loader->id[0] || !d->loader->load)
        return false;
    if (d->ui && d->ui->abi_version != PS_PLUGIN_ABI_VERSION)
        return false;
    return true;
}

// 子进程预检：插件静态初始化或入口崩溃时，宿主只丢一个插件，不跟着死。
// 返回 0 通过；1 dlopen 失败；2 缺入口；3 描述符非法；4 子进程异常退出。
int preflight(const char *path)
{
#if defined(__linux__)
    const pid_t pid = fork();
    if (pid < 0)
        return 4;
    if (pid == 0)
    {
        void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h)
            _exit(1);
        auto fn = reinterpret_cast<EntryFn>(dlsym(h, "ps_plugin_entry"));
        if (!fn)
            _exit(2);
        const PsPluginDescriptor *d = fn();
        _exit(descriptorOk(d) ? 0 : 3);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid)
        return 4;
    if (!WIFEXITED(status))
        return 4;
    return WEXITSTATUS(status);
#else
    (void)path;
    return 0;
#endif
}

std::string readText(const fs::path &p)
{
    std::ifstream f(p);
    if (!f)
        return {};
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

PluginRuntime &PluginRuntime::instance()
{
    static PluginRuntime rt;
    return rt;
}

PluginRuntime::~PluginRuntime()
{
    clear();
}

bool PluginRuntime::isValid(const PsPluginDescriptor *d) const
{
    return descriptorOk(d);
}

void PluginRuntime::scanDirectory(const std::string &dir)
{
    const fs::path root(dir);
    if (!fs::is_directory(root))
        return;

    std::vector<fs::path> libs;
    for (const auto &e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied))
    {
        if (!e.is_regular_file())
            continue;
        if (e.path().extension() == ".so")
            libs.push_back(e.path());
    }
    std::sort(libs.begin(), libs.end());

    std::set<std::string> known;
    std::set<std::string> ids;
    for (const PsLoadedPlugin &p : m_plugins)
    {
        known.insert(p.library);
        if (p.descriptor && p.descriptor->loader && p.descriptor->loader->id)
            ids.insert(p.descriptor->loader->id);
    }

    for (const fs::path &lib : libs)
    {
        const std::string path = fs::absolute(lib).string();
        if (known.count(path))
            continue;
        known.insert(path);

        const int pre = preflight(path.c_str());
        if (pre != 0)
        {
            const char *why = pre == 1 ? "dlopen 失败"
                             : pre == 2 ? "缺少 ps_plugin_entry"
                             : pre == 3 ? "ABI 或 loader vtable 非法"
                                        : "预检子进程异常退出";
            m_errors.push_back(path + "：" + why);
            continue;
        }

        void *h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h)
        {
            m_errors.push_back(path + "：dlopen 失败 " + (dlerror() ? dlerror() : ""));
            continue;
        }

        auto fn = reinterpret_cast<EntryFn>(dlsym(h, "ps_plugin_entry"));
        if (!fn)
        {
            m_errors.push_back(path + "：缺少 ps_plugin_entry");
            dlclose(h);
            continue;
        }

        const PsPluginDescriptor *d = fn();
        if (!isValid(d))
        {
            m_errors.push_back(path + "：ABI 版本或 loader vtable 非法");
            dlclose(h);
            continue;
        }

        PsLoadedPlugin rec;
        rec.library = path;
        rec.packageDir = fs::absolute(lib.parent_path()).string();
        rec.manifest = readText(lib.parent_path() / "manifest.json");
        rec.descriptor = d;
        rec.handle = h;
        const std::string id(d->loader->id);
        if (ids.count(id))
        {
            m_errors.push_back(path + "：重复插件 id " + id + "，已跳过");
            dlclose(h);
            continue;
        }
        ids.insert(id);
        m_plugins.push_back(rec);
    }
}

void PluginRuntime::clear()
{
    for (PsLoadedPlugin &p : m_plugins)
        if (p.handle)
            dlclose(p.handle);
    m_plugins.clear();
    m_errors.clear();
}

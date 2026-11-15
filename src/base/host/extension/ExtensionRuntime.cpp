#include "host/extension/ExtensionRuntime.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using EntryFn = const PsExtensionDescriptor *(*)();

#if defined(_WIN32)

void *openLibrary(const char *path)
{
    return reinterpret_cast<void *>(LoadLibraryA(path));
}

void closeLibrary(void *handle)
{
    if (handle)
        FreeLibrary(reinterpret_cast<HMODULE>(handle));
}

void *librarySymbol(void *handle, const char *name)
{
    return reinterpret_cast<void *>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
}

std::string libraryError()
{
    char buf[256] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   GetLastError(), 0, buf, sizeof(buf), nullptr);
    return buf;
}

const char *libraryExtension()
{
    return ".dll";
}

#else

void *openLibrary(const char *path)
{
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}

void closeLibrary(void *handle)
{
    if (handle)
        dlclose(handle);
}

void *librarySymbol(void *handle, const char *name)
{
    return dlsym(handle, name);
}

std::string libraryError()
{
    const char *e = dlerror();
    return e ? e : std::string();
}

const char *libraryExtension()
{
    return ".so";
}

#endif

const char *descriptorId(const PsExtensionDescriptor *d)
{
    if (d->loader && d->loader->id && d->loader->id[0])
        return d->loader->id;
    if (d->source && d->source->id && d->source->id[0])
        return d->source->id;
    return nullptr;
}

bool descriptorOk(const PsExtensionDescriptor *d)
{
    if (!d || d->abi_version != PS_EXTENSION_ABI_VERSION || !d->manifest_json ||
        d->family < PS_FAMILY_DATA_LOADER || d->family > PS_FAMILY_DATA_INPUT)
        return false;
    if (d->ui && d->ui->abi_version != PS_EXTENSION_ABI_VERSION)
        return false;
    if (d->family == PS_FAMILY_DATA_LOADER)
    {
        return d->loader && d->loader->abi_version == PS_EXTENSION_ABI_VERSION && d->loader->id &&
               d->loader->id[0] && d->loader->load;
    }
    if (d->family == PS_FAMILY_DATA_SOURCE)
    {
        return d->source && d->source->abi_version == PS_EXTENSION_ABI_VERSION && d->source->id &&
               d->source->id[0] && d->source->open && d->source->poll && d->source->close;
    }
    if (d->family == PS_FAMILY_DATA_INPUT)
    {
        const PsDataInputVtable *v = d->data_input;
        return v && v->protocol_version == PS_DATA_INPUT_PROTOCOL_VERSION &&
               v->struct_size >= sizeof(PsDataInputVtable) && v->create && v->destroy &&
               v->capabilities && v->start && v->stop && v->poll && v->current_state;
    }
    return d->capabilities != 0;
}

// 子进程预检：扩展静态初始化或入口崩溃时，宿主只丢一个扩展，不跟着死。
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
        auto fn = reinterpret_cast<EntryFn>(dlsym(h, "ps_extension_entry"));
        if (!fn)
            _exit(2);
        const PsExtensionDescriptor *d = fn();
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

ExtensionRuntime &ExtensionRuntime::instance()
{
    static ExtensionRuntime rt;
    return rt;
}

ExtensionRuntime::~ExtensionRuntime()
{
    clear();
}

bool ExtensionRuntime::isValid(const PsExtensionDescriptor *d) const
{
    return descriptorOk(d);
}

void ExtensionRuntime::scanDirectory(const std::string &dir)
{
    const fs::path root(dir);
    if (!fs::is_directory(root))
        return;

    std::vector<fs::path> libs;
    for (const auto &e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied))
    {
        if (!e.is_regular_file())
            continue;
        if (e.path().extension() == libraryExtension())
            libs.push_back(e.path());
    }
    std::sort(libs.begin(), libs.end());

    std::set<std::string> known;
    std::set<std::string> ids;
    for (const LoadedExtension &p : m_extensions)
    {
        known.insert(p.library);
        if (p.descriptor)
            if (const char *id = descriptorId(p.descriptor))
                ids.insert(id);
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
                             : pre == 2 ? "缺少 ps_extension_entry"
                             : pre == 3 ? "ABI 或 loader vtable 非法"
                                        : "预检子进程异常退出";
            m_errors.push_back(path + "：" + why);
            continue;
        }

        void *h = openLibrary(path.c_str());
        if (!h)
        {
            m_errors.push_back(path + "：加载失败 " + libraryError());
            continue;
        }

        auto fn = reinterpret_cast<EntryFn>(librarySymbol(h, "ps_extension_entry"));
        if (!fn)
        {
            m_errors.push_back(path + "：缺少 ps_extension_entry");
            closeLibrary(h);
            continue;
        }

        const PsExtensionDescriptor *d = fn();
        if (!isValid(d))
        {
            m_errors.push_back(path + "：ABI 版本或 loader vtable 非法");
            closeLibrary(h);
            continue;
        }

        LoadedExtension rec;
        rec.library = path;
        rec.packageDir = fs::absolute(lib.parent_path()).string();
        rec.manifest = readText(lib.parent_path() / "manifest.json");
        rec.descriptor = d;
        rec.handle = h;
        const std::string id(descriptorId(d));
        if (ids.count(id))
        {
            m_errors.push_back(path + "：重复扩展 id " + id + "，已跳过");
            closeLibrary(h);
            continue;
        }
        ids.insert(id);
        m_extensions.push_back(rec);
    }
}

void ExtensionRuntime::clear()
{
    for (LoadedExtension &p : m_extensions)
        if (p.handle)
            closeLibrary(p.handle);
    m_extensions.clear();
    m_errors.clear();
}

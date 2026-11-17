#pragma once

#include "ps_extension_api.h"

#include <string>
#include <vector>

// 外部扩展装载层：发现、预检、动态加载、ABI 校验。不依赖 Qt，也不认任何具体格式。
struct LoadedExtension
{
    std::string library;      // .so / .dll 绝对路径
    std::string packageDir;   // 扩展包目录，用于解析 .ui / 资源
    std::string manifest;     // 同目录 manifest.json 原文，可为空
    const PsExtensionDescriptor *descriptor = nullptr;
    void *handle = nullptr;   // 动态库句柄，运行期不解引用
};

class ExtensionRuntime
{
public:
    static ExtensionRuntime &instance();

    // 递归扫描目录下的 *.so；同一扩展只按路径装载一次。
    void scanDirectory(const std::string &dir);
    const std::vector<LoadedExtension> &loadedExtensions() const { return m_extensions; }
    const std::vector<std::string> &errors() const { return m_errors; }
    void clear();

private:
    ExtensionRuntime() = default;
    ~ExtensionRuntime();
    bool isValid(const PsExtensionDescriptor *d) const;

    std::vector<LoadedExtension> m_extensions;
    std::vector<std::string> m_errors;
};

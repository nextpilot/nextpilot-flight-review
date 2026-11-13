#pragma once

#include "plugin_sdk/ps_plugin_api.h"

#include <string>
#include <vector>

// 外部插件装载层：发现、预检、dlopen、ABI 校验。不依赖 Qt，也不认任何具体格式。
struct PsLoadedPlugin
{
    std::string library;      // .so 绝对路径
    std::string packageDir;   // 插件包目录，用于解析 .ui / 资源
    std::string manifest;     // 同目录 manifest.json 原文，可为空
    const PsPluginDescriptor *descriptor = nullptr;
    void *handle = nullptr;   // dlopen 句柄，运行期不解引用
};

class PluginRuntime
{
public:
    static PluginRuntime &instance();

    // 递归扫描目录下的 *.so；同一插件只按路径装载一次。
    void scanDirectory(const std::string &dir);
    const std::vector<PsLoadedPlugin> &plugins() const { return m_plugins; }
    const std::vector<std::string> &errors() const { return m_errors; }
    void clear();

private:
    PluginRuntime() = default;
    ~PluginRuntime();
    bool isValid(const PsPluginDescriptor *d) const;

    std::vector<PsLoadedPlugin> m_plugins;
    std::vector<std::string> m_errors;
};

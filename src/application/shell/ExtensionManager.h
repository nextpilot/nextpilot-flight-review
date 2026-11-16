#pragma once

#include "Component.h"

#include <QStringList>
#include <QVector>

#include <memory>

namespace ps::components
{
class ExtensionHost;
}

// 扩展管理入口：目录、激活状态、错误汇总。具体装载由 runtime 负责。
class ExtensionManager
{
public:
    struct ExtensionInfo
    {
        ps::components::ExtensionDescriptor descriptor;
        bool active = false;
        bool enabled = true;
        QString error;
    };

    ExtensionManager();
    ~ExtensionManager();

    static void scanStandardLocations();

    void activate(const ps::components::HostContext &context);
    void deactivate();

    ps::components::Registry &registry();
    const ps::components::Registry &registry() const;

    QStringList errors() const;
    QVector<ExtensionInfo> extensions() const;
    bool isActive(const QString &id) const;
    bool isEnabled(const QString &id) const;
    void setEnabled(const QString &id, bool enabled);
    QStringList disabledIds() const;

private:
    std::unique_ptr<ps::components::ExtensionHost> m_host;
};

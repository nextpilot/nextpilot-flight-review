#pragma once

#include "loader/LoaderPlugin.h"

#include <QVector>

struct PsPluginDescriptor;

class LoaderRegistry
{
public:
    static LoaderRegistry &instance();

    const LoaderPlugin *findBySuffix(const QString &suffix) const;
    const LoaderPlugin *findById(const QString &id) const;
    const QVector<LoaderPlugin> &plugins() const { return m_plugins; }

    // 内置插件入口：app 只接收描述符，不认具体格式。
    void registerDescriptor(const PsPluginDescriptor *desc);

private:
    LoaderRegistry();
    QVector<LoaderPlugin> m_plugins;
};

#pragma once

#include "loader/LoaderPlugin.h"

#include <QStringList>
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
    bool registerDescriptor(const PsPluginDescriptor *desc, const QString &packageDir = QString());

    // 外部插件目录：扫描、dlopen、ABI 校验后注册；返回错误列表。
    QStringList scanPlugins(const QStringList &dirs);

private:
    LoaderRegistry();
    QVector<LoaderPlugin> m_plugins;
};

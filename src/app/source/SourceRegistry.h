#pragma once

#include "source/SourcePlugin.h"

#include <QStringList>
#include <QVector>

struct PsPluginDescriptor;

// data_source catalog：内置源 + 外部 .so，按 id 去重。
class SourceRegistry
{
public:
    static SourceRegistry &instance();

    const SourcePlugin *findById(const QString &id) const;
    const QVector<SourcePlugin> &plugins() const { return m_plugins; }

    bool registerDescriptor(const PsPluginDescriptor *desc, const QString &packageDir = QString());
    QStringList scanPlugins(const QStringList &dirs);

private:
    SourceRegistry();
    QVector<SourcePlugin> m_plugins;
};

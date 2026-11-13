#pragma once

#include "loader/LoaderPlugin.h"

#include <QVector>

class LoaderRegistry
{
public:
    static LoaderRegistry &instance();

    const LoaderPlugin *findBySuffix(const QString &suffix) const;
    const LoaderPlugin *findById(const QString &id) const;
    const QVector<LoaderPlugin> &plugins() const { return m_plugins; }

private:
    LoaderRegistry();
    QVector<LoaderPlugin> m_plugins;
};

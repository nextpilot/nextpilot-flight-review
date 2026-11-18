#include "ExtensionRegistry.h"

#include <algorithm>

namespace ng::core
{

bool ExtensionRegistry::registerDriver(std::unique_ptr<IFormatDriver> driver)
{
    if (!driver)
        return false;
    const QString id = driver->id();
    if (id.isEmpty() || m_index.contains(id))
        return false;

    IFormatDriver *raw = driver.get();
    m_owned.push_back(std::move(driver));
    m_index.insert(id, raw);
    m_order.append(id);
    return true;
}

bool ExtensionRegistry::unregisterDriver(const QString &id)
{
    auto idx = m_index.find(id);
    if (idx == m_index.end())
        return false;
    IFormatDriver *raw = idx.value();

    auto owned = std::find_if(m_owned.begin(), m_owned.end(),
                              [raw](const std::unique_ptr<IFormatDriver> &p) {
                                  return p.get() == raw;
                              });
    if (owned != m_owned.end())
        m_owned.erase(owned);
    m_index.erase(idx);
    m_order.removeAll(id);
    return true;
}

IFormatDriver *ExtensionRegistry::driverForFormat(const QString &format) const
{
    if (format.isEmpty())
        return nullptr;
    for (const auto &driver : m_owned)
        if (driver->acceptsFormat(format))
            return driver.get();
    return nullptr;
}

IFormatDriver *ExtensionRegistry::driverForPath(const QString &path) const
{
    for (const auto &driver : m_owned)
        if (driver->acceptsSuffix(path))
            return driver.get();
    return nullptr;
}

QList<IFormatDriver *> ExtensionRegistry::drivers() const
{
    QList<IFormatDriver *> out;
    out.reserve(m_order.size());
    for (const QString &id : m_order)
        if (auto it = m_index.constFind(id); it != m_index.constEnd())
            out.append(it.value());
    return out;
}

QList<IFormatDriver *> ExtensionRegistry::driversByDomain(FormatDomain domain) const
{
    QList<IFormatDriver *> out;
    for (const auto &driver : m_owned)
        if (driver->domain() == domain)
            out.append(driver.get());
    return out;
}

QList<FormatDomain> ExtensionRegistry::domains() const
{
    QList<FormatDomain> out;
    for (const auto &driver : m_owned)
        if (!out.contains(driver->domain()))
            out.append(driver->domain());
    return out;
}

QStringList ExtensionRegistry::ids() const
{
    return m_order;
}

bool ExtensionRegistry::contains(const QString &id) const
{
    return m_index.contains(id);
}

int ExtensionRegistry::count() const
{
    return static_cast<int>(m_owned.size());
}

} // namespace ng::core

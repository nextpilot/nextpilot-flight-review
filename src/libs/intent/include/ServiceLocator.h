#pragma once

// 服务定位器（D11）：模块间通过稳定 id 取服务，不互相直接依赖。
//
// 服务 id 命名空间统一为 `domain.service.<域>.<名>`（契约 D11：`domain.service`）。
// 定位器只存 void* + 类型名，不做所有权管理（服务生命周期归注册方）；
// 查询时校验 typeName，避免拿错类型。headless 与 GUI 共用同一实例。

#include <QHash>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::intent
{

class ServiceLocator
{
public:
    struct Entry
    {
        void *instance = nullptr;
        QString typeName;
    };

    bool registerService(const QString &id, void *instance, const QString &typeName)
    {
        if (id.isEmpty() || !instance || m_entries.contains(id))
            return false;
        m_entries.insert(id, Entry{instance, typeName});
        return true;
    }

    template <typename T>
    T *service(const QString &id, const QString &typeName) const
    {
        const auto it = m_entries.constFind(id);
        if (it == m_entries.constEnd() || it.value().typeName != typeName)
            return nullptr;
        return static_cast<T *>(it.value().instance);
    }

    bool contains(const QString &id) const { return m_entries.contains(id); }
    QStringList ids() const { return m_entries.keys(); }
    int count() const { return m_entries.size(); }

    // 服务 id 必须是 domain.service. 前缀（契约 D11）。
    static bool isValidId(const QString &id)
    {
        return id.startsWith(QStringLiteral("domain.service."));
    }

private:
    QHash<QString, Entry> m_entries;
};

} // namespace nextpilot::plotscope::intent

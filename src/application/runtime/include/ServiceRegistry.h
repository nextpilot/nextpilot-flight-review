#pragma once

#include <cstddef>
#include <typeindex>
#include <unordered_map>

namespace ps::runtime
{

// 跨模块服务查找表。只登记和查找，不执行业务逻辑。
// 模块缺省时 find() 返回 nullptr，调用方自行降级。
class ServiceRegistry
{
public:
    template <typename T>
    void provide(T *service)
    {
        if (service)
            m_services[std::type_index(typeid(T))] = service;
        else
            m_services.erase(std::type_index(typeid(T)));
    }

    template <typename T>
    T *find() const
    {
        const auto it = m_services.find(std::type_index(typeid(T)));
        return it == m_services.end() ? nullptr : static_cast<T *>(it->second);
    }

    void clear() { m_services.clear(); }

private:
    std::unordered_map<std::type_index, void *> m_services;
};

} // namespace ps::runtime

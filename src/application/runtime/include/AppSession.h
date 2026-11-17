#pragma once

#include "ServiceRegistry.h"

namespace ps::runtime
{

// 进程级工作会话。当前 MainWindow 持有，生命周期跟随窗口；
// 真需要“关窗不退出”时再把本对象上移到 AppContext。
class AppSession
{
public:
    ServiceRegistry &services() { return m_services; }
    const ServiceRegistry &services() const { return m_services; }

private:
    ServiceRegistry m_services;
};

} // namespace ps::runtime

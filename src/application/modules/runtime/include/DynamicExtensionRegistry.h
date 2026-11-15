#pragma once

#include <QStringList>

namespace ps::components
{

// 把 ExtensionRuntime 已装载的动态扩展统一注册为组件；不依赖具体 family。
QStringList registerDynamicExtensions();

} // namespace ps::components

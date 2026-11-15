#pragma once

#include "Component.h"

namespace ps::components
{

// 绘图模块：注册“绘图”Ribbon页；画布与页面实现留在本模块。
void registerPlotContributions(Registry &registry);

} // namespace ps::components

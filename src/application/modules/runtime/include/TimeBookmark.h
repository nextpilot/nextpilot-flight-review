#pragma once

#include <QString>

namespace ps
{

// 时间书签：id 稳定，t 可随拖动变化。
struct TimeBookmark
{
    QString id;
    double t = 0.0;
    QString label;
};

} // namespace ps

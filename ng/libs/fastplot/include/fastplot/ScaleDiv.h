#pragma once

#include <QString>
#include <QVector>

namespace fastplot
{

struct Tick
{
    double value = 0.0;
    QString label;
    bool major = true;
};

// 刻度划分：把 [lo, hi] 切成人眼友好的主/次刻度（Heckbert nice numbers）。
//
// 只做线性刻度；对数轴在 P4 再议——现在不预留抽象，避免"为未来设计"。
class ScaleDiv
{
public:
    static ScaleDiv linear(double lo, double hi, int maxMajorTicks = 8);

    QVector<Tick> ticks;   // 主、次都在里面，按 value 升序
    double lower = 0.0;    // 第一个主刻度
    double upper = 1.0;    // 最后一个主刻度
    double majorStep = 1.0;
    int minorPerMajor = 5;

    // 离给定值最近的主刻度索引；没有主刻度时返回 -1。
    int nearestMajorIndex(double value) const;

    // 数值 → 文本；step 决定小数位数，避免 "0.30000000000000004"。
    static QString formatValue(double value, double step);
};

} // namespace fastplot

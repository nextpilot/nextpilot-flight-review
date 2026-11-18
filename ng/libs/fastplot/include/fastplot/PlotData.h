#pragma once

#include <QtGlobal>

namespace fastplot
{

// 一条曲线的数据访问抽象。
//
// 两条硬规则：
//   1) **不拥有数据**——生命周期由调用方（ng 的数据层/脚本）负责，库不复制；
//   2) **快路径优先**——连续内存的通道走 directAccess() 拿裸指针，
//      绝不能对 10⁶ 个点逐点调用虚函数（那样光是调用开销就吃掉一帧）。
class IPlotData
{
public:
    virtual ~IPlotData() = default;

    virtual qsizetype size() const = 0;
    virtual double xAt(qsizetype i) const = 0;
    virtual double yAt(qsizetype i) const = 0;

    // 返回 true 时 x/y 指向连续内存，长度均为 size()；否则调用方退回 xAt/yAt。
    virtual bool directAccess(const double **x, const double **y) const = 0;

    // 数据版本号：每次内容变化必须变（缓存失效的唯一依据）。
    // 用指针身份做缓存键会在"换新数组"时误命中，必须由生产方给版本。
    virtual quint64 revision() const = 0;
};

// 非拥有视图：把一段连续 double 数组当成曲线数据（零拷贝）。
// ng 的 Dataset 通道、脚本算出来的数组，都能直接用这个包一层。
class ArrayData final : public IPlotData
{
public:
    ArrayData(const double *x, const double *y, qsizetype n, quint64 revision = 0)
        : m_x(x), m_y(y), m_n(x && y ? n : 0), m_revision(revision)
    {
    }

    qsizetype size() const override { return m_n; }
    double xAt(qsizetype i) const override { return m_x[i]; }
    double yAt(qsizetype i) const override { return m_y[i]; }

    bool directAccess(const double **x, const double **y) const override
    {
        *x = m_x;
        *y = m_y;
        return m_x && m_y;
    }

    quint64 revision() const override { return m_revision; }

private:
    const double *m_x = nullptr;
    const double *m_y = nullptr;
    qsizetype m_n = 0;
    quint64 m_revision = 0;
};

// 内核已按像素分桶后的三元组（ng：ps_dataset_lod_minmax）。
// 数量恒为 O(像素宽)，与数据行数无关；库只画、不重算。
struct LodPoint
{
    double x = 0.0;
    double min = 0.0;
    double max = 0.0;
};

} // namespace fastplot

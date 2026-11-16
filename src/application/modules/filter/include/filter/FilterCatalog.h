#pragma once

#include <cstddef>
#include <cstring>

// 滤波算子唯一目录：datasource 派生通道与 plotting 曲线 filter 共用。
// 内核 ps-ops 的 filter id 必须与这里一一对应。
namespace ps::filter
{

enum class ParamType
{
    Double,
    Int,
    Bool,
    Choice,
};

struct ParamDef
{
    const char *key;
    const char *label;
    ParamType type;
    double min;
    double max;
    double def;
    int decimals;
    const char *const *choices;
    int choice_count;
};

struct Def
{
    const char *id;
    const char *label;
    const char *short_label;
    const ParamDef *params;
    int param_count;
};

inline const ParamDef kScaleParams[] = {
    {"value_scale", "比例", ParamType::Double, -1e12, 1e12, 1.0, 6, nullptr, 0},
    {"value_offset", "偏移", ParamType::Double, -1e12, 1e12, 0.0, 6, nullptr, 0},
    {"time_offset_sec", "时间平移 (s)", ParamType::Double, -1e9, 1e9, 0.0, 6, nullptr, 0},
};

inline const ParamDef kDerivativeParams[] = {
    {"use_custom_dt", "固定 Δt", ParamType::Bool, 0, 0, 0.0, 0, nullptr, 0},
    {"custom_dt", "Δt (s)", ParamType::Double, 1e-9, 1e9, 1.0, 6, nullptr, 0},
};

inline const ParamDef kMovingAverageParams[] = {
    {"window", "窗口", ParamType::Int, 1, 1000000, 100, 0, nullptr, 0},
    {"compensate_time_offset", "时间戳居中", ParamType::Bool, 0, 0, 0.0, 0, nullptr, 0},
};

inline const ParamDef kMovingRmsParams[] = {
    {"window", "窗口", ParamType::Int, 1, 1000000, 100, 0, nullptr, 0},
};

inline const ParamDef kMovingVarianceParams[] = {
    {"window", "窗口", ParamType::Int, 1, 1000000, 100, 0, nullptr, 0},
    {"std_dev", "输出标准差", ParamType::Bool, 0, 0, 0.0, 0, nullptr, 0},
};

inline const ParamDef kOutlierParams[] = {
    {"outlier_factor", "倍数阈值", ParamType::Double, 0, 1e12, 100.0, 3, nullptr, 0},
};

inline const ParamDef kSamplesParams[] = {
    {"samples_ms", "窗口 (ms)", ParamType::Double, 0, 1e9, 1000.0, 3, nullptr, 0},
};

inline const char *const kBinaryChoices[] = {"== a", "< a", "<= a", "> a", ">= a", "in [a,b]"};

inline const ParamDef kBinaryParams[] = {
    {"binary_op", "比较", ParamType::Choice, 0, 0, 3.0, 0, kBinaryChoices, 6},
    {"binary_a", "阈值 a", ParamType::Double, -1e12, 1e12, 0.0, 6, nullptr, 0},
    {"binary_b", "阈值 b", ParamType::Double, -1e12, 1e12, 0.0, 6, nullptr, 0},
    {"binary_tol", "== 容差", ParamType::Double, 0, 1e9, 1e-9, 9, nullptr, 0},
};

inline const Def kDefs[] = {
    {"none", "直通（无变化）", "直通", nullptr, 0},
    {"absolute", "绝对值 |y|", "绝对值", nullptr, 0},
    {"scale", "比例 + 偏移 + 时间平移", "比例", kScaleParams, 3},
    {"derivative", "微分 dv/dt", "微分", kDerivativeParams, 2},
    {"integral", "梯形积分", "积分", kDerivativeParams, 2},
    {"moving_average", "滑动平均", "滑动平均", kMovingAverageParams, 2},
    {"moving_rms", "滑动 RMS", "滑动 RMS", kMovingRmsParams, 1},
    {"moving_variance", "滑动方差 / 标准差", "滑动方差", kMovingVarianceParams, 2},
    {"outlier_removal", "剔除单点尖峰", "剔除尖峰", kOutlierParams, 1},
    {"samples_counter", "窗口内样本数", "样本数", kSamplesParams, 1},
    {"binary_filter", "阈值二值化", "二值化", kBinaryParams, 4},
    {"time_since_previous", "相邻样本 Δt", "相邻 Δt", nullptr, 0},
    {"normalize", "归一化 0–1", "归一化", nullptr, 0},
};

inline const Def *catalog(std::size_t *count = nullptr)
{
    if (count)
        *count = sizeof(kDefs) / sizeof(kDefs[0]);
    return kDefs;
}

inline const Def *find(const char *id)
{
    if (!id)
        return nullptr;
    for (const Def &def : kDefs)
        if (std::strcmp(def.id, id) == 0)
            return &def;
    return nullptr;
}

} // namespace ps::filter

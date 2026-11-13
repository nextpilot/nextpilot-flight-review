/* PlotScope 内核 C ABI（app 只允许 include 这一个头文件，见 D16） */
#ifndef PS_KERNEL_H
#define PS_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI 版本闸门：宿主用它挡掉不兼容的实现，见 D9 */
#define PS_ABI_VERSION 1u

/* 借用型字节片：宿主分配、内核不释放 */
typedef struct PsBytes {
    const uint8_t *ptr;
    size_t len;
} PsBytes;

uint32_t    ps_abi_version(void);   /* 恒等于 PS_ABI_VERSION */
const char *ps_kernel_version(void);/* 形如 "0.1.0"，稳定字符串，勿 free */
int32_t     ps_abi_selftest(void);  /* 0 = 通过；非 0 = 具体失败码 */

/* ---- 数据面：数据集与通道（M0） ---- */

typedef struct PsDataset PsDataset;  /* 不透明句柄，ps_dataset_free 释放 */

/* 通道视图。name/unit/time/value 都指向数据集内部内存：数据集释放后立即失效，
 * UI 不得跨数据集生命周期持有（M1 换成 NDF mmap 视图后指针语义不变）。 */
typedef struct PsChannelInfo {
    const char *name;
    const char *unit;
    uint32_t    point_count;
    const double *time;   /* 秒 */
    const double *value;
} PsChannelInfo;

/* ---- 数据集只由插件/缓存通过通用构造 API 生成 ---- */

uint32_t             ps_dataset_channel_count(const PsDataset *ds);
const PsChannelInfo *ps_dataset_channel(const PsDataset *ds, uint32_t index);
void                 ps_dataset_free(PsDataset *ds);

/* 缓存层用：新建空数据集并追加通道，避免二次加载重新解析源文件。
 * 数组由内核复制；返回 0 成功，非 0 见 err。 */
PsDataset           *ps_dataset_new(void);
int32_t              ps_dataset_add_channel(PsDataset *ds, const char *name, const char *unit,
                                            const double *time, const double *value, uint32_t count,
                                            char *err, uint32_t err_cap);

/* 绘图前变换：结果作为普通通道写回同一 PsDataset。
 * ps_dataset_apply_filter：input 为通道名，filter_id 为 PJ4 内置算子，
 * params_json 为对象；output 为空时自动 `<input>.<filter_id>`。
 * 返回 0 成功；非 0 见 err。 */
int32_t              ps_dataset_apply_filter(PsDataset *ds, const char *input, const char *filter_id,
                                             const char *params_json, const char *output,
                                             char *err, uint32_t err_cap);
/* 只读预览：结果写进 out_t/out_v，不改数据集。cap 不足返回 6，out_n 仍为实际长度。 */
int32_t              ps_preview_filter(const PsDataset *ds, const char *input, const char *filter_id,
                                       const char *params_json, double *out_t, double *out_v,
                                       uint32_t cap, uint32_t *out_n, char *err, uint32_t err_cap);
/* 四元数 w/x/y/z → `<out_topic>.roll/.pitch/.yaw` 三条通道，要求同点数同时间戳。 */
int32_t              ps_dataset_apply_quat_rpy(PsDataset *ds, const char *w, const char *x,
                                               const char *y, const char *z, const char *out_topic,
                                               uint8_t degrees, uint8_t unwrap,
                                               char *err, uint32_t err_cap);
/* 删除通道（包括派生通道）。 */
int32_t              ps_dataset_remove_channel(PsDataset *ds, const char *name);

/* 视口抽稀（铁律：降采样在 Rust 侧完成）。out 按 (x, min, max) 三元组排列，
 * 返回实际写入的三元组个数，上限 min(px, cap_triples)。 */
uint32_t ps_lod_minmax(const double *t, const double *v, uint32_t n,
                       double t0, double t1, uint32_t px,
                       double *out_xyz, uint32_t cap_triples);

/* 批量视口抽稀：一次调用处理 channels 条通道，通道数 >= 8 时内核内部并行。
 * time/value/count 是长度 channels 的数组；结果按通道写入
 * out + c*out_stride（每通道 (x,min,max)），实际三元组数写 out_counts[c]。
 * out_stride 必须 >= 3；px 超过 stride/3 时按 stride 截断。
 * 返回处理的通道数；空指针返回 0。 */
uint32_t ps_lod_minmax_many(const double *const *time,
                            const double *const *value,
                            const uint32_t *count,
                            uint32_t channels,
                            double t0, double t1, uint32_t px,
                            double *out, uint32_t out_stride,
                            uint32_t *out_counts);

#ifdef __cplusplus
}
#endif
#endif /* PS_KERNEL_H */

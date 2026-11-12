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

/* 读 CSV → 数据集。0 = 成功（写 out）；非 0 = 失败（err 里给原因）。 */
int32_t              ps_csv_load(const char *path, PsDataset **out, char *err, uint32_t err_cap);
uint32_t             ps_dataset_channel_count(const PsDataset *ds);
const PsChannelInfo *ps_dataset_channel(const PsDataset *ds, uint32_t index);
void                 ps_dataset_free(PsDataset *ds);

/* 视口抽稀（铁律：降采样在 Rust 侧完成）。out 按 (x, min, max) 三元组排列，
 * 返回实际写入的三元组个数，上限 min(px, cap_triples)。 */
uint32_t ps_lod_minmax(const double *t, const double *v, uint32_t n,
                       double t0, double t1, uint32_t px,
                       double *out_xyz, uint32_t cap_triples);

#ifdef __cplusplus
}
#endif
#endif /* PS_KERNEL_H */

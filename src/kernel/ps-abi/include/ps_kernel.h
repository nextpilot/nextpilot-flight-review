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

/* 读 CSV → 数据集。0 = 成功（写 out）；非 0 = 失败（err 里给原因）。
 * 这是"探测后按建议自动加载"的便捷入口；要用户选时间列就用下面的 opts 版本。 */
int32_t              ps_csv_load(const char *path, PsDataset **out, char *err, uint32_t err_cap);

/* 只探测、不加载：返回 JSON（分隔符、首行是否表头、列名/类型、前 9 行预览、
 * 建议时间列、警告）。写进 out_json，返回写入字节数；-1 = 失败。 */
/* delimiter_override：0 = 自动探测；否则传 ',' / ';' / '\t' / ' ' 之一 */
int32_t              ps_csv_probe(const char *path, char delimiter_override,
                                 char *out_json, uint32_t cap, char *err, uint32_t err_cap);

/* 按用户选项加载 CSV。opts_json 形如：
 * {"delimiter":",","has_header":true,"skip_rows":0,"time":{"mode":"column","index":1}}
 * time.mode: row | column | combined（combined 需 date / time 两个下标） */
int32_t              ps_csv_load_opts(const char *path, const char *opts_json,
                                     PsDataset **out, char *err, uint32_t err_cap);

/* 读 ULog（PX4 飞行日志）→ 数据集。通道名形如 `topic.field`，
 * 多实例为 `topic.00.field`；`timestamp` 与 `_padding` 不产出通道。
 * 单位信息 ULog 里没有（只能靠字段名/格式约定），unit 返回空串。 */
int32_t              ps_ulog_load(const char *path, PsDataset **out, char *err, uint32_t err_cap);
/* ULog 参数表：[{"name":...,"value":...}] JSON；cap 不足时截断，返回非 0 看 err */
int32_t              ps_ulog_params(const char *path, char *out_json, uint32_t cap, char *err, uint32_t err_cap);

/* 读 ArduPilot DataFlash `.bin` → 数据集。通道名形如 `ATT.Roll`，
 * 多实例 `IMU.#0.AccX`；FMTU 的单位写进 unit。 */
int32_t              ps_apbin_load(const char *path, PsDataset **out, char *err, uint32_t err_cap);
/* DataFlash 参数表（PARM）：[{"name":...,"value":...}] JSON */
int32_t              ps_apbin_params(const char *path, char *out_json, uint32_t cap, char *err, uint32_t err_cap);
/* 读 Vector ASC（文本 CAN 日志）→ 数据集。没有 DBC 就不猜物理量，
 * 只给可验证的两类通道：`CAN<总线>.<ID>.bN`（报文第 N 字节原始值）
 * 与 `CAN<总线>.<ID>.period`（同 ID 相邻帧间隔 ms）。 */
int32_t              ps_asc_load(const char *path, PsDataset **out, char *err, uint32_t err_cap);

/* 读 Vector BLF（二进制 CAN 日志）→ 数据集，通道规则同 ps_asc_load。 */
int32_t              ps_blf_load(const char *path, PsDataset **out, char *err, uint32_t err_cap);
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

#ifdef __cplusplus
}
#endif
#endif /* PS_KERNEL_H */

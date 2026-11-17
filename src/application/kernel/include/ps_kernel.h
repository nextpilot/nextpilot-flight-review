/* PlotScope 内核 C ABI（app 只允许 include 这一个头文件，见 D16） */
#ifndef PS_KERNEL_H
#define PS_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI 版本闸门：宿主用它挡掉不兼容的实现，见 D9 */
#define PS_ABI_VERSION 2u

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
    const double *time;   /* 秒；typed 表为共享 f64 视图 */
    const double *value;
    const char   *table;     /* typed 表名；旧通道为 NULL */
    const char   *column;    /* typed 列名；旧通道为 NULL */
    const void   *dataset;   /* 拥有该 typed 表的 PsDataset；旧通道为 NULL */
    uint64_t      generation;/* 数据源重建代数，0 表示旧通道 */
} PsChannelInfo;

/* 单通道统计结果。NaN/inf 视作缺失点，不计入 count。 */
typedef struct PsStats {
    uint32_t count;
    double   mean;
    double   std_dev;   /* 总体标准差 */
    double   min;
    double   max;
    double   rms;
    double   median;    /* 未请求时填 NaN */
} PsStats;

/* typed 列类型；顺序与 Rust ps_ndf::typed::ValueType 一致。 */
#ifndef PS_VALUE_TYPE_DEFINED
#define PS_VALUE_TYPE_DEFINED
typedef enum PsValueType {
    PS_VALUE_F64 = 0,
    PS_VALUE_F32 = 1,
    PS_VALUE_I64 = 2,
    PS_VALUE_U64 = 3,
    PS_VALUE_I32 = 4,
    PS_VALUE_U32 = 5,
    PS_VALUE_I16 = 6,
    PS_VALUE_U16 = 7,
    PS_VALUE_I8 = 8,
    PS_VALUE_U8 = 9,
    PS_VALUE_BOOL = 10,
    PS_VALUE_TEXT = 11
} PsValueType;
#endif

/* 一帧/一批行里的单列。values 按 type 解释；nulls 长度 (rows+7)/8，NULL=全有效。 */
#ifndef PS_FRAME_COLUMN_DEFINED
#define PS_FRAME_COLUMN_DEFINED
typedef struct PsFrameColumn {
    const char    *name;
    const char    *unit;
    PsValueType    type;
    const void    *values;
    const uint8_t *nulls;
} PsFrameColumn;
#endif


/* ---- 数据集只由扩展/缓存通过通用构造 API 生成 ---- */

uint32_t             ps_dataset_channel_count(const PsDataset *ds);
const PsChannelInfo *ps_dataset_channel(const PsDataset *ds, uint32_t index);
void                 ps_dataset_free(PsDataset *ds);

/* 缓存层用：新建空数据集并追加通道，避免二次加载重新解析源文件。
 * 数组由内核复制；返回 0 成功，非 0 见 err。 */
PsDataset           *ps_dataset_new(void);
int32_t              ps_dataset_add_channel(PsDataset *ds, const char *name, const char *unit,
                                            const double *time, const double *value, uint32_t count,
                                            char *err, uint32_t err_cap);
/* 表级构造：一个 topic 一张表，表内通道共享同一时间轴。 */
int32_t              ps_dataset_add_table(PsDataset *ds, const char *table, const double *time,
                                          uint32_t count, char *err, uint32_t err_cap);
int32_t              ps_dataset_add_table_channel(PsDataset *ds, const char *table,
                                                  const char *name, const char *unit,
                                                  const double *value, uint32_t count,
                                                  char *err, uint32_t err_cap);
/* typed 表级构造：时间轴用 UTC 微秒；列值按 PsValueType 解释，nulls 可选位图（1=null）。 */
int32_t              ps_dataset_add_table_typed(PsDataset *ds, const char *table,
                                                const int64_t *time_us, uint32_t count,
                                                char *err, uint32_t err_cap);
int32_t              ps_dataset_add_table_column_typed(PsDataset *ds, const char *table,
                                                       const char *name, const char *unit,
                                                       uint32_t value_type, const void *values,
                                                       const uint8_t *nulls, uint32_t count,
                                                       char *err, uint32_t err_cap);
/* frame_batch 落库：表不存在则按首批列建表，存在则校验列顺序后追加；seq 表内自增。 */
int32_t              ps_dataset_append_frame(PsDataset *ds, const char *table,
                                             const int64_t *time_us, uint32_t rows,
                                             const PsFrameColumn *columns, uint32_t column_count,
                                             char *err, uint32_t err_cap);
/* 设置 live ring 字节预算；table 为 NULL 表示 Dataset 默认预算，bytes=0 取消 ring。 */
int32_t              ps_dataset_set_ring_budget(PsDataset *ds, const char *table, uint64_t bytes,
                                                char *err, uint32_t err_cap);
/* typed 表当前行数；表不存在返回 0。 */
uint64_t             ps_dataset_table_rows(PsDataset *ds, const char *table);
/* typed 表视口 LOD；out 为 (time_sec,min,max) 三元组，长度至少 cap*3，返回实际三元组数。 */
uint32_t             ps_dataset_lod_minmax(PsDataset *ds, const char *table, const char *column,
                                           int64_t t0_us, int64_t t1_us, uint32_t px,
                                           double *out, uint32_t cap);
/* typed 表按行取值；out_time 为秒，out_value 为 f64 兼容值。返回 0 成功、1 越界、2 null。 */
int32_t              ps_dataset_value_at(PsDataset *ds, const char *table, const char *column,
                                         uint64_t row, double *out_time, double *out_value);
/* typed 表按 UTC 微秒取最近行；返回 0 成功、1 空表/找不到。 */
int32_t              ps_dataset_value_near(PsDataset *ds, const char *table, const char *column,
                                           int64_t t_us, double *out_time, double *out_value);
/* 统一取值入口：旧通道读 f64 视图，typed 列走 ps_dataset_value_at。 */
/* typed 表按时间窗口批量取值；out 为 time_sec/value 两个数组，cap 为各自容量。
 * 行数超过 cap 时等步长抽稀；返回写入行数。 */
uint32_t             ps_dataset_read_window(PsDataset *ds, const char *table, const char *column,
                                            int64_t t0_us, int64_t t1_us,
                                            double *out_time, double *out_value, uint32_t cap);
static inline double ps_channel_value_at(const PsChannelInfo *ch, uint32_t i)
{
    if (!ch)
        return 0.0;
    if (ch->value)
        return ch->value[i];
    if (ch->dataset && ch->table && ch->column)
    {
        double t = 0.0, v = 0.0;
        ps_dataset_value_at((PsDataset *)ch->dataset, ch->table, ch->column, i, &t, &v);
        return v;
    }
    return 0.0;
}

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
/* 曲线级滤波：直接对内存序列应用同一套算子，不改数据集。
 * 返回 0 成功；6 缓冲区不足（out_n 仍为实际长度）。 */
int32_t              ps_filter_apply_series(const char *filter_id, const char *params_json,
                                            const double *t, const double *v, uint32_t n,
                                            double *out_t, double *out_v, uint32_t cap,
                                            uint32_t *out_n, char *err, uint32_t err_cap);
/* 四元数 w/x/y/z → `<out_topic>.roll/.pitch/.yaw` 三条通道，要求同点数同时间戳。 */
int32_t              ps_dataset_apply_quat_rpy(PsDataset *ds, const char *w, const char *x,
                                               const char *y, const char *z, const char *out_topic,
                                               uint8_t degrees, uint8_t unwrap,
                                               char *err, uint32_t err_cap);
/* 删除通道（包括派生通道）。 */
int32_t              ps_dataset_remove_channel(PsDataset *ds, const char *name);

/* ---- 分析类算子（S2 计算库的后端）。全部 f64，结果写进调用方缓冲区。
 * 缓冲不足一律返回 6，且 out_n/out_m 仍回填实际长度，调用方可据此重试。 ---- */

/* 一次遍历出 count/mean/std/min/max/rms；with_median 非 0 时多一次排序。 */
int32_t ps_stats_series(const double *v, uint32_t n, int32_t with_median,
                        PsStats *out, char *err, uint32_t err_cap);

/* 单边幅度谱，长度 = nfft/2+1（nfft 为补零后的 2 的幂）。
 * window：0 矩形、1 Hann、2 Hamming、3 Blackman。 */
int32_t ps_fft_series(const double *v, uint32_t n, double fs, int32_t window,
                      double *out_freq, double *out_mag, uint32_t cap, uint32_t *out_m,
                      char *err, uint32_t err_cap);

/* Welch 功率谱密度。nfft 为 0 时取 256，非 2 的幂会进位；overlap 为 0..1。
 * 归一化为密度口径（单边乘 2，DC/Nyquist 不乘）。 */
int32_t ps_psd_series(const double *v, uint32_t n, double fs, uint32_t nfft, double overlap,
                      int32_t window, double *out_freq, double *out_psd, uint32_t cap,
                      uint32_t *out_m, char *err, uint32_t err_cap);

/* 巴特沃斯 IIR（二阶节级联）。kind：0 低通、1 高通、2 带通。
 * 低通/高通：order 取偶数，fc 为截止频率；带通：fc 为中心频率，bandwidth 为带宽比例（0.01..1）。 */
int32_t ps_iir_series(const double *v, uint32_t n, double fs, double fc, uint32_t order,
                      int32_t kind, double bandwidth, double *out, uint32_t cap, uint32_t *out_n,
                      char *err, uint32_t err_cap);

/* 抗混叠抽取：内置低通 FIR + 按 factor 取点，时间轴同步抽取。 */
int32_t ps_decimate_series(const double *t, const double *v, uint32_t n, uint32_t factor,
                           double *out_t, double *out_v, uint32_t cap, uint32_t *out_n,
                           char *err, uint32_t err_cap);

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

/* ---- 缓存：机制在 kernel，key/失效策略在 app ---- */

/* 按源文件指纹写数据集缓存；原子写（临时文件 + rename）。
 * 返回 0 成功；1 参数错误；2 IO/格式错误。 */
int32_t ps_cache_save_dataset(const PsDataset *ds, const char *path,
                              int64_t source_size, int64_t source_mtime,
                              char *err, uint32_t err_cap);

/* 读回缓存：0 命中；1 不存在/指纹过期；2 文件损坏。
 * 命中时 *out 为新数据集，调用方用 ps_dataset_free 释放。 */
int32_t ps_cache_load_dataset(const char *path,
                              int64_t source_size, int64_t source_mtime,
                              PsDataset **out, char *err, uint32_t err_cap);

/* 参数 JSON 缓存：同样按源文件指纹校验；0 成功/命中，1 过期，2 损坏。 */
int32_t ps_cache_save_params(const char *path, const char *json,
                             int64_t source_size, int64_t source_mtime,
                             char *err, uint32_t err_cap);
int32_t ps_cache_load_params(const char *path,
                             int64_t source_size, int64_t source_mtime,
                             char *out, uint32_t cap,
                             char *err, uint32_t err_cap);


#ifdef __cplusplus
}
#endif
#endif /* PS_KERNEL_H */

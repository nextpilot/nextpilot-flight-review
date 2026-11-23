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

/* ---- 数据面：数据集与通道 ---- */

typedef struct PsDataset PsDataset;  /* 不透明句柄，ps_dataset_free 释放 */

/* 通道视图。name/unit/time/value 都指向数据集内部内存：数据集释放后立即失效，
 * UI 不得跨数据集生命周期持有。 */
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
    PS_VALUE_TEXT = 11,
    PS_VALUE_OBJECT = 12  /* 每行一个 uint64 对象句柄，payload 走 blob_put/get */
} PsValueType;
#endif

/* 列压缩编解码器；ps_dataset_column_encode 的 codec 参数。 */
#ifndef PS_CODEC_DEFINED
#define PS_CODEC_DEFINED
typedef enum PsCodec {
    PS_CODEC_CONSTANT = 0,  /* 全列同值 */
    PS_CODEC_FOR = 1,       /* 基值 + 定长偏移，适合小范围整数 */
    PS_CODEC_DICTIONARY = 2,/* 去重值表 + 行索引，适合低基数/文本 */
    PS_CODEC_BITPACK = 3    /* 布尔按位打包 */
} PsCodec;
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
/* 数据代际：每次成功追加（含建表）递增，读侧据此判断派生视图是否需要刷新。 */
uint64_t             ps_dataset_generation(const PsDataset *ds);
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
/* typed 表级构造：时间轴用纳秒；列值按 PsValueType 解释，nulls 可选位图（1=null）。 */
int32_t              ps_dataset_add_table_typed(PsDataset *ds, const char *table,
                                                const int64_t *time_ns, uint32_t count,
                                                char *err, uint32_t err_cap);
int32_t              ps_dataset_add_table_column_typed(PsDataset *ds, const char *table,
                                                       const char *name, const char *unit,
                                                       uint32_t value_type, const void *values,
                                                       const uint8_t *nulls, uint32_t count,
                                                       char *err, uint32_t err_cap);
/* frame_batch 落库：表不存在则按首批列建表，存在则校验列顺序后追加；seq 表内自增。 */
int32_t              ps_dataset_append_frame(PsDataset *ds, const char *table,
                                             const int64_t *time_ns, uint32_t rows,
                                             const PsFrameColumn *columns, uint32_t column_count,
                                             char *err, uint32_t err_cap);
/* 设置 live ring 字节预算；table 为 NULL 表示 Dataset 默认预算，bytes=0 取消 ring。 */
int32_t              ps_dataset_set_ring_budget(PsDataset *ds, const char *table, uint64_t bytes,
                                                char *err, uint32_t err_cap);
/* ---- S4 对象列：payload 存内核池，跨 C ABI 只传不透明句柄（RFC D4，禁裸指针） ----
 * 句柄 = slot+1，slot 只增不复用；句柄生命周期绑定数据集，free 后失效，勿跨集持有。 */
/* 写入 payload 返回句柄（>0），失败返回 0；type_tag 由调用方约定，内核只透传。 */
uint64_t    ps_dataset_blob_put(PsDataset *ds, const void *payload, uint32_t len,
                                uint32_t type_tag, char *err, uint32_t err_cap);
/* 按句柄读，字节级拷贝到 out。返回 0 成功；1 参数错；2 句柄无效/已释放；3 out 不足。 */
int32_t     ps_dataset_blob_get(const PsDataset *ds, uint64_t handle, void *out,
                                uint32_t out_cap, uint32_t *out_len, uint32_t *out_type_tag);
/* 增加/释放外部引用。0 成功；2 句柄无效/引用已尽。 */
int32_t     ps_dataset_blob_retain(PsDataset *ds, uint64_t handle);
int32_t     ps_dataset_blob_release(PsDataset *ds, uint64_t handle);

/* typed 表当前行数；表不存在返回 0。 */
uint64_t             ps_dataset_table_rows(PsDataset *ds, const char *table);
/* typed 表视口 LOD；out 为 (time_sec,min,max) 三元组，长度至少 cap*3，返回实际三元组数。 */
uint32_t             ps_dataset_lod_minmax(PsDataset *ds, const char *table, const char *column,
                                           int64_t t0_ns, int64_t t1_ns, uint32_t px,
                                           double *out, uint32_t cap);
/* typed 表按行取值；out_time 为秒，out_value 为 f64 兼容值。返回 0 成功、1 越界、2 null。 */
int32_t              ps_dataset_value_at(PsDataset *ds, const char *table, const char *column,
                                         uint64_t row, double *out_time, double *out_value);
/* typed 表按行取文本值（params/facts 等 key-value 表）：写入 NUL 结尾缓冲（cap 字节，超长截断），
 * out_len 回填实际字节数（不含 NUL，可先传 out=NULL/cap=0 查询长度）。返回 0 成功、1 表/列/行不存在、2 非文本列或 null。 */
int32_t              ps_dataset_text_at(PsDataset *ds, const char *table, const char *column,
                                        uint64_t row, char *out, uint32_t cap, uint32_t *out_len);
/* typed 表按纳秒取最近行；返回 0 成功、1 空表/找不到。 */
int32_t              ps_dataset_value_near(PsDataset *ds, const char *table, const char *column,
                                           int64_t t_ns, double *out_time, double *out_value);
/* 统一取值入口：旧通道读 f64 视图，typed 列走 ps_dataset_value_at。 */
/* typed 表按时间窗口批量取值；out 为 time_sec/value 两个数组，cap 为各自容量。
 * 行数超过 cap 时等步长抽稀；返回写入行数。 */
uint32_t             ps_dataset_read_window(PsDataset *ds, const char *table, const char *column,
                                            int64_t t0_ns, int64_t t1_ns,
                                            double *out_time, double *out_value, uint32_t cap);
/* typed 表内 seq；不存在返回 UINT64_MAX。 */
uint64_t             ps_dataset_seq_at(PsDataset *ds, const char *table, uint32_t row);
/* typed 列原始类型；不存在/旧通道返回 -1。 */
int32_t              ps_dataset_column_type(PsDataset *ds, const char *table, const char *column);

/* ---- M2.7 kernel 优化与功能补全 ---- */

/* 2.7.1 整列原始读取：借出 typed 列的内部连续缓冲（零拷贝）。
 * out_values 按 *out_type（PsValueType）解释，元素数 *out_count；
 * out_nulls 可空：非空时指向 null 位图（(count+7)/8 字节，1=null）。
 * 指针只在数据集只读期间有效（append/remove/free 后失效）。
 * 返回 0 成功；1 参数错误；3 Text 列不支持；4 表/列不存在。 */
int32_t              ps_dataset_column_raw(const PsDataset *ds, const char *table, const char *column,
                                           const void **out_values, uint64_t *out_count, uint32_t *out_type,
                                           const uint8_t **out_nulls);

/* ---- S5 选择性列编码压缩 ---- */
/* 对指定列开启压缩编码，按列显式选择；F64 绘图主通道不调用即保零拷贝。
 * codec 取 PsCodec；返回 0 成功，1 参数错误，2 表/列不存在，3 已编码，4 不适合该 codec。 */
int32_t              ps_dataset_column_encode(PsDataset *ds, const char *table, const char *column,
                                              uint32_t codec, char *err, uint32_t err_cap);
/* 列是否编码：0=原生，1=已编码，-1=表/列不存在。 */
int32_t              ps_dataset_column_is_encoded(const PsDataset *ds, const char *table, const char *column);
/* 取列解码后的数值（null 行写 NaN）到调用方 f64 缓冲，返回写入行数（<=cap）。 */
uint32_t             ps_dataset_column_decode_f64(const PsDataset *ds, const char *table, const char *column,
                                                  double *out, uint32_t cap);

/* 2.7.5 数据集切片：按时间范围 [t0_ns, t1_ns]（含端点）与列子集抽出新 PsDataset。
 * columns 为列名数组，NULL/0 取全部列；新数据集只含 typed 表，独立生命周期。
 * 返回 0 成功；1 参数错误；2 名字非法；3 表/列不存在；5 内部错误（见 err）。 */
int32_t              ps_dataset_slice(const PsDataset *ds, const char *table,
                                      const char *const *columns, uint32_t column_count,
                                      int64_t t0_ns, int64_t t1_ns,
                                      PsDataset **out, char *err, uint32_t err_cap);

/* 2.7.6 多列对齐读取：同表多列一次读完 [t0_ns, t1_ns] 的对齐行区间。
 * out_values 是长度 column_count 的指针数组，每元素指向 cap 大小 f64 缓冲；
 * 行数超 cap 时等步长抽稀，各列严格同行对齐；null 输出 NaN。
 * 返回实际写入行数（0 = 空窗口/参数错误）。 */
uint32_t             ps_dataset_read_window_multi(const PsDataset *ds, const char *table,
                                                  const char *const *columns, uint32_t column_count,
                                                  int64_t t0_ns, int64_t t1_ns,
                                                  double *out_time, const double *const *out_values,
                                                  uint32_t cap);

/* 2.7.9 分位数统计：probs 为分位点（0.5/0.95/0.99…），线性插值口径。
 * NaN/inf 不参与；out 容量必须 >= prob_count。
 * 返回 0 成功；2 缓冲不足。 */
int32_t              ps_stats_quantiles(const double *v, uint32_t n,
                                        const double *probs, uint32_t prob_count,
                                        double *out, uint32_t out_cap,
                                        char *err, uint32_t err_cap);

static inline const char *ps_value_type_name(int32_t t)
{
    static const char *names[] = {"F64", "F32", "I64", "U64", "I32", "U32",
                                  "I16", "U16", "I8", "U8", "Bool", "Text"};
    return t >= 0 && t < 12 ? names[t] : "";
}
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
 * ps_dataset_apply_filter：input 为通道名，filter_id 为内置算子，
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
/* 四元数 w/x/y/z → roll/pitch/yaw，**数组级**（不写回 dataset；派生语义专用）。
 * 输出写调用方缓冲区，各 cap = n。返回 0 成功，非 0 失败（err 带原因）。 */
int32_t              ps_quat_to_rpy(uint32_t n, const double *w, const double *x,
                                    const double *y, const double *z,
                                    uint8_t degrees, uint8_t unwrap,
                                    double *out_roll, double *out_pitch, double *out_yaw,
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

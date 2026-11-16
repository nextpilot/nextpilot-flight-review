#pragma once
/* PlotScope 扩展 SDK：数据面 + UI 面契约。纯 C，不依赖 Qt，不依赖内核内部类型。
 * base 与 extensions 只共同依赖本头；扩展不 include 宿主 UI。
 *
 * UI 面只给 .ui 路径和 JSON 回调：扩展提供 .ui + JSON，宿主加载，不返回 QWidget*。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS_EXTENSION_ABI_VERSION 7u
#define PS_HOST_API_VERSION 2u
#define PS_DATA_SOURCE_PROTOCOL_VERSION 3u

#if defined(_WIN32)
#define PS_EXTENSION_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define PS_EXTENSION_EXPORT __attribute__((visibility("default")))
#else
#define PS_EXTENSION_EXPORT
#endif

/* 扩展 family：入口第一层分类，app 按 family 分派到不同 catalog。 */
#define PS_FAMILY_DATA_LOADER 1u   /* 文件/日志入口：probe + load */
#define PS_FAMILY_LIVE_SOURCE 2u   /* 实时源：MAVLink/ROS/OEM7/串口 */
#define PS_FAMILY_PARSER      3u   /* 消息解析：DBC/MAVLink/RTCM */
#define PS_FAMILY_TOOLBOX     4u   /* 算子/分析工具 */
#define PS_FAMILY_PANEL        5u   /* 面板/可视化扩展 */
#define PS_FAMILY_DATA_SOURCE 6u   /* 统一数据源：文件/流同一族，替代 loader + live source */

/* 能力位：宿主据此决定显示哪些入口、是否写缓存。 */
#define PS_CAP_FILE      (1u << 0)
#define PS_CAP_PROBE     (1u << 1)
#define PS_CAP_PARAMS    (1u << 2)
#define PS_CAP_SCHEMA    (1u << 3)
#define PS_CAP_UI        (1u << 4)
#define PS_CAP_CACHE     (1u << 5)
#define PS_CAP_STREAM    (1u << 6)
#define PS_CAP_RECORD    (1u << 7)
#define PS_CAP_COMMAND   (1u << 8)   /* 支持向设备发送调试/控制命令 */
#define PS_CAP_REPLAY    (1u << 9)   /* 同一扩展可用 __replay_path 回放原始文件 */
#define PS_CAP_PROTOCOL  (1u << 10)  /* 注册帧协议 kind/schema/validate */
#define PS_CAP_RIBBON    (1u << 11)  /* 通过 ui->ribbon_json 贡献菜单 */
#define PS_CAP_OP        (1u << 12)  /* 注册通用算子 */
#define PS_CAP_CURVE     (1u << 13)  /* 注册曲线能力 */
#define PS_CAP_PANEL     (1u << 14)  /* 注册面板 */

/* 宿主函数表：扩展不再直接链接 kernel，也不靠 exe 导出 ps_dataset_* 符号。
 * 扩展用 host->dataset_new/add_channel 构造数据集；数据集所有权仍归宿主。
 * 这张表由宿主在 load/open 时传入，跨 Windows/Linux 同一套语义。 */
typedef struct PsHostApi {
    uint32_t abi_version;       /* 必须等于 PS_HOST_API_VERSION */
    uint32_t kernel_abi_version;/* 当前 kernel ABI 版本，供扩展诊断 */
    void *(*dataset_new)(void);
    int32_t (*dataset_add_channel)(void *dataset, const char *name, const char *unit,
                                   const double *time_s, const double *value, uint32_t count,
                                   char *err, uint32_t err_cap);
    void (*dataset_free)(void *dataset);
    /* 可选：解析进度。done/total 均为字节、行或记录数；total=0 表示不确定。
     * 宿主在 load 调用期间提供线程局部回调，扩展在解析循环中调用即可。 */
    void (*progress)(uint64_t done, uint64_t total);
} PsHostApi;

/* Loader 数据面。所有 JSON 返回：>=0 为写入字节数；<0 失败。
 * load 返回 0 成功，数据集所有权交给宿主。 */
typedef struct PsLoaderVtable {
    uint32_t abi_version;
    const char *id;
    const char *label;
    const char *filter;       /* QFileDialog 过滤器 */
    const char *extensions;   /* 逗号分隔，如 ".csv,.tsv"；带点、小写 */
    const char *format_tag;   /* 数据源列表显示名 */
    uint8_t tree;             /* 是否有 topic/ID 层级 */
    uint8_t has_params;       /* 是否有参数表 */
    uint8_t reserved[6];

    int32_t (*schema_json)(char *out, uint32_t cap);
    int32_t (*probe_json)(const char *path, const char *opts_json,
                          char *out, uint32_t cap);
    int32_t (*default_options)(const char *path, char *out, uint32_t cap);
    int32_t (*params_json)(const char *path, char *out, uint32_t cap);
    int32_t (*load)(const char *path, const char *opts_json, const PsHostApi *host,
                    void **dataset, char *err, uint32_t err_cap);
} PsLoaderVtable;

/* 实时源向宿主写数据的唯一通道：扩展按批提交，宿主负责追加到实时通道。
 * 返回非 0 表示宿主拒绝该批（例如缓冲区已满）；扩展可停止本次 poll。 */
typedef struct PsSourceSink {
    void *ctx;
    int32_t (*channel_batch)(void *ctx, const char *name, const char *unit,
                             const double *time_s, const double *value, uint32_t count);
    int32_t (*event)(void *ctx, const char *topic, const char *payload_json);
    /* 可选：扩展把原始字节交给宿主统一录制。topic/encoding 用于回放路由。 */
    int32_t (*raw_batch)(void *ctx, const char *topic, const char *encoding,
                         int64_t timestamp_ns, const uint8_t *data, uint32_t size);
} PsSourceSink;

/* ---- 统一数据输入 ABI（PJ4 形状，PlotScope 宿主服务） --------------------
 * 一个 DataSource 扩展族：文件/流是 capabilities，不是两个 family。
 * 扩展内部可用 C++ class；跨边界只暴露本结构 + opaque ctx。
 * 所有槽位必须 noexcept；失败写 PsError，不让异常穿过 ABI。
 */

#define PS_INPUT_FILE        (1u << 0)  /* 有限文件导入 */
#define PS_INPUT_STREAM      (1u << 1)  /* 长连接流 */
#define PS_INPUT_DIRECT      (1u << 2)  /* 扩展自己解码并写 sink */
#define PS_INPUT_DELEGATED   (1u << 3)  /* 扩展只把原始字节交给宿主 parser */
#define PS_INPUT_PAUSE       (1u << 4)  /* pause/resume 可用 */
#define PS_INPUT_DIALOG      (1u << 5)  /* get_dialog 返回配置界面 */
#define PS_INPUT_COMMAND     (1u << 6)  /* send_command 可用 */
#define PS_INPUT_REPLAY      (1u << 7)  /* 同一扩展可回放自己录制的原始文件 */
#define PS_INPUT_RECORD      (1u << 8)  /* 支持把原始字节落盘 */
#define PS_INPUT_SEEK        (1u << 9)  /* 文件模式支持 seek */

typedef enum PsInputState {
    PS_INPUT_IDLE = 0,
    PS_INPUT_RUNNING = 1,
    PS_INPUT_PAUSED = 2,
    PS_INPUT_DONE = 3,
    PS_INPUT_ERROR = 4
} PsInputState;

typedef struct PsError {
    int32_t code;
    char message[256];
} PsError;

typedef struct PsStringView {
    const char *data;
    uint32_t size;
} PsStringView;

/* 扩展自带 UI：要么给 .ui 路径，要么给 schema JSON；都归扩展所有。 */
typedef struct PsInputDialog {
    const char *ui_path;
    const char *schema_json;
    const char *default_config_json;
} PsInputDialog;

/* 宿主服务在 bind() 一次性注入。sink/host 生命周期覆盖整个实例。 */
typedef struct PsDataSourceServices {
    uint32_t abi_version;
    const PsHostApi *host;
    const PsSourceSink *sink;
} PsDataSourceServices;

typedef struct PsDataSourceVtable {
    uint32_t protocol_version;   /* = PS_DATA_SOURCE_PROTOCOL_VERSION */
    uint32_t struct_size;        /* = sizeof(PsDataSourceVtable) */

    void *(*create)(void);       /* 扩展内 new，返回 opaque ctx */
    void (*destroy)(void *ctx);  /* 扩展内 delete，与 create 配对 */

    const char *manifest_json;   /* 编译期常量；id/name/version/file_extensions */

    uint64_t (*capabilities)(void *ctx);

    bool (*bind)(void *ctx, PsDataSourceServices services, PsError *err);

    bool (*save_config)(void *ctx, PsStringView *out_json, PsError *err);
    bool (*load_config)(void *ctx, PsStringView config_json, PsError *err);

    bool (*start)(void *ctx, PsError *err);
    void (*stop)(void *ctx);
    bool (*pause)(void *ctx, PsError *err);
    bool (*resume)(void *ctx, PsError *err);
    bool (*poll)(void *ctx, PsError *err);

    PsInputState (*current_state)(void *ctx);
    PsInputDialog (*get_dialog)(void *ctx);
    const void *(*get_extension)(void *ctx, PsStringView id);

    /* 可选尾部：宿主按 struct_size 判断是否存在。 */
    bool (*send_command)(void *ctx, PsStringView command,
                         PsStringView *out_json, PsError *err);
    bool (*seek)(void *ctx, double time_s, PsError *err);
    int32_t (*status_json)(void *ctx, char *out, uint32_t cap);
    /* 有限文件导入：成功返回 0 并交出一个 PsDataset*；失败写 err。 */
    int32_t (*import_file)(void *ctx, const char *path, const char *opts_json,
                           void **dataset, char *err, uint32_t err_cap);
    /* 外部 transport 模式：宿主把共享 transport 的原始字节喂进来，扩展只解析。 */
    int32_t (*feed_raw)(void *ctx, const char *topic, const char *encoding,
                        int64_t timestamp_ns, const uint8_t *data, uint32_t size,
                        char *err, uint32_t err_cap);
} PsDataSourceVtable;


/* 实时源：不返回 QWidget*，不持有宿主 UI。config 用 JSON，status 用 JSON。
 * open 成功后 handle 归扩展所有，close 必须释放；poll 在宿主 UI 线程调用。 */
typedef struct PsLiveSourceVtable {
    uint32_t abi_version;
    const char *id;
    const char *label;
    const char *kind;          /* sim / mavlink / serial / oem7 ... */
    const char *transport;     /* internal / udp / serial ... */
    uint8_t recordable;
    uint8_t reserved[7];

    int32_t (*config_schema_json)(char *out, uint32_t cap);
    int32_t (*default_config_json)(char *out, uint32_t cap);
    int32_t (*open)(const char *config_json, const PsSourceSink *sink, const PsHostApi *host,
                    void **handle, char *err, uint32_t err_cap);
    int32_t (*poll)(void *handle, int32_t timeout_ms, char *err, uint32_t err_cap);
    int32_t (*close)(void *handle);
    int32_t (*status_json)(void *handle, char *out, uint32_t cap);
    /* 可选。向设备发送一条调试/控制命令；返回 >0 为 out 的 JSON 字节数，<0 失败。
     * 具体命令语法由扩展决定，宿主只做通用调试控制台。 */
    int32_t (*send_command)(void *handle, const char *command, char *out, uint32_t cap);
    /* 外部 transport 模式：宿主把共享 transport 的原始字节喂进来，扩展只解析。 */
    int32_t (*feed_raw)(void *handle, const char *topic, const char *encoding,
                        int64_t timestamp_ns, const uint8_t *data, uint32_t size,
                        char *err, uint32_t err_cap);
} PsLiveSourceVtable;

/* 扩展 UI：只交 .ui 文件和 JSON，绝不返回 QWidget*。
 * dialog_ui 相对扩展包目录；panels_json/ribbon_json 为贡献声明，可为 NULL。 */
typedef struct PsUiVtable {
    uint32_t abi_version;
    const char *dialog_ui;
    const char *panels_json;
    const char *ribbon_json;

    int32_t (*get_data)(const char *widget, char *out, uint32_t cap);
    int32_t (*on_event)(const char *widget, const char *payload_json,
                        char *out, uint32_t cap);
    int32_t (*on_tick)(const char *state_json, char *out, uint32_t cap);
} PsUiVtable;

/* 协议扩展：扩展可同时注册 loader 与 protocol；编辑器 schema 交给宿主渲染。 */
typedef struct PsProtocolVtable {
    uint32_t abi_version;
    const char *kind;          /* 协议 kind，例如 sequential 或自定义 */
    const char *label;
    const char *schema_json;   /* 编辑器 schema；可为 NULL */
    /* 校验协议 JSON：返回 0 通过，>0 错误条数；err 写换行分隔的错误。 */
    int32_t (*validate)(const char *json, char *err, uint32_t cap);
    /* 规范化协议 JSON：返回输出字节数，<0 失败；可选。 */
    int32_t (*normalize)(const char *json, char *out, uint32_t cap);
} PsProtocolVtable;

typedef struct PsExtensionDescriptor {
    uint32_t abi_version;      /* 必须等于 PS_EXTENSION_ABI_VERSION */
    const char *manifest_json; /* 编译期内嵌，不读外部 JSON */
    uint32_t family;           /* PS_FAMILY_* */
    uint32_t capabilities;     /* PS_CAP_* 位或 */
    const PsLoaderVtable *loader;   /* data_loader 家族使用，其他家族可为 NULL */
    const PsUiVtable *ui;           /* 可选；无自定义 UI 时为 NULL */
    const PsLiveSourceVtable *source; /* data_source 家族使用，其他家族可为 NULL */
    const PsDataSourceVtable *data_source; /* PS_FAMILY_DATA_SOURCE 使用，过渡期可为 NULL */
    const PsProtocolVtable *protocol;    /* 可选：同一扩展可额外贡献协议 */
} PsExtensionDescriptor;

typedef const PsExtensionDescriptor *(*PsExtensionEntryFn)(void);

#ifdef __cplusplus
}
#endif

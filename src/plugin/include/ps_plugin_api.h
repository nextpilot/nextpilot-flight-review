#pragma once
/* PlotScope 插件 SDK：数据面 + UI 面契约。纯 C，不依赖 Qt，不依赖内核内部类型。
 * app 与 plugins 只共同依赖本头；插件不 include src/app。
 *
 * UI 面只给 .ui 路径和 JSON 回调：插件提供 .ui + JSON，宿主加载，不返回 QWidget*。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS_PLUGIN_ABI_VERSION 4u
#define PS_HOST_API_VERSION 1u

/* 插件 family：入口第一层分类，app 按 family 分派到不同 catalog。 */
#define PS_FAMILY_DATA_LOADER 1u   /* 文件/日志入口：probe + load */
#define PS_FAMILY_DATA_SOURCE 2u   /* 实时源：MAVLink/ROS/OEM7/串口 */
#define PS_FAMILY_PARSER      3u   /* 消息解析：DBC/MAVLink/RTCM */
#define PS_FAMILY_TOOLBOX     4u   /* 算子/分析工具 */
#define PS_FAMILY_PANEL        5u   /* 面板/可视化扩展 */

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

/* 宿主函数表：插件不再直接链接 kernel，也不靠 exe 导出 ps_dataset_* 符号。
 * 插件用 host->dataset_new/add_channel 构造数据集；数据集所有权仍归宿主。
 * 这张表由宿主在 load/open 时传入，跨 Windows/Linux 同一套语义。 */
typedef struct PsHostApi {
    uint32_t abi_version;       /* 必须等于 PS_HOST_API_VERSION */
    uint32_t kernel_abi_version;/* 当前 kernel ABI 版本，供插件诊断 */
    void *(*dataset_new)(void);
    int32_t (*dataset_add_channel)(void *dataset, const char *name, const char *unit,
                                   const double *time_s, const double *value, uint32_t count,
                                   char *err, uint32_t err_cap);
    void (*dataset_free)(void *dataset);
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

/* 实时源向宿主写数据的唯一通道：插件按批提交，宿主负责追加到实时通道。
 * 返回非 0 表示宿主拒绝该批（例如缓冲区已满）；插件可停止本次 poll。 */
typedef struct PsSourceSink {
    void *ctx;
    int32_t (*channel_batch)(void *ctx, const char *name, const char *unit,
                             const double *time_s, const double *value, uint32_t count);
    int32_t (*event)(void *ctx, const char *topic, const char *payload_json);
} PsSourceSink;

/* 实时源：不返回 QWidget*，不持有宿主 UI。config 用 JSON，status 用 JSON。
 * open 成功后 handle 归插件所有，close 必须释放；poll 在宿主 UI 线程调用。 */
typedef struct PsDataSourceVtable {
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
     * 具体命令语法由插件决定，宿主只做通用调试控制台。 */
    int32_t (*send_command)(void *handle, const char *command, char *out, uint32_t cap);
} PsDataSourceVtable;

/* 插件 UI：只交 .ui 文件和 JSON，绝不返回 QWidget*。
 * dialog_ui 相对插件包目录；panels_json/ribbon_json 为贡献声明，可为 NULL。 */
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

typedef struct PsPluginDescriptor {
    uint32_t abi_version;      /* 必须等于 PS_PLUGIN_ABI_VERSION */
    const char *manifest_json; /* 编译期内嵌，不读外部 JSON */
    uint32_t family;           /* PS_FAMILY_* */
    uint32_t capabilities;     /* PS_CAP_* 位或 */
    const PsLoaderVtable *loader;   /* data_loader 家族使用，其他家族可为 NULL */
    const PsUiVtable *ui;           /* 可选；无自定义 UI 时为 NULL */
    const PsDataSourceVtable *source; /* data_source 家族使用，其他家族可为 NULL */
} PsPluginDescriptor;

typedef const PsPluginDescriptor *(*PsPluginEntryFn)(void);

#ifdef __cplusplus
}
#endif

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

#define PS_PLUGIN_ABI_VERSION 1u

/* Loader 数据面。所有 JSON 返回：>=0 为写入字节数；<0 失败。
 * load 返回 0 成功，数据集所有权交给宿主。 */
typedef struct PsLoaderVtable {
    uint32_t abi_version;
    const char *id;
    const char *label;
    const char *filter;       /* QFileDialog 过滤器 */
    const char *format_tag;   /* 数据源列表显示名 */
    uint8_t tree;             /* 是否有 topic/ID 层级 */
    uint8_t has_params;       /* 是否有参数表 */
    uint8_t reserved[6];

    int32_t (*schema_json)(char *out, uint32_t cap);
    int32_t (*probe_json)(const char *path, const char *opts_json,
                          char *out, uint32_t cap);
    int32_t (*default_options)(const char *path, char *out, uint32_t cap);
    int32_t (*params_json)(const char *path, char *out, uint32_t cap);
    int32_t (*load)(const char *path, const char *opts_json, void **dataset,
                    char *err, uint32_t err_cap);
} PsLoaderVtable;

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
    const PsLoaderVtable *loader;
    const PsUiVtable *ui;      /* 可选；无自定义 UI 时为 NULL */
} PsPluginDescriptor;

typedef const PsPluginDescriptor *(*PsPluginEntryFn)(void);

#ifdef __cplusplus
}
#endif

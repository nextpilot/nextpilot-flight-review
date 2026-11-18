#pragma once
/*
 * PlotScope 解析器 SPI（Service Provider Interface）——纯 C ABI。
 *
 * 设计依据（需求）：
 *   「解析器核心工作，由调用者给他喂数据，解析器出来结果，然后调用者把数据
 *     推送到 rust 或者其他位置。解析器不限制编写语言（可以由 dll 提供）。
 *     解析器允许主动注册（内部扩展中），也支持自动扫描（插件中的）。」
 *
 * 职责边界：
 *   解析器 = 字节 → 结构化批 的纯转换。
 *     不读文件、不写存储、不认识宿主、不管生命周期。
 *   调用者 = 喂字节、收批、决定批去哪（Rust typed 表 / 其它存储 / 转发 / 丢弃）。
 *
 * 由此得到的性质：
 *   - 同一份解析器既能读文件、也能读串口/网络流（喂数据的是调用者）；
 *   - 解析器可独立单测：喂一段字节，断言输出结构，无需启动整个 app；
 *   - 结果归宿由调用者决定，解析器不绑定 Rust；
 *   - 纯 C ABI → C / C++ / Rust / Python 均可实现，可放进 .so/.dll。
 *
 * 批结构复用老仓库 src/application/runtime/host/include/ps_extension_api.h 的
 * PsFrameBatch 布局——它与 Rust ps_dataset_append_frame 的入参同构，落库零转换。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SPI ABI 版本。宿主与解析器必须一致，否则拒绝加载。 */
#define PS_PARSER_ABI_VERSION 1u

/* 列值类型：与 kernel ps_kernel.h / 老仓库 ps_extension_api.h 同值。 */
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

#ifndef PS_FRAME_COLUMN_DEFINED
#define PS_FRAME_COLUMN_DEFINED
/* 一列的值：按 type 解释；nulls 为 (rows+7)/8 字节位图（1=null），NULL=全有效。
 * 指针仅在回调期间有效，宿主负责拷贝。 */
typedef struct PsFrameColumn {
    const char    *name;
    const char    *unit;
    PsValueType    type;
    const void    *values;
    const uint8_t *nulls;
} PsFrameColumn;
#endif

#ifndef PS_FRAME_BATCH_DEFINED
#define PS_FRAME_BATCH_DEFINED
/* 一批行：同一批的所有列共享 time_us（UTC 微秒）。
 * 这正是 Rust ps_dataset_append_frame 的入参形状。 */
typedef struct PsFrameBatch {
    const char          *table;    /* 表名（= topic / 通道组） */
    const int64_t       *time_us;  /* 行时间戳，长度 = rows */
    uint32_t             rows;     /* 行数 */
    uint32_t             columns;  /* 列数 */
    const PsFrameColumn *column_data;
} PsFrameBatch;
#endif

/* 错误码与消息。失败时写入，消息以 NUL 结尾。 */
typedef struct PsError {
    int32_t code;
    char    message[256];
} PsError;

/* 解析器元数据：编译期常量，进程内可查。 */
typedef struct PsParserInfo {
    uint32_t    abi_version;   /* = PS_PARSER_ABI_VERSION */
    const char *id;            /* 全局唯一，如 "ulog" / "dbc" / "mavlink" */
    const char *display_name;  /* 展示名 */
    const char *extensions;    /* 逗号分隔、带点小写，如 ".ulg,.ulog"；可为空 */
    uint8_t     can_sniff;     /* 是否支持探测格式 */
    uint8_t     streaming;     /* 是否支持增量喂（流式） */
    uint8_t     reserved[2];
} PsParserInfo;

/* 结果出口：由调用者提供，解析器只往这里写。 */
typedef struct PsParserSink {
    void *ctx;

    /* 一批行。返回 0 成功；非 0 表示宿主拒绝（例如缓冲区已满），
     * 解析器可据此停止本次 feed。 */
    int32_t (*frame_batch)(void *ctx, const PsFrameBatch *batch);

    /* 进度（可选）：done/total 均为字节或记录数；total=0 表示不确定。
     * total 可为 NULL（宿主不关心进度）。 */
    void (*progress)(void *ctx, uint64_t done, uint64_t total);
} PsParserSink;

/* 解析器实例：opaque ctx + vtable。
 * 所有槽位都不得抛异常穿过 ABI；失败写 PsError。 */
typedef struct PsParserVtable {
    uint32_t abi_version;  /* = PS_PARSER_ABI_VERSION */
    uint32_t struct_size;  /* = sizeof(PsParserVtable)，用于判定尾部可选槽位 */

    /* 生命周期：create/destroy 必须配对。 */
    void *(*create)(void);
    void  (*destroy)(void *ctx);

    /* 探测：给一段头字节，判断是否本格式。
     * 宿主用它做「未知扩展名」的自动选择。 */
    int32_t (*sniff)(void *ctx, const uint8_t *data, uint32_t size);

    /* 喂数据：解析器把识别出的批通过 sink 吐回给调用者。
     * 返回 0 成功；<0 失败（err 写原因）。 */
    int32_t (*feed)(void *ctx, const uint8_t *data, uint32_t size,
                    const PsParserSink *sink, PsError *err);

    /* 收尾：冲刷残留（文件尾部的半帧、索引表、参数段等）。 */
    int32_t (*finish)(void *ctx, const PsParserSink *sink, PsError *err);

    /* 重置：复用于下一个文件。 */
    void (*reset)(void *ctx);

    /* ---- 以下为可选尾部槽位（宿主按 struct_size 判定是否存在）---- */

    /* 解析器的静态元数据（比 PsParserInfo 更灵活：可运行时构造）。 */
    const PsParserInfo *(*info)(void *ctx);

    /* 能力位：支持的文件后缀 / 是否可录制等。 */
    uint64_t (*capabilities)(void *ctx);
} PsParserVtable;

/* 插件导出入口：外部 .so/.dll 必须导出此符号。
 * 返回 NULL 表示该插件不提供解析器。 */
typedef const PsParserVtable *(*PsParserEntryFn)(void);

#ifdef __cplusplus
}
#endif

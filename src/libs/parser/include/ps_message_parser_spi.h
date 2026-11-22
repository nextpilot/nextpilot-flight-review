#pragma once
/*
 * 协议检查器 SPI（C ABI 镜像）——让 C / Rust / 插件形式的消息解析器也能接入。
 *
 * 与 IMessageParser.h 是同一能力的两种形态：
 *   - IMessageParser.h：C++ 内置扩展用，普通虚函数（configure 是普通虚方法）；
 *   - 本文件：外部 C 插件用，C ABI vtable，configure 走**可选尾部槽**。
 *
 * configure 槽位设计与 ps_parser_spi.h 完全同构：
 *   - 靠 struct_size 判定是否存在，老插件（struct_size 不含本槽位）不会被调用，用内置默认；
 *   - 传 JSON 键值对字符串（与 IMessageParser::configSchema 声明的字段同构）；
 *   - create() 之后、首次 parse() 之前调用一次；
 *   - 解析器只从这里取自己 schema 声明的参数，不读全局。
 *
 * 两套形态都汇入同一个注册表（ParserHub），宿主对来源无感。
 * 本轮：C++ 路径实装；C 插件加载链按既有插件扫描规划落地，本头先定契约。
 */

#include "ps_parser_spi.h" // 复用 PsError / PsValueType 定义

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS_MSG_PARSER_ABI_VERSION 1u

/* 解码字段（C 布局，字符串借用解析器缓冲，仅在回调期间有效）。 */
typedef struct PsMsgField {
    const char *name;
    const char *value; /* 已缩放/枚举后的可读值 */
    const char *unit;
    const char *enum_label;
    int32_t offset;
    int32_t size;
} PsMsgField;

/* 解码帧。 */
typedef struct PsMsgFrame {
    const char *protocol;
    const char *frame_name;
    const char *frame_key;
    double timestamp;
    const PsMsgField *fields;
    uint32_t field_count;
    uint8_t checksum_ok;
    uint8_t unknown;
    uint8_t reserved[2];
} PsMsgFrame;

/* 帧出口：解析器解出一帧就往这里写。 */
typedef struct PsMsgSink {
    void *ctx;
    /* 返回 0 继续；非 0 表示宿主拒绝，解析器可停止本次 parse。 */
    int32_t (*frame)(void *ctx, const PsMsgFrame *frame);
} PsMsgSink;

typedef struct PsMsgParserVtable {
    uint32_t abi_version;  /* = PS_MSG_PARSER_ABI_VERSION */
    uint32_t struct_size;  /* = sizeof(PsMsgParserVtable) */

    void *(*create)(void);
    void (*destroy)(void *ctx);

    /* 探测（可选）。>0 表示是本协议。 */
    int32_t (*sniff)(void *ctx, const uint8_t *data, uint32_t size);

    /* 核心行为（必需）：解码一段字节，通过 sink 吐帧。 */
    int32_t (*parse)(void *ctx, const uint8_t *data, uint32_t size,
                     const PsMsgSink *sink, PsError *err);

    /* === 以下尾部槽位按 struct_size 判定 === */

    /* 自述：协议可读名 / 用途 / 支持 transfer（逗号分隔）/ schema（JSON 字符串）。 */
    const char *(*protocol_name)(void);
    const char *(*usage)(void);
    const char *(*supported_transports)(void);
    const char *(*config_schema_json)(void);

    /* configure：JSON 键值对字符串或 NULL/空串（用默认）。create 后、parse 前调用一次。 */
    int32_t (*configure)(void *ctx, const char *params_json, PsError *err);
} PsMsgParserVtable;

/* configure 槽位是否存在。 */
static inline int ps_msg_parser_has_configure(const PsMsgParserVtable *v)
{
    return v && v->struct_size >= offsetof(PsMsgParserVtable, configure) + sizeof(v->configure)
        && v->configure != 0;
}

/* 插件导出入口。 */
typedef const PsMsgParserVtable *(*PsMsgParserEntryFn)(void);

#ifdef __cplusplus
}
#endif

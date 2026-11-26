# psproto

把一段**顺序字节流**切成帧、再把帧切成字段的模板语言。用户写一份 JSON 模板，解析器按模板读出通道（时间 + 数值）。

- 协议本体规范：[`SPEC.md`](SPEC.md)（v3，语义权威；本文是速览）
- 结构校验 schema：[`psproto.schema.json`](psproto.schema.json)（可挂到编辑器做自动补全/校验）

v3 是**重构**，不是兼容升级：旧键一律报"未知键"，没有迁移期。段键采用白名单校验，写错直接编译失败。

## 一个最小例子

```jsonc
{
  "name": "imu_frame",
  "mode": "bin",
  "layout": "sequential",
  "endian": "little",
  "maxsize": 32,
  "timestamp": { "expr": "time_us", "unit": "us", "origin": "first_frame" },
  "segments": [
    { "name": "sync", "type": "u8", "output": "none", "expect": [170] },
    { "name": "time_us", "type": "u32" },
    { "name": "roll", "type": "i16", "transform": "self*0.01", "unit": "deg" },
    { "name": "pitch", "type": "i16", "transform": "self*0.01", "unit": "deg" },
    { "name": "crc", "type": "u8", "output": "none", "expect": [{ "op": "crc8", "poly": 7, "over": "sync:roll" }] },
  ],
}
```

帧布局按 `segments` 顺序逐段读取；物理换算写进 `transform`（能折成 `self*k+b` 的表达式在编译期折进零开销路径，`phys = raw*0.01` 即此形态）；约束校验统一走 `expect`。

## 顶层键（14 个）

`name`（必填）/ `mode`（必填，`bin` 或 `text`，无 mixed）/ `segments`（必填）/
`description` / `layout`（`sequential`）/ `endian`（`little`/`big`，别名 `le`/`be`）/
`maxsize` / `term`（或 `terminator`）/ `blocks` / `derived` / `series` / `types` / `timestamp`

没有 `psproto`、`version` 键，二者都算未知键。

## bin 段：10 个键

`name`（必填）/ `type`（必填）/ `description` / `unit` / `output` / `count` / `transform` / `expect` / `on_mismatch` / `if`

| 键            | 说明                                                                                                                        |
| ------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `output`      | `channel`（出通道，默认）/ `aux`（参与表达式与校验、不出通道）/ `none`（只占位、只校验）——取代 v2 的 `emit`/`hidden`/`plot` |
| `count`       | `3`（恰好）/ `"n"`（表达式求值）/ `"*"`（读到帧尾，全局唯一）                                                               |
| `transform`   | `"self*0.25"` 表达式，或算子数组 `[{"op":"scale","k":0.25},…]`                                                              |
| `expect`      | 约束数组，逐项 AND，判**原始值**：`[">63"]` / `["[36,427)"]` / `["{1,3,5}"]` / CRC 等摘要 op                                |
| `on_mismatch` | `reject`（默认）/ `warn`（出 NaN 通道 + 明细）/ `skip`                                                                      |
| `if`          | 条件表达式，不成立时该段不存在                                                                                              |

位域：`type` 取 `bit`（lsb0）/ `rbit`（msb0），宽度与起始位写进 `count:"宽@偏移"`（如 `"29@0"`）。位域段不推进字节游标，偏移相对帧起点。

## text 段：12 个键

`name` / `type` / `description` / `unit` / `output` / `format` / `count` / `transform` / `expect` / `on_mismatch` / `if` / `delimiter`

`format` 直接写扩展 scanf 语法：`%d` `%x` `%f` `%s` `%[^,]` `%t` `%5d` `%*d`，普通字符逐字匹配。text **必须先能定位帧首**（首段字面锚点或顶层 `terminator`），否则编译期直接报错。

v2 词法键（`literal` / `key` / `width` / `rest` / `base` / `pattern` / `fixed` / `signed` / `sci` / `empty` / `repeater`）**全部删除**，替代见 SPEC §10.6。

```jsonc
{
  "name": "nmea_gga",
  "mode": "text",
  "term": ["\r\n"],
  "segments": [
    { "name": "msgid", "format": "$GPGGA,", "output": "none" },
    { "name": "msgid2", "format": "%[^,]", "expect": [{ "op": "eq", "value": "$GPGGA" }], "output": "aux" },
  ],
}
```

## 段组与派生

- `blocks`：字段组（顺序块 / 位域块 `of` / 组重复 `repeater:"*"`），只允许一层。
- `derived`：顶层派生量，不读字节、不占空间，可引用前序派生；引用块内字段用裸字段名。

## 类型

`u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 bool bcd char`（bin 内置全集，另有表达式类型 `number`/`rest`/`time` 等 text 形态用）；整型保持整型、浮点保持浮点；表达式不做隐式降级。text 派生函数：`time_hhmmss` / `deg_ddmm` / `text_number`。

## 帧族变体 `series`

```jsonc
"series": {
  "selector": "msgid",
  "payload": "payload",
  "fallback": "reject",
  "variants": [
    { "keys": [1], "name": "att",  "segments": [ /* … */ ] },
    { "fallback": true }
  ]
}
```

## 时间戳

```jsonc
"timestamp": { "expr": "time_us", "unit": "us", "origin": "first_frame", "fallback": "reject" }
```

- `unit`：`s` / `ms` / `us` / `ns`；输出整数 ticks，**不经过 double**。
- `origin: first_frame` 先做整数相减再换算；`fallback: index` 用帧序号兜底。

## 摘要脚本后端（Lua 5.4）

`expect` 内置 20 种 op（判值 7 种 + 摘要 12 种 + `script`）。脚本兜底：

```jsonc
{ "expect": [{ "op": "script", "script": "return xor_of(ctx.data) > 127 and 1 or 0" }] }
```

脚本是普通 Lua chunk，可用全局 `ctx`（`ctx.data` 原始字节、`ctx.len`、`ctx.fields` 段名→值）。`return` 必须是数字。沙箱只开 `base/string/table/math`；指令配额 40 万条，超限报 `ScriptTimeout`。

## 旧键一律拒绝

v1/v2 的 `size` / `counts` / `value` / `hidden` / `plot` / `hasValue` / `role` / `fromVariant` / `scale`（bin 段） / `emit` / `enum` / `range` / `checks` / `digest`（对象） / `repeat` / `until` / `length` / `derive`（段级）/ `as` / `literal` / `key` / `width` / `rest` / `base` / `pattern` / `empty` **一律报错，不做兼容映射**。想表达什么用哪个键，见 SPEC §11 对照表。

> 容器格式（PCAP / MDF4 / BLF / PNG）、嵌套对象树、压缩加密不在 psproto 范围，由上层专门解析器处理。

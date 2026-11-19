# psproto

把一段**顺序字节流**切成帧、再把帧切成字段的模板语言。用户写一份 JSON 模板，解析器按模板读出通道（时间 + 数值）。

- 协议本体规范：[`SPEC.md`](SPEC.md)
- 结构校验 schema：[`psproto.schema.json`](psproto.schema.json)（可挂到编辑器做自动补全/校验）
- 设计取舍与分批：`docs/roadmap/psproto-v2.md`

## 一个最小例子

```jsonc
{
  "name": "imu_frame",
  "layout": "sequential",
  "endian": "little",
  "maxsize": 32,
  "timestamp": { "expr": "time_us", "unit": "us", "origin": "first_frame" },
  "segments": [
    { "name": "sync",  "type": "u8",  "expect": 0xAA, "emit": "none" },
    { "name": "time_us", "type": "u32" },
    { "name": "roll",  "type": "i16", "scale": 0.01, "unit": "deg" },
    { "name": "pitch", "type": "i16", "scale": 0.01, "unit": "deg" },
    { "name": "crc",   "type": "u8", "emit": "none",
      "digest": { "alg": "crc8/atm", "over": ["sync:roll"] } }
  ]
}
```

帧布局按 `segments` 顺序逐段读取；`scale/offset` 做物理换算 `phys = raw*scale + offset`。

## 顶层键

| 键 | 说明 |
|---|---|
| `name` | 协议名（必填） |
| `layout` | 目前唯一 `sequential` |
| `endian` | 默认端序 `little`/`big`（别名 `le`/`be`），段可覆盖 |
| `maxsize` | 单帧字节上限，解码时强制 |
| `segments` | 字段段数组（必填） |
| `series` | 帧族变体：按选择器换整套布局 |
| `types` | 类型别名，复用 `type/scale/unit/range` |
| `timestamp` | 时间戳取值与单位 |
| `starts` / `terminator` | ASCII 行协议的起始/终止标记（可选） |

## 段键（常用）

| 键 | 说明 |
|---|---|
| `name` | 段名，表达式按名引用（必填） |
| `type` | 见下表；或 `@别名` 引用 `types` |
| `count` | 元素个数（数组长度）；标量省略。**字节数由 `type` 推算，不写** |
| `scale` / `offset` | 物理换算 |
| `unit` | 单位，显示用 |
| `emit` | `channel`（出通道，默认）/ `aux`（中间量）/ `none`（仅校验/容器） |
| `expect` | 常量匹配（同步字、状态字） |
| `checks` | 谓词校验：eq/ne/lt/le/gt/ge/in/range/mask |
| `digest` | 摘要校验：命名 CRC / sum / xor / parity，可带 `over` 段区间、`inject` 常量字节 |
| `bit` | 位域：`offset`（位）/ `width` / `order: lsb0\|msb0` / `base` 容器段 |
| `if` | 条件字段，false 时整段跳过 |
| `enum` | 值 → 名字映射 |
| `range` | 声明物理范围（不判失败） |
| `repeat` / `until` | 重复方式：`elems`（按 count）/ `eos` / `until`（读到标记） |
| `derive` | 计算字段，不从字节读 |
| `length` | 长度段反算：本段值必须等于后面字段合计字节/元素数 |

## 类型

`u8 i8 u16 i16 u32 i32 u64 i64 f32 f64 bool bcd text`

- 整型保持整型，浮点保持浮点；表达式不做隐式降级（规则见 SPEC §3 / v2 §10.9）。
- `text` 为文本段；ASCII 派生函数 `time_hhmmss` / `deg_ddmm` / `text_number`。

## 帧族变体 `series`

```jsonc
"series": {
  "selector": "msgid",
  "payload": "payload",
  "fallback": "reject",
  "variants": [
    { "keys": [1], "name": "att",  "segments": [ /* … */ ] },
    { "keys": [2, 3], "name": "env", "segments": [ /* … */ ] },
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

## v1 → v2 键名对照

| v1 | v2 |
|---|---|
| `size`（元素个数） | `count` |
| `counts` | `length` |
| `role` | 由 `expect` / `length` / `digest` / `series.payload` 显式表达 |
| `hidden` / `plot` / `hasValue` | `emit: none / aux / channel` |
| `value` | `expect` |
| `str` / `string` / `ascii` / `char` | `text` |
| `fromVariant` | 删除（实现细节，不进模板） |

> 容器格式（PCAP / MDF4 / BLF / PNG）、嵌套对象树、压缩加密不在 psproto 范围，由上层专门解析器处理。

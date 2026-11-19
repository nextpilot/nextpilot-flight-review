# psproto v2 规范

> 这是**协议的规范**，不是讨论稿。目标：一个有明确边界、能拿出去对标 Kaitai / DBC 的帧协议描述语言。
> 设计取舍的理由见 `docs/roadmap/psproto-v2.md`；本文只讲**协议长什么样、和 v1 差在哪、能覆盖什么**。

---

## 0. 定位与边界

**psproto 是一个"帧协议描述语言"，不是通用序列化格式。**

| 做 | 不做 |
|---|---|
| 描述**一个字节流如何切成帧** | 描述容器格式（PCAP / MDF4 / BLF / PNG） |
| 描述**帧内每个字段怎么解**（定长、变长、变体、位域、ASCII 行） | 描述嵌套对象树（Protobuf / ASN.1 / JSON） |
| 描述**校验与时间戳** | 描述压缩、加密、跨帧上下文（除 CRC 流） |
| 运行时加载、可被 GUI 编辑、可生成代码 | 追求成为通用数据建模语言 |

**判断标准**：能描述"一段字节 → 一帧 → 若干字段"，就归 psproto；需要"先读索引表再跳着读"的，归上层容器解析器，不进 psproto。

### 0.1 能力归属原则（先定这条，避免把能力砍窄）

**psproto 把能力按协议族需要配齐，不按"谁来用"砍。**

- ULog / MAVLink / NMEA / OEM7 等格式在产品里可以有**专门的代码解析器**（字典驱动、复杂协议用代码更稳）；这是**落地方式**的选择，不构成从 psproto 规范里删除对应能力的理由。
- 同一项能力（位域、ASCII 行协议、带 `inject` 的 CRC、24 位 CRC）：有现成代码解析器就用代码；遇到私有格式、临时调试、陌生报文，再用 psproto 模板。用不用、走哪条路，是上层和用户的选择。
- 规范只排除**容器 / 嵌套 / 索引跳读**这类超出"一帧怎么解"定位的东西；其余能力一律保留。

---

## 1. 顶层

```jsonc
{
  "psproto": "2",              // 版本键与 "version" 分离，v1 的 "version":"1" 自动识别
  "name": "ubx_nav_pvt",
  "description": "u-blox NAV-PVT",
  "layout": "sequential",      // 目前唯一布局；保留扩展位
  "endian": "little",          // 默认端序，段可覆盖
  "maxsize": 2048,             // 单帧字节上限，解码时强制
  "starts": "$",               // ASCII 帧起始标记（可选）
  "terminator": "\r\n",        // ASCII 帧终止标记（可选）
  "segments": [ … ],
  "series": { … },             // 帧族变体（可选）
  "types": { … },              // 类型别名（可选）
  "timestamp": { … }           // 时间戳（可选）
}
```

---

## 2. 段（segment）

### 2.1 全键表

| 键 | 类型 | 作用 | 说明 |
|---|---|---|---|
| `name` | string | 唯一名，表达式按名引用 | 必填 |
| `type` | string | `u8/i8/u16/i16/u32/i32/u64/i64/f32/f64/bool/bcd/char`，或 `@别名` | 必填 |
| `count` | 数字/表达式 | **元素个数（数组长度）** | 标量省略（默认 1）；**字节数由 `type` 推算，不写**；可引用前面段（`"len"`） |
| `length` | object | 长度段反算绑定 | 原 `counts`：这段的值必须等于后面字段合计的字节/元素数；见 §2.4 |
| `endian` | `little`/`big` | 覆盖顶层端序 | 别名 `le`/`be` 在解析层归一，**不再有 `bigEndian` 布尔** |
| `scale` / `offset` | number | 物理换算 `phys = raw*scale + offset` | 存在时通道类型为 `f64` |
| `unit` | string | 单位 | 显示用 |
| `emit` | `channel`/`aux`/`none` | 是否产出通道 | 见 2.2 |
| `expect` | 数字/字符串/数组 | 常量匹配 | 原 `value` |
| `expectExpr` | 表达式 | 常量匹配（表达式形式） | 原 `valueExpr` |
| `checks` | array | **谓词校验**（eq/ne/lt/le/gt/ge/in/range/mask） | 见 §4.1 |
| `digest` | object | **摘要校验**（crc/sum/xor/parity） | 见 §4.2 |
| `bit` | object | 位域 | 见 §6 |
| `if` | 表达式 | 条件字段，false 时整段跳过 | 新增 |
| `enum` | object | 值→名字映射 | 新增 |
| `range` | `[min,max]` | **声明**物理范围（不判失败） | 与 `checks.range` 语义区分 |
| `repeat` | `elems`/`eos`/`until` | 重复方式 | 默认 `elems`（由 `count` 定） |
| `until` | 表达式/字符串 | 重复终止条件（读到该值为止） | 配 `repeat:"until"` |
| `consume` | bool | ASCII 段是否吃掉结束分隔符 | 见 §7 |
| `empty` | `"nan"`/`"skip"` | ASCII 空字段策略 | `nan`：空段填 NaN；`skip`：跳过不产出；见 §7 |
| `derive` | 表达式 | 计算字段（非字节读出） | 新增 |
| `description` | string | 文档 | 编辑器/Schema 生成用 |

### 2.2 `emit` 三态（v1 的 `hidden`/`plot`/`hasValue` 三个键已删除）

| 值 | 含义 | v1 等价 |
|---|---|---|
| `channel`（默认） | 出通道，可画 | 默认 |
| `aux` | 出通道但不进默认画布（中间量） | `plot:false` |
| `none` | 不出通道（仅供校验/位域容器/常量） | `hidden:true` / `hasValue` / `role:sync\|length\|checksum` |

**默认规则**（避免用户漏写）：
- 带 `expect` / `digest` / `length` 的段默认 `none`；
- `series.payload` 指向的段默认 `aux`；
- 其余默认 `channel`。

### 2.3 **删除 `role`**

v1 的 `role`（sync/length/payload/checksum/data）与显式键**完全重叠**：
`role:"sync"` ↔ `expect`、`role:"length"` ↔ `length`、`role:"checksum"` ↔ `digest`、`role:"payload"` ↔ `series.payload`。

保留它只会有两种结果：**与显式键冲突**，或**变成没人读的装饰**。两种都是"概念太多"的病。
→ **v2 删 `role`，行为一律由显式键决定。** v1 模板迁移时按表机械映射。

---

### 2.4 `count` 与字节数的关系（不要再用 `size` 这个词）

- **`count` = 元素个数**。人写协议时想的是"这一段有几个数"，不是"几个字节"。
- **字节数不写**：`bytes = count × sizeof(type)`，由引擎按 `type` 推算。`u16` 天然是 2 字节，不需要在段里重复一遍。
- 原始字节块（`type:"u8"`）此时 `count` 就是字节数，两种说法一致，不冲突。
- **标量段省略 `count`**，默认 1。
- `length`（原 `counts`）是**长度段反算**：声明这个段的值必须等于后面 payload 的合计字节/元素数，用于校验帧长度，与数组长度是两件事。

---

## 3. 表达式

运算符 `+ - * / % & | ^ << >>`，函数 `abs/min/max/clamp`，显式转换 `toF64/toI64/toU64`。

ASCII 派生函数（作用于 `char` 段）：`time_hhmmss`（`hhmmss.ss` → 当日秒数）、`deg_ddmm`（度分格式 → 十进制度）、`text_number`（文本 → 数值，支持十进制/十六进制，空字段按 `empty` 策略处理）。

**类型由操作数决定，不做隐式降级**（完整规则见 `docs/roadmap/psproto-v2.md` §10.9）：

| 场景 | 结果 |
|---|---|
| 整型 op 整型 | `i64`（有符号）或 `u64`（全无符号），不窄化 |
| 浮点参与 | `f64`，但只允许浮点 op 浮点，**整型+浮点报错** |
| `/`、`%` 整数域 | 整除、取模（**不需要 `//`**） |
| `/`、`%` 浮点域 | 常规除法 |
| 比较 | `bool`；整型族内部可互比，整型 vs 浮点**报错** |

> 注意：typed 之后**不再需要引入 `//` 运算符**。类型决定除法语义（Rust 做法），比"符号区分"少一个概念。

---

## 4. 校验：拆成 `checks` 与 `digest`

v1 把"判真假"和"算摘要"塞进同一个 `op` 命名空间（`op:"in"` 还被误归进摘要分支）。v2 拆开。

### 4.1 `checks`（谓词，判真假）

```jsonc
"checks": [
  {"op":"eq","value":0xAA},
  {"op":"range","min":0,"max":8000},
  {"op":"mask","value":0x0F,"expr":"0x3"},
  {"op":"in","values":[0,1,2]},
  {"op":"lt","expr":"other_seg"},
  {"scope":"frame","op":"le","expr":"len"}
]
```
`over`（段区间）只对摘要类有意义，**从 `checks` 移除**。

### 4.2 `digest`（摘要，算校验和）

```jsonc
"digest": {
  "alg": "crc16/ccitt-false",         // 命名算法；详见内置表
  "over": ["cls:payload"],            // 段区间 [from:to]
  "width": 16,                        // 位宽（支持 24/32 等非 8 倍数）
  "poly": "...", "init": "...", "refin": true, "refout": true, "xorout": "..."  // 覆盖命名算法参数
}
```

**必须补的三项能力**（`digest` 是市场覆盖的关键）：

1. **命名算法表**：`crc8/atm`、`crc16/ccitt-false`、`crc16/modbus`、`crc16/x25`、`crc32/iso`、`crc24/q`、`nmea_xor`、`sum8`、`xor8`、`parity`。
   现在只有 `crc8/crc16/crc32` 三个固定 + 手写 5 个参数键，用户为常见变体要写一堆键。
2. **`width` 按位**（现在按字节）：RTCM 的 CRC-24Q 无法表达。
3. **`inject`**：CRC 计算时在末尾追加常量字节（**MAVLink 的 CRC_EXTRA 必需**，否则整个 MAVLink 族都解不了）：
   ```jsonc
   "digest": { "alg":"crc16/mcrf4xx", "over":["len:payload"], "inject":[0x41] }
   ```

---

## 5. 变体与条件

**帧族**（整帧替换，保留）：
```jsonc
"series": {
  "selector": "msg_id",
  "payload": "payload",
  "variants": [
    {"keys":["parsed"], "segments":[ … ]},
    {"keys":["unknown"], "fallback":true}
  ]
}
```

**条件字段**（字段级局部，新增，替代 v1 的 `fromVariant` 泄漏）：
```jsonc
{"name":"ext_flags","type":"u8","if":"msg_id == 12"}
```

**删除 `fromVariant`**：它是 codec 拼通道前缀的实现细节，不该出现在用户写的模板里。

---

## 6. 位域（v2 的核心新增）

```jsonc
{"name":"id",  "type":"u32", "bit":{"offset":0,"width":29,"order":"msb0"}},
{"name":"body","type":"u8","count":8,"emit":"none"},
{"name":"rpm", "type":"u16", "bit":{"base":"body","offset":8,"width":16,"order":"msb0"},
               "scale":0.25, "unit":"rpm", "range":[0,8000]}
```

| 键 | 含义 |
|---|---|
`offset` | 位偏移（**位**，不是字节）
`width` | 位宽
`order` | `lsb0`（等价 DBC Intel）/ `msb0`（等价 DBC Motorola）
`base` | 位域容器段名（不写则 `offset` 相对帧起点）

约束：`width + (offset % 8) <= 64`，保证单次 `loadBytes <= 8`，不需要 128 位整数（MSVC 没有）。

---

## 7. ASCII（NMEA / AIS 行协议）

```jsonc
{
  "starts": "$", "terminator": "\r\n",
  "segments": [
    {"name":"msgid","type":"char","until":",","emit":"aux"},
    {"name":"time",  "type":"char","until":",","derive":"time_hhmmss","emit":"channel"},
    {"name":"lat",   "type":"char","until":",","derive":"deg_ddmm","emit":"channel"},
    {"name":"empty", "type":"char","until":",","empty":"nan"},
    {"name":"chk",   "type":"u8","digest":{"alg":"nmea_xor"}}
  ]
}
```

需要的五个原语：`starts`、`terminator`、段级 `until`、段级 `consume`、派生函数 `time_hhmmss` / `deg_ddmm` / `text_number`。
**空字段**（NMEA 的 `,,`）必须能用 `empty: "nan"|"skip"` 表达，否则真实日志全部解析失败。

**重同步按行，不按字节**：解析失败跳到下一个 `terminator`，成本是丢一行，不是 O(n²) 逐字节试探。

---

## 8. 时间戳

```jsonc
"timestamp": {
  "expr": "time_us",
  "unit": "us",               // s / ms / us / ns（v1 缺 ns）
  "origin": "first_frame",    // 或 "absolute"
  "raw": true,                // 用原始值而非物理换算值
  "fallback": "reject"        // reject / index
}
```

- 求值结果**必须是整型**，浮点报错；
- `unit` → `ticksPerSecond`：`s`=1、`ms`=1e3、`us`=1e6、`ns`=1e9；
- `origin: first_frame` → **先 i64 相减，再换算**；
- 输出 `vector<int64_t> ticks`，**不经过 double**。

---

## 9. 类型别名 `types`（新增）

```jsonc
"types": {
  "engine_rpm": {"type":"u16","scale":0.25,"unit":"rpm","range":[0,8000]},
  "wheel_speed": {"type":"u16","scale":0.01,"unit":"km/h"}
},
"segments": [
  {"name":"rpm","@":"engine_rpm"},
  {"name":"wheel_fl","@":"wheel_speed"}
]
```
对应 DBC 的 `SGTYPE`。同一物理量在 CAN 报文里出现 4 次时，不用复制 4 份 `scale/unit`。

---

## 10. v1 → v2 变更总表

### 修错（3 项，非改不可）

| # | 现状 | v2 | 为什么 |
|---|---|---|---|
| 1 | `endian` 解码认 `be`、校验只认 `big` | 统一 `little`/`big`，别名在解析层归一 | 写 `"be"` 会"能跑但校验报错" |
| 2 | `std::stoul` 解析 `poly`/`init`/`xorout` **无异常保护** | 安全解析，失败写 `err` | `"poly":"0xZZ"` 直接抛异常终止进程 |
| 3 | `/` **恒为 trunc**（浮点也被截断） | 类型决定语义 | `1/2.0` 现在得 0 |

> 注：v1 的 `size` 表示**元素个数**，这是符合写协议习惯的，**语义保留、不反转**；只是名字容易被误读成"字节大小"，改名为 `count`（归入下表收敛项）。

### 收敛（7 项，删概念）

| # | 删除/合并 | 理由 |
|---|---|---|
| 5 | **删 `role`** | 与 `expect`/`length`/`digest`/`series.payload` 完全重叠 |
| 6 | `hidden`+`plot`+`hasValue` → **`emit`** 三态 | 三个开关在 codec 里串在同一个 `if` 上，干的是同一件事 |
| 7 | `value` → **`expect`** | 与 `valueExpr` 并列时"谁是常量谁是表达式"靠猜 |
| 8 | `str`/`string`/`ascii`/`text` → **`char`** | 四个名字一个类型；最终只留 `char` |
| 9 | **删 `fromVariant`** | 实现细节泄漏进 schema |
| 10 | `size` → **`count`**（元素个数，语义不变） | `size` 容易被误读成字节数；字节宽由 `type` 推算，不需要第二个字段 |
| 11 | `counts` → **`length`** | 避免与新的 `count` 看成一对；它是长度段反算，与数组长度是两件事 |

### 新增（9 组，补表达力）

`bit` 位域、`if` 条件段、`digest` 拆分+命名算法表+`inject`+位宽、`until`/`consume`/`starts`/`terminator`、`enum`、`range` 声明、`repeat: eos|until`、`derive` 计算字段、`types` 别名。

---

## 11. 市场覆盖矩阵（按能力分，不按协议名）

| # | 能力 | 代表协议 | v1 | v2 |
|---|---|---|---|---|
| 1 | 定长帧 + 同步字 + 校验 | 多数飞控私有帧 | ✓ | ✓ |
| 2 | 长度前缀变长 | UBX / 自定义 TLV | ✓ | ✓ |
| 3 | 帧族变体（选择器换布局） | UBX class/id | ✓ | ✓ |
| 4 | 校验带额外常量字节 | **MAVLink v1/v2（CRC_EXTRA）** | ✗ | ✓（`inject`） |
| 5 | 非 8 倍数位宽 CRC | **RTCM v3（CRC-24Q）** | ✗ | ✓（`width` 按位） |
| 6 | 位域 | **CAN / J1939 / CANopen / ADS-B / CCSDS** | ✗ | ✓ |
| 7 | 行终止 ASCII + 空字段 | **NMEA 0183** | ✗ | ✓ |
| 8 | 重复到标记 | TLV 列表 / EDR | ✗ | ✓ |
| 9 | 容器 / 嵌套 / 索引跳读 | PCAP / MDF4 / BLF / PNG / SpaceWire | ✗ | ✗（明确不覆盖） |
| 10 | 6-bit ASCII 装甲 | **AIS** | ✗ | ✗（P2，需专门解包器） |

**两轴说明**：表中 v2 列指"psproto 模板能否表达该能力"，**与产品里是否已有专门代码解析器无关**。ULog / MAVLink / NMEA / OEM7 可由代码解析器承担；同一项能力在规范里保留，遇到私有格式时模板也能写。两者并存，互不为前提。

**结论**：v1 覆盖 3/8 类"值得覆盖"的能力；v2 覆盖 8/8，仅容器与 AIS 装甲明确排除。
第 4、5 两条是**以前没意识到的硬缺口**：不补，psproto 模板这条路表达不了 MAVLink 的 CRC_EXTRA 与 RTCM 的 24 位 CRC；这两个协议族本身仍可由专门代码解析器承担。

---

## 12. 兼容与迁移

- 顶层用 `"psproto":"2"` 标识；读到 `"version":"1"` 或缺失时走 v1 兼容分支（**只读不写**）。
- `size` → `count` 是**纯改名**，语义不变（元素个数），可自动迁移；`counts` → `length` 同理。
- 其余改名/合并项可自动迁移，迁移器输出逐条 diff。本规范没有"无法自动迁移"的语义反转项。

---

## 13. "拿得出手"的交付标准（12 项）

一个能对外宣称"可用"的协议库，缺任何一项都算半成品：

1. **本规范**（SPEC.md）—— 已有
2. **JSON Schema**：结构层校验（键、类型、枚举、必填/互斥）
3. **语义校验**：段名唯一、表达式引用存在、`over` 区间合法、变体覆盖完整、`series.payload` 指向存在
4. **诊断**：帧号 + 段名 + 偏移 + 原因，不是只 `++rejected`
5. **迁移器** v1→v2，含不可自动迁移项报告
6. **示例集**：定长 / 变长 / 变体族 / 位域 / ASCII / 校验 / 时间戳 各至少一个可跑样例
7. **一致性测试**：用真实日志（ULog、UBX、NMEA）解出的字段，与**既有成熟解析器**逐值比对（这是"半成品"和"能用"的分水岭）
8. **fuzz**：随机字节 + 畸形模板，不崩、不超时、不越界
9. **性能基线**：每 MB 耗时、零拷贝验证（payload 不复制）
10. **API 文档 + 版本策略**：v2 之后不破坏性变更
11. **错误码表**：所有失败路径有稳定标识，便于上层分类处理
12. **codegen**（C++/Python）—— 可选，但宣称"给其他语言用"就必须有

---

## 14. 一句话总结

**psproto v2 的调整不是"加功能"，而是三件事**：
**修 3 处错**（端序校验、崩溃、除法）、
**收敛 7 个概念**（role、hidden/plot/hasValue、value、四个文本类型名、fromVariant、size→count、counts→length）、
**补 9 组表达力**（位域、条件段、digest 参数化与 inject、ASCII 原语、enum、range、repeat、derive 计算字段、types 别名）。

做完这三件，能力覆盖从 3/8 到 8/8，概念数量反而**减少**。

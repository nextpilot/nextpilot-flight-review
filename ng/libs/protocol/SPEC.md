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
| `type` | string | `u8/i8/u16/i16/u32/i32/u64/i64/f32/f64/bool/bcd/char`，或 `types` 里定义的类型别名名 | 必填 |
| `count` | 数字/表达式 | **元素个数（数组长度）** | 标量省略（默认 1）；**字节数由 `type` 推算，不写**；可引用前面段（`"len"`） |
| `length` | object | 长度段反算绑定 | 原 `counts`：这段的值必须等于后面字段合计的字节/元素数；见 §2.4 |
| `endian` | `little`/`big` | 覆盖顶层端序 | 别名 `le`/`be` 在解析层归一，**不再有 `bigEndian` 布尔** |
| `scale` / `offset` | number | 物理换算 `phys = raw*scale + offset` | 存在时通道类型为 `f64` |
| `unit` | string | 单位 | 显示用 |
| `transform` | array | 值算子链（解析后按序应用） | `sexagesimal`/`scale`/`mul`/`offset`/`add`/`abs`/`round`/`clamp`；见 §4.3 |
| `emit` | `channel`/`aux`/`none` | 是否产出通道 | 见 2.2 |
| `expect` | 数字/字符串/数组 | 常量匹配 | 原 `value` |
| `checks` | array | **谓词校验**（eq/ne/lt/le/gt/ge/in/range/mask） | 见 §4.1 |
| `digest` | object | **摘要校验**（crc/sum/xor/parity） | 见 §4.2 |
| `bit` | object | 位域 | 见 §6 |
| `if` | 表达式 | 条件字段，false 时整段跳过 | 新增 |
| `enum` | object | 值→名字映射 | 新增 |
| `range` | `[min,max]` | **声明**物理范围（不判失败） | 与 `checks.range` 语义区分 |
| `repeat` | `elems`/`eos`/`until` | 重复方式 | 默认 `elems`（由 `count` 定）；`eos` = 读到帧尾，**必须是最后一个读字节的段**（后面只允许 `bit`（不推进字节游标）），否则编译期报错 |
| `until` | 表达式/字符串 | 重复终止条件（读到该值为止） | 配 `repeat:"until"` |
| `empty` | `"nan"`/`"skip"` | ASCII 空字段策略 | `nan`：空段填 NaN；`skip`：跳过不产出；见 §7 |
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

> **适用范围**：`checks` 是谓词（eq/ne/lt/le/gt/ge/in/range/mask），ASCII 与二进制都执行（ASCII 在段解析完即判，不符→`FieldInvalid`）。序列类（crc/sum/xor/parity）不属于 `checks`，ASCII 段写了会被编译期拒绝，统一走 `digest`。
> 文本常量要写成表达式字面量：`{"op":"eq","value":"'N'"}`（`value` 里的字符串按表达式解析，裸 `"N"` 会被当成段名引用）。

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

### 4.3 `transform` 算子链

`scale`/`offset` 是单一线性换算；真实协议常要"换算后再取整/钳位/拆度分"。`transform` 是解析后按序应用的算子链，每步作用于上一步的值：

| op | 参数 | 作用 |
|---|---|---|
| `sexagesimal` | `split`（0..8） | NMEA 的 `4807.038` → `48 + 7.038/60` |
| `scale` / `mul` | `k` | `v *= k` |
| `offset` / `add` | `b` | `v += b` |
| `abs` | — | 绝对值 |
| `round` | — | 四舍五入 |
| `clamp` | `min` / `max` | 钳到区间（可只给一端） |

```jsonc
{ "name":"lat", "type":"f64", "transform":[
    {"op":"sexagesimal","split":2},
    {"op":"scale","k":1.0},
    {"op":"clamp","min":-90,"max":90}
] }
```

约束：算子链只作用于 ASCII 的 token 数值（二进制段用 `scale`/`offset`）；`clamp` 的 `min` 必须 ≤ `max`。

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

### 6.1 位域段不占字节

位域段是对**已有字节区间**的视图，不推进字节游标、不单独占长度：

- 有 `base`：窗口相对该容器段的字节区间（典型 `{"name":"body","type":"u8","count":8,"emit":"none"}` 的 8 字节）。
- 无 `base`：窗口相对**帧起点**（CAN 裸帧只有 8 字节时常用）。
- 第 N 个位域段紧接着读下一个段的字节位置不受影响，所以一个 CAN 帧可以只写一条 `body`，其余全是位域视图。

`type` 决定两件事：值类型（决定通道列类型）与符号性（`i*` 会按 `width` 做符号扩展）。
`scale`/`offset` 照常作用于**读出的整数**，产出物理浮点列。

---

### 6.2 条件段 `if`

```jsonc
{"name":"flags","type":"u8","count":1},
{"name":"extra","type":"u8","count":1,"if":"flags == 1"},
{"name":"b","type":"u8","count":1}
```

条件表达式的类型规则与 `count` 完全一致；结果为假时：

- 该段**不存在**：不消耗字节、不产出通道、槽位保持空；
- 后面的段按「没有这一段」继续定位，所以 `b` 读到的是 `flags` 之后的第 2 个字节；
- 别的段引用被跳过段的槽位会明确报错，不静默给 0。

这是通用能力，不是「位域专用」：可选扩展块、按型号追加的尾部字段都用它表达。

---

## 7. ASCII（行协议：NMEA / 私有 CSV / 键值）

ASCII **不是另一套模型**：同样是"段 + 边界"，只是边界从字节宽度换成字符边界。
`$`、`*` 都是**段**（literal 段），不是协议级的 `starts`/`trailer` 概念。

### 7.1 段级边界（没有协议级 ascii/bin）

```jsonc
"term": ["\r\n", "\n"]    // 帧尾候选集：写了就硬校验，没写就不管
```

没有协议级 `lexer`/ascii/bin 开关：**每个段自己声明边界**（见 §7.2 的 `boundaryOf`）。

- `delimited`：分隔符切 token（NMEA、CSV 这类）
- `fixed`：定宽字符，段用 `width`
- `keyed`：`key=value`（后续批次）
- `term` **纯可选**：写了每条记录必须以它收尾，没写就不检查（靠 `digest` 宽度自证）

### 7.2 段的 ASCII 键

| 键 | 含义 |
|---|---|
| `literal` | 必须逐字符匹配的字面量段（`$`、`*`、`,`） |
| `delimiter` | 该段结束边界：字符串或数组；`"blank"` = 空格/Tab 字符类 |
| `repeater` | 只允许 `"*"`：重复到下一段边界（右边界 = 后一个 literal 或记录尾） |
| `rest` | 整段剩余原文，不切分 |
| `width` | 定宽字符数（`fixed` 词法） |
| `base` | 数字进制，**默认 10**；`16` 用于校验字段 |
| `fixed` | 定点小数位数（`fixed: 3` 表示 3 位小数） |
| `signed` / `sci` | 允许负号 / 科学计数法（默认都拒绝） |
| `transform` | `[{"op":"sexagesimal","split":3}]` → `DDMM.MMM` 转十进制度 |
| `empty` | 空字段策略：`nan` / `skip` / `raw`（默认报错） |
| `pattern` | 正则**只做校验**，不切分 |
| `on_mismatch` | `reject` / `warn` / `skip` |
| `digest` | 见 §4.2；ASCII 上 `from`/`to` 指向段名端点 |

ASCII 段的 `count` 可省略（默认 1）；`bit` 不允许出现在 ASCII 段。

### 7.3 例：NMEA GGA

```jsonc
{
  "name": "nmea_gga", "term": ["\r\n"], "maxsize": 128,
  "segments": [
    {"name":"dollar","type":"char","literal":"$","emit":"none"},
    {"name":"msgid","type":"string","delimiter":",","emit":"none"},
    {"name":"utc","type":"string","delimiter":",","emit":"none"},
    {"name":"lat","type":"number","delimiter":",","transform":[{"op":"sexagesimal","split":3}]},
    {"name":"ns","type":"string","delimiter":",","emit":"none"},
    {"name":"lon","type":"number","delimiter":",","transform":[{"op":"sexagesimal","split":3}]},
    {"name":"ew","type":"string","delimiter":",","emit":"none"},
    {"name":"fix","type":"number","delimiter":",","emit":"none"},
    {"name":"sats","type":"number","delimiter":",","emit":"none"},
    {"name":"hdop","type":"number","delimiter":",","emit":"none"},
    {"name":"alt","type":"number","delimiter":",","unit":"m"},
    {"name":"alt_unit","type":"string","delimiter":",","emit":"none"},
    {"name":"geoid","type":"string","delimiter":",","emit":"none"},
    {"name":"geoid_unit","type":"string","delimiter":",","emit":"none"},
    {"name":"age","type":"number","delimiter":",","empty":"skip","emit":"none"},
    {"name":"station","type":"string","delimiter":",","empty":"skip","emit":"none"},
    {"name":"star","type":"char","literal":"*","emit":"none"},
    {"name":"check","type":"number","base":16,"emit":"aux",
     "digest":{"alg":"xor8","from":"after:dollar","to":"before:star"}}
  ]
}
```

`digest` 算的是 **`$` 之后到 `*` 之前的连续原始字节**（含所有逗号），不是字段值拼回去。

### 7.4 例：无同步字的空白分隔（私有 CSV）

```jsonc
{"term":["\r\n","\n"],"maxsize":128,
 "segments":[
   {"name":"id","type":"number","delimiter":"blank"},
   {"name":"val","type":"number","delimiter":"blank"},
   {"name":"name","type":"string","delimiter":"blank"}]}
```

`blank` 是**字符类**（空格/Tab，连续空白算一个分隔符），不做正则切分。

### 7.5 流式语义

1. 首个 literal 段 → 在流里定位，之前字节丢弃（保留末尾 `literal.size()-1` 字节防跨包截断）；
2. 有 `term` 就在流里找它，作为记录逻辑尾；
3. 逐段判定边界；`*` 是硬匹配，**等不到就一直等**；
4. **等边界时又出现新帧首 `$` → 旧候选整体作废**，从新帧首重来（`RecordAborted`）；
5. 记录完整后算 `digest`，通过才产出；
6. 没有 `term` 时，靠 `check` 段的十六进制宽度自证记录尾（`xor8`=2、`crc16`=4、`crc32`=8 个字符）。

### 7.6 错误分类

`SyncMissing / RecordAborted / ChecksumMismatch / FieldInvalid / TooManyFields /
PatternMismatch / OverflowMaxsize / IncompleteAtEof`

每条带 `recordOffset`（记录首字节的流偏移）、`segment`（段名）、`detail`（期望/实际）；
明细最多 128 条 + `dropped` / `rejected` / `overflow` / `skipped` 计数，流继续。

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

`bit` 位域、`if` 条件段、`digest` 拆分+命名算法表+`inject`+位宽、`until`/`starts`/`terminator`、`enum`、`range` 声明、`repeat: eos|until`、`types` 别名。

**ASCII/混合协议（v2 新增，S4a）**：`SegBoundary{Byte,Literal,Delimit,Fixed,Rest,Repeat,Keyed}`
取代协议级 `lexer`——字节段边界由 `type × count` 推出，字符段由 `literal`/`delimiter`/`width`/`rest`/`key` 声明；
同一记录里可混排（文本头 + 二进制载荷）。流式语义：锚点定位、term 可选、`$`/`*` 硬匹配、新 `$` 作废旧候选、
无 term 靠 check 十六进制宽度自证；`digest.from/to` 走原始字节。时间轴由 `timestamp` 表达式求值。

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
**补 9 组表达力**（位域、条件段、digest 参数化与 inject、ASCII 原语、enum、range、repeat、types 别名）。

做完这三件，能力覆盖从 3/8 到 8/8，概念数量反而**减少**。

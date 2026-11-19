# psproto v2：模板驱动的动态帧解码库

> 定位一句话：**用一份 JSON 模板声明帧布局，在运行时编译成段表，再把字节流解成 `channel → (t, v)`。不生成代码，不重编译。**
>
> 本文件是 `ng/libs/psproto`（现 `ng/libs/protocol`）的完善方案，含**表达力统计**、schema、流式解析器、编译器与代码生成路线。

| 项 | 值 |
|---|---|
| 状态 | 方案待拍板（2026-11-19） |
| 现状代码 | 2462 行：`ProtocolDocument`(839) / `ProtocolSpec`(317) / `SequentialCodec`(1053) |
| 硬约束 | 纯逻辑、无 QWidget、无 Dataset、无宿主；headless 可单测 |

---

## 1. 现状盘点：现在能描述什么

### 1.1 已支持

| 维度 | 能力 |
|---|---|
| 布局 | `layout: "sequential"`（顺序帧） |
| 字节序 | 文档级 `endian`，段级可覆盖（`bigEndian`） |
| 类型 | `u8..i64`、`f32/f64`、`bool`、`bcd`、`str/string/ascii/char` |
| 段属性 | `name`、`role`（sync/length/payload/checksum/data）、`unit`、`description`、`prefix`、`scale`、`offset`、`hidden`、`plot`、`value`（期望值）、`size`（常量或**表达式**） |
| 变长 | `size` 表达式可引用前序段（含 `counts`）→ 变长数组 |
| 变体 | `series.selector` + `variants[]`，按选择器值切不同段布局；`fallback: reject/raw` |
| 校验 | 谓词 `eq/ne/lt/le/gt/ge/in/range/mask`；序列校验 `crc8/16/32`、`sum8/16`、`xor8`、`parity`，带 `over` 范围；CRC 可自定义 `poly/init/xorout/refin/refout` |
| 表达式 | `+ - * / % & \| ^ << >>`、`abs/min/max/clamp`、字段引用（可带下标） |
| 时间戳 | `hasTimestamp/timestampExpr/timestampUnit(s/ms/us)/origin(first_frame)/raw/fallback` |
| 重同步 | 解帧失败时前进 1 字节重试（隐式扫描） |

### 1.2 明确不支持（关键短板）

1. **按分隔符读**：没有“读到 `\r\n` / `,` / `*` 为止”的能力 → ASCII 行协议几乎无法描述；
2. **文本数字**：`str` 段只能当字符串显示，不能 `"12.34" → 12.34` 进通道；
3. **字符串通道**：`str` 段不产通道（只用于预览与选择器）；
4. **位域**：段只能整字节对齐，没有 `bit_offset/bit_width`；
5. **派生通道**：`valueExpr` 只做校验，`hasValue` 的段反而**被排除出通道**；`scale/offset` 只能线性变换，非线性公式算不出通道；
6. **通用 CRC**：宽度被算子名写死（8/16/32），无 `crc24`（RTCM）、无 `crc64`；
7. **嵌套 / 重复组**：无 TLV、无子结构、无“按块长度跳读”；
8. **全流一次喂入**：`decodeSequential(bytes, doc)` 要求整块字节，流式分片要上层自己缓冲；无 `feedByte` / `feedChunk`。

---

## 2. 表达力统计（样本：24 个常见格式）

> 判定口径：**可** = 现有 schema 直接能写；**需补** = 补本文 §4 的能力后能写；**不可** = 属于容器/嵌套，不该塞进帧模板。

### 2.1 二进制（16 个）

| # | 格式 | 特征 | 判定 | 缺什么 |
|---|---|---|---|---|
| 1 | 自定义飞控帧（sync+len+id+crc） | 定长头+变长体+CRC16 | **可** | — |
| 2 | u-blox UBX | `B5 62` + class/id + len + payload + CK_A/B | **可** | — |
| 3 | MSP / 私有遥测 | 头+len+payload+校验 | **可** | — |
| 4 | Modbus RTU | 地址+功能码+len+数据+CRC16 | **可** | — |
| 5 | HID/传感器寄存器流 | 定长记录+寄存器地址变体 | **可** | — |
| 6 | MAVLink v2 | 10 字节头 + 24bit msgid + 变长 payload + CRC16 + CRC_EXTRA | 可（受限） | 需要外部常量表做 CRC_EXTRA |
| 7 | RTCM3 | `D3` + 10bit len + 12bit msg + CRC24Q | 需补 | 通用 CRC（24 位） |
| 8 | ARINC 429 | 32 位字：label/SDI/data/SSM/parity | 需补 | 位域或派生通道（`(v>>…)&…`） |
| 9 | CAN 2.0 / DBC 信号 | 8 字节按位切信号 | 需补 | 位域 |
| 10 | J1939 / NMEA 2000 | 29 位 id + 8 字节位域 | 需补 | 位域 + 长帧重组 |
| 11 | ADS-B / Mode S | 位域 + 跨字段推导 | 需补 | 位域 + 派生通道 |
| 12 | 1553B | 命令字/数据字位域 | 需补 | 位域 |
| 13 | SpaceWire RMAP | 命令+地址+数据，带嵌套 | 不可 | 嵌套 |
| 14 | PCAP | 文件容器 | 不可 | 容器 |
| 15 | MDF4 / BLF | 块嵌套容器 | 不可 | 容器 |
| 16 | PNG / MP4 | 分块嵌套 | 不可 | 容器 |

**小计：现成可写 5/16（31%）；补齐后 11/16（69%）；结构性不可 5 个。**

### 2.2 ASCII（8 个）

| # | 格式 | 特征 | 判定 | 缺什么 |
|---|---|---|---|---|
| 17 | NMEA 0183 | `$…*hh\r\n`，逗号字段 | 需补 | 读终止符 + 文本数字 |
| 18 | AIS（NMEA 封装） | 同 17 + 6bit 编码 payload | 需补 | 同上 + 位域 |
| 19 | SBS-1 | 逗号分隔定字段 | 需补 | 读终止符 + 文本数字 |
| 20 | CSV | 行+列分隔+表头 | 需补 | 分隔符切分 + 文本数字 |
| 21 | OEM7 ASCII 头 | `#HEADER,...;` | 需补 | 读终止符 + 文本数字 |
| 22 | 自定义 `K:12.3,P:4.5\r\n` | 键值行 | 需补 | 同上 |
| 23 | AT 命令 / 应答 | 行 + 前缀 + 可选字段 | 需补 | 同上 |
| 24 | JSON Lines | 结构化嵌套 | 不可 | 不该用帧模板（交给专门解析器） |

**小计：现成可写 0/8；补齐后 7/8（87%）。**

### 2.3 结论

- **总计约 40% 现成**（全部集中在二进制定长/变长帧）；
- **补齐 5 项能力后约 75%**（ASCII 从 0 拉到 87%）；
- 剩下 ~25% 是**容器格式与嵌套结构**（PCAP/MDF4/PNG/JSON），应当明确交给专用解析器，**不扩张本库去覆盖**——那会把一个干净的帧模板语言变成半吊子通用格式引擎。

**优先级由此确定：ASCII 是最大短板，位域是二进制第二大短板。**

---

## 3. 目标

1. 一份**可校验的 schema** + 一份**能照着写模板的 README**；
2. **模板编译一次 → 段表**，解码期零 JSON 遍历（学 binary-parser 的“运行时生成紧凑解析器”）；
3. **流式解析器**：`feedByte` / `feedChunk` / `finish`，内部残余缓冲；
4. 补齐 §2 暴露的 5 项表达力缺口；
5. 预留**代码生成**（C++ → Python → Java），生成物自包含、不依赖本库运行时。

---

## 4. 设计

### 4.1 模板 schema（两层校验）

**产物**：`ng/libs/psproto/schema/psproto.schema.json`（JSON Schema draft-07）+ C++ 内建校验器。

为什么两层：
- **JSON Schema** 给编辑器/CI/外部工具用（VSCode `$schema` 直接提示、自动补全、拼写检查）；
- **C++ `validate()`** 给运行时用（不引入 JSON Schema 依赖，保持 headless 纯逻辑），负责语义校验：段名唯一、role 唯一、表达式引用存在、size 表达式无环、变体 key 合法性。

schema 顶层结构：

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "psproto 帧模板",
  "type": "object",
  "required": ["version", "name", "layout", "segments"],
  "properties": {
    "version":  { "type": "string" },
    "name":     { "type": "string" },
    "description": { "type": "string" },
    "layout":   { "enum": ["sequential"] },
    "endian":   { "enum": ["little", "big"] },
    "maxsize":  { "type": "integer", "minimum": 1, "maximum": 1048576 },
    "segments": { "$ref": "#/definitions/segments" },
    "series":   { "$ref": "#/definitions/series" },
    "timestamp":{ "$ref": "#/definitions/timestamp" }
  }
}
```

段（v2 扩充项标 ➕）：

```json
{
  "name": "roll",
  "role": "data",
  "type": "i16",
  "size": 1,
  "bit": { "offset": 0, "width": 16 },     // ➕ 位域（可选，缺省整字节对齐）
  "until": "\r\n",                          // ➕ 读到终止符（与 size 互斥）
  "scale": 0.01, "offset": 0.0, "unit": "rad",
  "derive": "temp_c * 1.8 + 32",            // ➕ 派生成通道（可引用前序段）
  "checks": [ { "op": "crc16", "over": ["body:0-10"] } ],
  "plot": true, "hidden": false
}
```

`type` 增加 ➕ `text_number`（把 ASCII 文本按十进制/十六进制解析成数值）。

### 4.2 README（用户视角）

`ng/libs/psproto/README.md` 结构固定为：

1. **30 秒上手**：最小二进制模板 + 最小 ASCII 模板，各配一行命令/一段 C++ 调用；
2. **模板速查表**：字段 × 类型 × 是否必填 × 默认值 × 示例（一张表覆盖全部键）；
3. **六种典型场景配方**：定长帧、变长数组、按 id 变体、ASCII 行、位域（CAN 风格）、时间戳换算；
4. **校验与调试**：`validate` 报错码表、`previewSingleFrame` 怎么看、常见坑（字节序、长度是否含头、CRC 覆盖范围）；
5. **从老 `.psproto` 迁移**：`frame_size/sync/fields` 旧方言 → 新 `segments` 的对照表。

### 4.3 编译成段表（核心性能改造）

现状：`decodeSequentialJson` 每帧都重新解析 JSON、重建段结构、重复求 size 表达式。

改造为两段式：

```cpp
struct Program;                                  // 不透明编译产物
bool compile(const QJsonObject &root, Program &out, std::string &err);   // 一次

// 编译后段表（内部结构，外部不依赖）
struct RtSeg {
    SegReader read;        // 具体读取器：u8/i16/f32/bcd/str/until/bitfield
    SizeFn size;           // 常量，或预编译表达式字节码
    std::vector<CheckFn> checks;
    uint32_t channelBase;  // 通道下标基址
    bool emit;
};
struct Program {
    std::vector<RtSeg> segments;             // 扁平段表
    std::vector<VariantTable> variants;      // 选择器 → 段表切片
    size_t fixedSize;                        // 全定长时可跳过长度计算
    // 表达式编译成字节码（栈式），不再走 AST 递归
};
```

验收口径：同一份 12 秒 ULog 级数据（3000 帧 × 16 段），改造后解码耗时至少降到原来的 **1/3**（现场测，不臆测）。

### 4.4 流式解析器

```cpp
class StreamParser
{
public:
    explicit StreamParser(const Program &program);

    // 两种输入粒度，同一状态机：
    bool feedByte(uint8_t b);                    // 单字节（便利接口）
    bool feedChunk(const uint8_t *data, size_t n);// 块（推荐主路径）
    bool finish();                               // 冲刷尾帧

    void setSink(FrameSink sink);                // 每解出一帧回调
    void reset();                                // 换文件复用
private:
    std::vector<uint8_t> m_residual;             // 半帧残余（唯一常驻缓冲）
};
```

纪律：
- **单字节不是主路径**：`feedByte` 内部并入残余缓冲后走块逻辑，避免逐字节虚调用；
- 常驻内存 = 残余缓冲 + 一帧，与文件大小无关；
- 解帧失败前进 1 字节重同步，与现状一致；
- 输出走 sink 回调，不攒整份结果（`decodeSequential` 保留为“整块便捷入口”，内部用 `StreamParser` 实现，行为不回归）。

### 4.5 补齐表达力（按优先级）

| 优先级 | 能力 | 设计 | 覆盖收益 |
|---|---|---|---|
| P0 | **读终止符** `until` | 段声明终止字节序列，读到即切帧；与 `size` 互斥；`finish` 时无终止符则丢弃或按 `fallback` 处理 | ASCII 行协议全部 |
| P0 | **文本数字** `text_number` | 读到的 ASCII 片段按 `radix`（10/16）转 double；支持前导空格、`+/-`、科学计数 | CSV/NMEA/OEM7 |
| P0 | **派生通道** `derive` | 表达式产通道（引用前序段、可用位运算）；取代 `valueExpr` 只校验的局限 | ARINC/ADS-B/CAN 手工解 |
| P0 | **位域** `bit.*` | 见 §4.8：段可声明位偏移/位宽/位序，跨字节不对齐 | CAN/J1939/1553/ARINC/DBC |
| P1 | **参数化 CRC** `op:"crc"` | 宽度显式（8/16/24/32/64）+ `poly/init/refin/refout/xorout` 全显式，`alg:"crc24q"` 可指向内含算法表；旧算子保留为别名 | RTCM3/任意标准 CRC |
| P1 | **帧脚本（AST 逃生门）** | 见 §4.7：校验/派生/选择器三处都能写一小段代码 | 非标校验、非线性换算、协议内自写 CRC |
| P2 | **字符串通道** | `str` 段可选产“枚举化数值通道”（值 → 枚举表下标） | 状态字、模式字段 |
| P2 | 嵌套/重复组 | **不做**，明确留给专用解析器 | — |

### 4.6 代码生成（S5，预留）

从 `Program` 直接生成自包含源码，**不依赖本库运行时**：

```
psproto-codegen --template foo.psproto --lang cpp   > foo_parser.h
psproto-codegen --template foo.psproto --lang python > foo_parser.py
psproto-codegen --template foo.psproto --lang java   > FooParser.java
```

生成物形态（C++）：

```cpp
namespace foo {
struct Frame { int64_t t_us; double roll; double pitch; };
class Parser {
public:
    void feed(const uint8_t*, size_t);
    std::function<void(const Frame&)> onFrame;
};
}
```

验收：**同一份模板、同一份字节，生成的 C++ 解析器与解释器输出逐字段一致**（golden 测试）。先 C++（与宿主同语言、可 embed），再 Python（脚本/分析），最后 Java。

---

## 4.7 帧脚本（pscode）：在协议里自己写代码

> 需求原话：「CRC 支持在协议里面自己写代码，一套 AST 语法」。
> 这条要求不是给 CRC 加个参数，而是给模板语言开一个**表达能力逃生门**。

### 4.7.1 先看清一个硬事实

现有表达式引擎是 **double 语义**：位运算前 `static_cast<int64_t>` 再转回 double。这意味着：

- `0xFFFFFFFFFFFFFFFF` 存不进 double（53 位尾数）→ u64 CRC 状态直接失真；
- `c << 1` 连左移 11 次就开始丢低位；
- 没有任何类型、没有变量、没有循环。

**结论：现有表达式写不出 CRC。** 要支持"协议里自己写代码"，必须先给引擎一套**类型化 AST**，这是新增能力，不是改写现有算子。

### 4.7.2 能力边界：脚本是逃生门，不是主路径

| 该用内置算子 | 该用脚本 |
|---|---|
| 标准 CRC（含 24 位）、sum/xor/parity、范围谓词 | 非标校验（协议自带算法、异或累积、双校验、哈希） |
| 线性 `scale/offset` | 非线性换算（多项式温度、分段查表、单位反算） |
| 单字段选变体 | 多字段联合决策的变体选择 |

**红线**：脚本**不允许**替代整份帧布局描述。一旦允许"用脚本描述整帧"，这个库就退化成半吊子通用引擎，与 §7 红线冲突。脚本只能挂在**三个点**上：校验、派生通道、选择器。

### 4.7.3 值语义（必须钉死，否则 codegen 不可等价）

- 类型：`u8/u16/u24/u32/u64`（定宽无符号，运算结果**按类型宽度自动截断**，语义 = C 无符号整数）+ `f64`（仅派生通道用）+ `bool`
- **不提供隐式 double↔整数转换**：混算报编译错，逼作者显式 `toF64()` / `toU32()`
- 除零、越界访问 = 运行时错误，整帧判定失败（可重同步）
- 无全局状态、无 I/O、无堆分配

这套语义的好处：**C++ 里 `uint32_t` 直接对应，Java 用 `long` + mask 对应，Python 显式 mask 对应**——三语言 codegen 能逐位一致。

### 4.7.4 语法（最小集，够写 CRC 与换算）

```text
// 例：RTCM3 的 CRC24Q，协议里自写
let c: u24 = 0;
for (i = 0; i < len(body); i++) {
    c ^= toU24(body[i]) << 16;
    for (k = 0; k < 8; k++) {
        c = c << 1;
        if (c & 0x1000000) c ^= 0x1864CFB;
    }
}
return c == crc24;
```

语句：`let` / 赋值（含 `+= -= &= |= ^= <<= >>=`）/ `if-else` / `for`（有界）/ `break` / `continue` / `return`
表达式：`+ - * / % & | ^ << >> ~`、比较、`&& || !`、三目
**禁止 `while` / `goto` / 递归函数** → 循环次数上界可静态证明（`maxsize × 8`），保证 O(n) 不挂死

内建函数（首批）：

| 函数 | 说明 |
|---|---|
| `len(seg)` | 段的元素/字节数 |
| `seg[i]` | 段内第 i 个字节（u8） |
| `mask(v, w)` / `toUN(v)` | 取低 w 位 / 转定宽 |
| `crcParam(data, w, poly, init, refin, refout, xorout)` | **参数化 CRC**，脚本里也能调，不必手写循环 |
| `abs/min/max/clamp/floor/ceil/round/pow` | 数学 |
| `frame.offset / frame.size` | 帧在流内位置与总长 |

### 4.7.5 三个挂点（同一套 AST，三处复用）

```jsonc
// 1) 校验：返回 bool
"checks": [{ "op": "script", "code": "…", "expect": true }]

// 2) 派生通道：返回数值/布尔，直接进数据集
{ "name": "temp_c", "derive": { "code": "…", "unit": "C" } }

// 3) 变体选择：返回用于匹配 variant 的键
"series": { "selector": "…", "selectorScript": "…" }
```

`schema` 只需把一个字段声明为 `string`（源码字符串），编译期转 AST 再转字节码——**不用 JSON 嵌套 AST**，可读性与 diff 友好性都更好。

### 4.7.6 校验与调试（脚本最大的风险点）

- **编译期**：未定义变量、类型不匹配、循环无界、访问不存在的段、返回值类型不符 → 带 `行:列` 报错；
- **运行期**：错误信息带行号 + 变量快照；
- **编辑器**：单帧试算面板增加"脚本变量"一栏，逐语句可看中间值（这是让用户敢写脚本的前提；
  没有这个，脚本就是黑盒，写错了只能靠猜）；
- **常量折叠**：纯常量脚本在编译期直接求值，避免每帧重算。

### 4.7.7 与 codegen 的关系（关键约束）

脚本必须能被**编译进生成代码**，否则 codegen 产物就得依赖 psproto 运行时，违背"自包含"。因此：

- 脚本 AST → 三语言 emitter（C++ 内联函数 / Java 方法 / Python 函数）；
- 语义已钉死（定宽无符号 + 显式 mask），三语言可逐位一致；
- **脚本不能有库依赖**，所以内建函数表必须小而固定，不做"插件函数"。

### 4.7.8 性能与取舍

| 路径 | 相对成本 | 用途 |
|---|---|---|
| 内置 `crc` 算子（参数化 + 预生成表） | 1×（基准） | 标准 CRC 的默认选择 |
| 脚本走字节码解释器 | 约 5–10× | 非标场景兜底 |
| 脚本常量折叠后 | ≈1× | 纯常量校验 |

所以：**内置算子优先，脚本兜底**；文档里明确"能用算子就别写脚本"，避免用户把模板写成代码。

### 4.7.9 工作量

| 模块 | 估计 |
|---|---|
| 词法 + 语法 + AST | ~350 行 |
| 语义检查 + 常量折叠 | ~250 行 |
| AST → 字节码 | ~250 行 |
| 字节码解释器（栈式） | ~300 行 |
| C++ emitter | ~250 行 |
| Python / Java emitter | 各 ~250 行 |

合计约 1900 行，**必须拆两批**：引擎（解释器跑通）与 emitter（三语言等价）分开验收。

---

## 4.8 位域：按位切信号

> 需求原话：「没有位域，这个要支持」。已从 P1 升为 **P0**。

### 4.8.1 为什么必须内置，不接受“靠表达式拼”

现状 `readElem()` 全部按整字节对齐读，`size` 最小粒度是 1 字节。用 `derive` 里 `(v >> 3) & 0x1F` 拼确实**能**凑出结果，但有四个代价：

1. **可读性崩坏**：DBC 一帧 8 字节带 30 个信号，就是 30 行魔法数偏移，改一个信号要重算一片；
2. **无法自动校验越界**：手算 shift 写错不报错，静默出垃圾数据；
3. **codegen 无法优化**：表达式是通用路径，提取不出“一次加载 + 一次移位”；
4. **DBC 无法直接导入**：`.dbc` 的 start bit/length/signed/scale 本来就与位域语义一一对应，绕道表达式就白丢了这张映射表。

内置后：模板里一行一个信号，校验器能查越界，编译期能生成最优提取器，DBC 可 1:1 映射。

### 4.8.2 数据模型：扁平位域段 + 容器段（不引入嵌套）

沿用现有扁平段表，位域段用 `bit` 指向一个**容器段**，不新增嵌套结构：

```jsonc
// 容器：CAN 报文 8 字节 payload，自身不产通道
{ "name": "payload", "type": "u8", "size": 8, "hidden": true }

// 位域信号：相对 payload 首字节起算
{ "name": "engine_rpm", "type": "u16",
  "bit": { "base": "payload", "offset": 8, "width": 16, "order": "msb0" },
  "scale": 0.25, "unit": "rpm" }
```

- `base`：缺省 `"frame"`（相对帧首字节）；可填段名（相对该段首字节）；
- `offset`：**位**偏移（不是字节），从 `base` 首字节起算；
- `width`：位宽 1–64；
- `order`：`lsb0`（默认）/ `msb0`，见 §4.8.3；
- 位域段**不再使用 `size`**，占用宽度由 `width` 决定；`type` 只用来定符号（`u16`/`i16`/`u32`…）；
- 位域段不得为 `role: sync/length/payload/checksum`（这些必须是字节粒度）。

这样做的理由：段表保持扁平，编译期不需要建树，`decode` 循环不用递归——与现有架构完全兼容。

### 4.8.3 位编号：三个维度必须分开（这是最容易翻车的地方）

业界的坑在于把三件事混成一个“位偏移”：

| 维度 | 含义 | 本库字段 |
|---|---|---|
| 字节序 | 多字节整数怎么拼 | `endian`（文档级）/ 段级覆盖 |
| **位序** | 位索引从 LSB 还是 MSB 起数 | `bit.order` |
| 位偏移 | 从容器首字节起算多少位 | `bit.offset` |

两种位序的精确定义：

- **`lsb0`**：位索引 `i` → 第 `i/8` 字节的第 `i%8` 位（bit0 在最右）。索引递增 = 向高位走，跨字节连续。
- **`msb0`**：位索引 `i` → 第 `i/8` 字节的第 `7 - (i%8)` 位（bit7 在最左）。索引递增 = 向低位走，跨字节连续。

### 4.8.4 与主流协议位编号的等价对照（已核对）

| 协议 / 工具 | 是否等价 | 对应配置 |
|---|---|---|
| DBC **Intel**（little-endian）信号 | 等价 | `order:"lsb0"`，`offset = start bit` |
| DBC **Motorola**（big-endian）信号 | 等价 | `order:"msb0"`，`offset = 字节*8 + (7 - startbit%8)` |
| J1939 / NMEA 2000 | 等价 | 同 Motorola，多帧需上层重组 |
| u-blox UBX 位标志字 | 等价 | `lsb0` + `width:1` 取单个 flag |
| ARINC 429 32 位字 | 等价 | 容器 `u32`（`order:"msb0"`），label/data/SSM 各一个位域 |
| 1553B 命令字 | 等价 | 同 ARINC，`msb0` 分字段 |

Motorola 等价性推导（写进文档防止后人改坏）：DBC start bit `s` 位于字节 `b=s/8`、位 `k=s%8`（MSB 优先编号），换算到 `msb0` 索引为 `b*8 + (7-k)`；信号向低位延伸时索引递增，`k=0` 之后正好落到下一字节 bit7，索引连续。**故 `msb0` 模型与 DBC Motorola 逐位等价。**

### 4.8.5 提取算法（编译期编成一个四元组）

编译期把 `(base, offset, width, order)` 折成：

```cpp
struct BitReader {
    uint32_t byteOff;   // 起始字节（相对 base）
    uint8_t  loadBytes; // 1..8
    uint8_t  shift;
    uint64_t mask;
    bool     msb0;
};
```

解码期：

```cpp
uint64_t raw = readUInt(p + byteOff, loadBytes, msb0 /* 拼装按 MSB 优先 */);
raw = (raw >> shift) & mask;
if (signed && width < 64) { raw = (uint64_t)((int64_t)(raw << (64-width)) >> (64-width)); }
phys = raw * scale + offset;
```

- `lsb0`：`loadBytes = ceil((offset%8 + width)/8)`，`shift = offset%8`；
- `msb0`：`shift = loadBytes*8 - (offset%8) - width`。

**约束（务实取舍）**：`width + (offset % 8) ≤ 64`。这条保证了跨字节时 `loadBytes ≤ 8`，一次 `readUInt` 就能取完，不需要 `__int128`（MSVC 没有）也不需要第二次读取。违背此约束时校验器直接报错，不做静默降级——位域越界静默降级是最坏的设计。

成本：一次 ≤8 字节 `readUInt`（常量长度，编译期可展开）+ 一次移位 + 一次 mask。比“每帧跑表达式”快，比逐位循环快得多。

### 4.8.6 与现有能力的关系

| 组合 | 行为 |
|---|---|
| 位域 + `scale/offset` | 支持（CAN 物理值 = raw*scale + offset，DBC 同语义） |
| 位域 + 有符号 | 支持，按 `width` 做符号扩展（不是按 `type` 宽度） |
| 位域 + `checks` | 支持（eq/range/mask 等，按物理值判） |
| 位域 + `derive` / 脚本 | 支持，位域值可被脚本引用 |
| 位域 + 变体 `series` | 支持（变体整体替换段表，offset 语义不变） |
| 位域 + `until`（ASCII） | **互斥**，校验报错 |
| 位域 + `counts` 数组 | **互斥**（数组请用字节粒度段 + `size` 表达式） |
| 位域 + `role` | 互斥（sync/length/payload/checksum 必须字节粒度） |

### 4.8.7 验收（硬标准，不接受“看起来对”）

1. 用 `cantools` 从一份真实 `.dbc` 生成期望解码结果，抽 **32 个信号**（含 Intel/Motorola 混合、非对齐、有符号、跨字节），与本库输出**逐位比对全部相等**；
2. 一份 8 字节 CAN 报文模板能解出 30+ 信号，模板内无一处手写 `>>`/`&`；
3. 越界配置（`width + offset%8 > 64`、`base` 不存在、位域与 `until` 同时出现）全部被 `validate` 拒绝，报错带段名与具体位范围；
4. ARINC 429 与 1553B 各一份模板可解，作为“非 CAN 场景”的回归。

---

## 5. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
| **S1** | JSON Schema + README + 现状模板迁移（`demo*.psproto` 全部通过新校验） | schema 可被 VSCode 识别；7 个 fixture 全部 `validate` 通过；README 能照着写出新模板 |
| **S2** | `compile()` → 段表 + 表达式字节码；`decode` 改走段表 | 行为与现状逐字段一致（回归测试）；3000 帧基准耗时 ≤ 1/3 |
| **S3** | `StreamParser`（feedByte/feedChunk/finish/setSink/reset） | 单字节逐喂 与 整块喂 结果一致；跨块半帧正确；尾帧冲刷正确 |
| **S4a** | 表达力补齐：`until` / `text_number` / `derive` / **位域（§4.8）** / 参数化 CRC（含 24 位） | NMEA、CSV 可解；**CAN DBC 32 信号与 cantools 逐位一致**；ARINC429/1553B 可解；RTCM3 可解；老模板不回归 |
| **S4b** | **pscode 引擎**：类型化标量 + AST + 语义检查 + 字节码解释器 + 编辑器变量面板 | 用脚本写出 CRC24Q 且与内置算子结果**逐位一致**；循环上界可静态证明；错误带行列号 |
| **S4c** | **pscode emitter**：C++ → Python → Java | 三语言生成物对同一输入逐位一致（golden）；生成物零依赖 |
| **S5** | 模板 codegen（帧结构 → 解析器源码） | 生成的 C++ 解析器与解释器逐字段一致 |

---

## 6. 需要拍板的五个点

1. **ASCII 支持范围**：做到“分隔符切分 + 文本数字 + 键值对”（覆盖 NMEA/CSV/行协议，工作量中等），还是只做“定长 ASCII 字段”（工作量小，但 ASCII 覆盖仍然接近 0）？
   建议：**做全**。不做全，这个库就只能描述二进制，而你的需求明确要求 ASCII。
2. **位域实现方式**：内置 `bit.offset/width`（schema 变复杂，但 CAN/J1939/1553 直接能写），还是只靠 `derive` 里的 `>>`/`&` 手工拼（schema 不变，但模板可读性差、每个信号写一行表达式）？
   建议：**内置**。CAN 是工业界最常见的“按位切信号”，靠表达式拼会把模板写成代码。
3. **CRC 的定位**：参数化算子为主、脚本兜底（推荐），还是全部走脚本（模板里每个 CRC 都手写循环）？
   建议：**算子为主、脚本兜底**。标准 CRC 手写循环既慢又易错，且无法 codegen 成最优实现。
4. **脚本的类型语义**：定宽无符号（`u8..u64`，按宽截断，= C 语义）还是统一 double（改动小但 CRC 写不出）？
   建议：**定宽无符号**。这是脚本能正确表达 CRC、且能被三语言等价生成的前提。
5. **旧 API 兼容**：保留 `decodeSequential(bytes, doc)` 作为整块便捷入口（内部走 StreamParser），还是直接切到新接口、老调用点全改？
   建议：**保留**，行为不变，降低上层迁移风险。

另外：**codegen（S5）是否本批就做**，还是先做到 S4c 再单独一批？
建议：**分两批**。三语言 golden 验收成本高，和表达力补齐混排会拖慢主线。

---

## 7. 红线

- 不引入 JSON Schema 运行时依赖（schema 文件给工具用，C++ 用自带校验）；
- 不生成代码也要能解析（解释器是主线，codegen 是附加物）；
- 解码期零 JSON 遍历、零 AST 递归（编译成段表 + 字节码）；
- 常驻内存与文件大小无关（只有残余缓冲 + 一帧）；
- 容器格式与嵌套 TLV **明确不覆盖**，不为“装得像通用引擎”牺牲简洁；
- `str` 不产数值通道时不得伪造数值，避免污染数据集；
- **脚本只能挂在校验/派生/选择器三处，不得用来描述帧布局**；脚本不得含 `while`、递归、I/O、堆分配。

## 8. 已定 / 未定

| 项 | 状态 |
|---|---|
| 位域内置（`bit.base/offset/width/order`） | **已定**（§4.8） |
| 位序 `lsb0`/`msb0` 双模型，对齐 DBC Intel/Motorola | **已定**（§4.8.4 有等价性推导） |
| 位域约束 `width + offset%8 ≤ 64`（避免 `__int128`） | **已定**（§4.8.5） |
| 脚本引擎类型语义（定宽无符号） | 待拍板 |
| CRC 定位（算子为主 / 全脚本） | 待拍板 |
| ASCII 深度（全做 / 只定长） | 待拍板 |
| 旧 API 是否保留 | 待拍板 |

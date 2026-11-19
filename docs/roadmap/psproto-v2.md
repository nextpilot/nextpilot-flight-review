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
| P1 | **位域** `bit.offset/width` | 段可声明位偏移与位宽（跨字节），独立于字节对齐 | CAN/J1939/1553 |
| P1 | **通用 CRC** `op:"crc", width:24` | 宽度显式，取代 `crc8/16/32` 三选一；保留旧算子为别名 | RTCM3/自定义 |
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

## 5. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
| **S1** | JSON Schema + README + 现状模板迁移（`demo*.psproto` 全部通过新校验） | schema 可被 VSCode 识别；7 个 fixture 全部 `validate` 通过；README 能照着写出新模板 |
| **S2** | `compile()` → 段表 + 表达式字节码；`decode` 改走段表 | 行为与现状逐字段一致（回归测试）；3000 帧基准耗时 ≤ 1/3 |
| **S3** | `StreamParser`（feedByte/feedChunk/finish/setSink/reset） | 单字节逐喂 与 整块喂 结果一致；跨块半帧正确；尾帧冲刷正确 |
| **S4** | P0 三项（until / text_number / derive）+ P1 两项（位域 / 通用 CRC） | NMEA、CSV、CAN 风格的 3 份新模板可解；老模板不回归 |
| **S5** | codegen C++ → Python → Java | golden 一致；生成物零依赖 |

---

## 6. 需要拍板的四个点

1. **ASCII 支持范围**：做到“分隔符切分 + 文本数字 + 键值对”（覆盖 NMEA/CSV/行协议，工作量中等），还是只做“定长 ASCII 字段”（工作量小，但 ASCII 覆盖仍然接近 0）？
   建议：**做全**。不做全，这个库就只能描述二进制，而你的需求明确要求 ASCII。
2. **位域实现方式**：内置 `bit.offset/width`（schema 变复杂，但 CAN/J1939/1553 直接能写），还是只靠 `derive` 里的 `>>`/`&` 手工拼（schema 不变，但模板可读性差、每个信号写一行表达式）？
   建议：**内置**。CAN 是工业界最常见的“按位切信号”，靠表达式拼会把模板写成代码。
3. **旧 API 兼容**：保留 `decodeSequential(bytes, doc)` 作为整块便捷入口（内部走 StreamParser），还是直接切到新接口、老调用点全改？
   建议：**保留**，行为不变，降低上层迁移风险。
4. **codegen 是否本批就做**：先只做到 S4（解释器完善），codegen 单独一批；还是 S5 一起排期？
   建议：**分两批**。codegen 的验收成本高（三语言 golden），和表达力补齐混在一起会拖慢主线。

---

## 7. 红线

- 不引入 JSON Schema 运行时依赖（schema 文件给工具用，C++ 用自带校验）；
- 不生成代码也要能解析（解释器是主线，codegen 是附加物）；
- 解码期零 JSON 遍历、零 AST 递归（编译成段表 + 字节码）；
- 常驻内存与文件大小无关（只有残余缓冲 + 一帧）；
- 容器格式与嵌套 TLV **明确不覆盖**，不为“装得像通用引擎”牺牲简洁；
- `str` 不产数值通道时不得伪造数值，避免污染数据集。

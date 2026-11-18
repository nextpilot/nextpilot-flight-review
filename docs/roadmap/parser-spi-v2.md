# 解析器插件模型 v2：自描述接口 + 字符步进基类

| 项 | 值 |
|---|---|
| 状态 | 方案待开工（2026-11-18） |
| 现状 SPI | `ng/core/include/ps_parser_spi.h`（纯 C ABI）+ `ParserRegistry` |
| 目标 | 解析器继承统一接口类**自述元信息**（名称/范围/传输/参数），供打开对话框直接消费；具体解析器主要实现 `parseChar` 字符状态机，缓冲/残余/ABI 适配由基类承担 |
| 硬约束 | 解析器只认字节、不写存储、不认识宿主；headless 不链 Qt Widgets；外部插件 ABI 向后兼容 |

## 1. 现状与缺口

现有 C ABI：

- `PsParserInfo`：`id / display_name / extensions / can_sniff / streaming`；
- vtable：`create / destroy / sniff / feed(chunk) / finish / reset / info / capabilities`；
- 具体解析器（`PspxParser`）手写 vtable 闭包，自己管理跨 feed 的残余字节；`UlogStreamParser` 还是普通函数，尚未接 SPI。

缺口：

1. **自述信息不够**：没有业务领域/使用范围说明、没有支持的传输方式、没有可配置参数 schema；打开对话框无法从解析器拿到表单定义。
2. **每个解析器重复造轮子**：残余缓冲、分帧、ABI 适配、注册样板都要自己写；新格式上手成本高。
3. **`feed` 是整块喂入**：文本/流式协议（NMEA、MAVLink、自定义顺序帧）本来就是逐字符状态机，整块接口逼着实现者自己拼缓冲。

## 2. 目标（两条）

1. **继承一个接口类自述**：协议名称、使用范围（领域 + 说明）、支持的传输协议、可供配置的参数；这些信息直接喂给自定义打开对话框（见 [protocol-migration.md](protocol-migration.md) §5）。
2. **特定类主要实现 `parseChar`**：基类负责缓冲、残余字节、逐字节派发、sink 出口与 ABI 适配；子类只写字符级状态机和元信息。

## 3. 分层设计

### 3.1 描述接口 `IParserDescriptor`（headless C++）

```cpp
// 字节传输方式。一个解析器可支持多种（如 CAN：File 回放 blf/asc/mdf，Can 实时总线）。
enum class Transport {
    File,     // 文件回放（.ulg/.csv/.blf/…）
    Serial,   // 串口 / COM 口
    Udp,
    Tcp,
    Can,      // CAN 总线（socketcan / 设备通道）
    // 预留：WebSocket / Bluetooth / SharedMemory，需要时再加枚举值
};

class IParserDescriptor
{
public:
    virtual ~IParserDescriptor() = default;

    virtual QString id() const = 0;              // 全局唯一，如 "ulog" / "psproto"
    virtual QString displayName() const = 0;     // 对话框展示名
    virtual QString description() const = 0;     // 使用范围 / 适用场景说明
    virtual FormatDomain domain() const = 0;     // 业务领域（复用 FormatDomain）
    virtual QList<Transport> transports() const = 0;  // 支持多种；对话框据此显示对应配置行
    virtual QStringList extensions() const = 0;  // 带点小写后缀，可空
    virtual bool canSniff() const = 0;

    // 可配置参数（结构与 protocol-migration §5.2 的 ParamField 同一套，提到 core 共享）
    virtual QList<ParserParam> paramSchema() const = 0;
};
```

- `ParserParam`（原 `ParamField`）上提到 core，`IFormatDriver` 与解析器描述共用一份定义，避免两套字段模型。
- **单一真源（建议）**：元信息由解析器自述；`IFormatDriver` 委托给它对应的解析器，不在驱动侧再维护一份名称/后缀/参数。
- 描述全部是纯值结构，不含 QWidget、不含解析逻辑，可 headless 单测。

### 3.2 字符步进基类 `CharStreamParser`

```cpp
class CharStreamParser : public IParserDescriptor
{
public:
    // 基类实现 SPI vtable 适配：create/destroy/feed/finish/reset/sniff
    static const PsParserVtable *parserVtable();

protected:
    // 子类主入口：逐字符状态机。满帧时经基类 helper 吐批。
    virtual void parseChar(uint8_t b, ParserSink &sink, ParseError &err) = 0;

    // 可选钩子
    virtual void onFinish(ParserSink &sink, ParseError &err) {}
    virtual int  sniffHead(const uint8_t *data, uint32_t size) { return 0; }

    // 基类提供的辅助（子类不碰裸缓冲）
    //   need(n) / take(n) / skip(n) / emitBatch(...) / residual() ...
};
```

职责划分：

| 基类 `CharStreamParser` 负责 | 子类负责 |
|---|---|
| 残余缓冲维护、chunk 追加与逐字节 drain | `parseChar` 状态机（拼帧、校验、取值） |
| SPI vtable 适配、create/destroy/reset | 元信息 getter（id/name/domain/transports/schema） |
| sink 出口、错误透传、finish 冲刷钩子 | 满帧时调 `emitBatch` 填列 |
| 内存纪律：常驻 = 残余缓冲 + 一批 | 帧内字段解释、scale/offset、字节序 |

### 3.2.1 文本文件怎么进 parse

文本格式（CSV / NMEA / ASCII 日志）和二进制帧走**同一条字节流通路**，不在喂入上换机制；区别只在"一帧怎么算完整"：二进制按固定宽度/帧头帧尾，文本按**分隔符 / 行结束符**。

喂入链路：

```
FileByteSource 读一块(≤256KB) → orchestrator 调 parser.feed(chunk)
  → 基类追加到残余缓冲 → 逐字节 drain
      → 行式便捷层：累积到换行符 → 解码成 QString → onLine(...)
      → 或原始层：parseChar(b) 自己识别分隔符/帧头
  → 一行/一帧凑齐 → 字段切分 → emitBatch
finish()：冲刷残余里没结束的最后一行
```

基类为文本提供两个粒度，子类按协议选：

| 粒度 | 入口 | 适用 |
|---|---|---|
| 原始字节状态机 | `parseChar(uint8_t)` | 帧头帧尾混合、NMEA 那种 `$...\r\n` + 校验和的复杂文本协议 |
| 行式便捷层（建议提供 `LineStreamParser` 薄基类） | `onLine(const QString &line, sink, err)` | CSV、ASCII 日志等纯换行分隔；基类负责攒行、剥 `\r`、保留跨行半行 |

文本处理约定：

- **编码**：基类不做转码，按 UTF-8/ASCII 解释；行累积完再 `QString::fromUtf8`。非 UTF-8 编码留后续。
- **BOM**：首行开头的 UTF-8 BOM 由基类 helper 剥掉一次。
- **半行跨 chunk**：没遇到换行符的残留在缓冲里，下次 feed 接上，和二进制残余同一套机制。
- **字段切分**：行内分隔符（逗号/制表符/空格）是协议知识，由子类按自己的 `paramSchema`（如 CSV `delimiter`）切；基类不猜分隔符。
- **末行**：文件结尾没有换行符时，`finish()` 把残余作为最后一行交给 `onLine`。
- **批量逃生门对文本同样可用**：超大 CSV 可覆写 `feedChunk` 按行整块扫描，避免逐字符开销；默认行走行累积即可。

**批量逃生门（已拍板保留）**：定长二进制帧格式（如 ULog D 段）逐字节开销大。基类保留一个可选 `feedChunk(data, size, sink, err)` 覆写点；不覆写就走默认逐字节路径。不为了统一而牺牲高吞吐格式的效率。

两条路径的分工：
- `parseChar`：默认主路径，文本/流式/变长帧协议用；基类负责缓冲与逐字节派发。
- `feedChunk`：批量逃生门，定长二进制/高吞吐帧用；覆写后该类绕过逐字节派发，直接在块上解析，残余字节仍由基类缓冲管理。

### 3.3 边界：打开文件 / 串口 / 网络不属于解析器基类

"字节从哪来"由 core 的 `IByteSource` 层负责，解析器基类**不引入**任何打开/连接/设备句柄的接口：

| 层 | 职责 | 实现 |
|---|---|---|
| `IByteSource`（core，已有抽象） | 字节来源：打开/关闭/读/超时 | `FileByteSource`（已有）、`MemoryByteSource`；`SerialByteSource` / `UdpByteSource` / `TcpByteSource` / `CanByteSource` 待补（Q3） |
| `CharStreamParser`（本方案） | 字节解析：缓冲/状态机/吐批 | 只通过 `read()` 拿字节，不知道对面是文件还是网线 |
| `IngestOrchestrator` | 编排：按 transport 选源 → 喂解析器 → 收批入仓；录制 tee 挂这层 | 现有摄取管道 |

理由：

- 同一解析器要能跑多种来源（如 MAVLink 既回放 `.tlog` 也监听 UDP）；开设备写进解析器会让逻辑复制两份。
- 录制旁路只在字节源层做一次 tee；解析器自己开文件会让录制盖不住。
- 解析器可 headless 单测（喂 `MemoryByteSource`）；掺句柄/socket 后测试无法跑。

传输相关参数由对话框收集进 `params`，编排层据此构造对应 `IByteSource`；解析器不读这些参数。`transports()` 只做**能力声明**，不负责连接。各传输典型参数：

| Transport | 典型配置参数 |
|---|---|
| File | 文件路径 |
| Serial | COM 口/设备名、波特率、数据位/校验/停止位 |
| Udp / Tcp | 主机、端口、（UDP）绑定地址 |
| Can | 通道/接口名、比特率、（可选）DBC 路径 |

### 3.4 openFile 怎么知道读什么、喂给谁、怎么喂

整条链路分四段，openFile 自己**不掌握**格式细节：

```
对话框收集 {path, format?, 配置参数…}
  → Intent "data.load"
  → DataLoadHandler：选驱动（format 显式 > 后缀匹配 > 内容嗅探），驱动给出 parserId
  → IngestOrchestrator：
      1) 按 transport + params 构造字节源（FileByteSource；Serial/Udp/Tcp/Can 待补）
      2) pipeline.bindParser：注册表查 vtable → create 实例 → 挂 sink
      3) 【v2 补】configure(params)：把分隔符/波特率/模板路径等配置喂给实例
      4) 循环 source.readChunk(256KB) → pipeline.feed → vtable.feed(ctx, bytes, sink)
      5) finish 冲刷残余
  → 解析器内部 parseChar/feedChunk，满帧经 sink.frame_batch 入 Dataset
```

分工对照：

| 问题 | 谁回答 |
|---|---|
| 字节从哪个设备读、怎么读 | `IByteSource` 实现（按 transport 选）；编排层只调统一 `readChunk` |
| 字节交给哪个解析器 | 驱动 `parserId` + `ParserRegistry` 查 vtable |
| 字节怎么解成帧/批 | 解析器内部（`parseChar` / `feedChunk`） |
| 配置参数（分隔符/波特率/模板…） | 对话框收进 `params` → 编排透传 → 解析器实例 `configure` |

**当前缺口（v2 一并补）**：现有 vtable 没有配置入口，解析器拿不到 intent 里的参数。需要在实例生命周期 `create` 之后、`feed` 之前加一个 `configure(params)` 槽位（C++ 基类普通虚函数；C ABI 走可选尾部槽，传键值对）。解析器从这里取自己 schema 声明的参数，不从全局读。

### 3.5 与现有 C ABI 的关系

- **内置 C++ 解析器**：继承 `CharStreamParser`，基类导出 vtable；`NG_REGISTER_PARSER` 宏改为接收基类子类。
- **外部插件（.so/.dll）**：保留纯 C ABI；描述信息经 vtable 尾部可选槽位返回（描述结构或 JSON）。老插件不填新槽位时，回退到旧 `PsParserInfo` 字段，不破坏兼容。

### 3.4 对话框消费

`LoadDataDialog` 枚举解析器描述：

- 按 `domain` 分组展示；
- 按 `transports` 过滤：`File` 显示数据文件行，`Udp/Tcp/Serial` 显示地址/端口行（活源第二批）；
- 按 `paramSchema()` 动态生成参数表单（控件类型/默认值/范围/选项/条件显隐，见 protocol-migration §5.3）；
- `description` 作为格式说明文字展示。

## 4. 分批与验收

| 批次 | 内容 | 验收 |
|---|---|---|
| Q1 | core 抽出 `Transport / ParserParam / IParserDescriptor`；`CharStreamParser` 基类 + vtable 适配；`PspxParser` 迁移为参考实现 | 单测：逐字节喂入拼出整帧；残余跨 feed 保留；描述字段可查；ctest 通过 |
| Q2 | `ParserRegistry` 与对话框接描述信息（领域分组/传输过滤/schema 表单）；现有解析器逐个迁到基类 | 对话框按描述生成表单；ULog/CSV 迁移后行为不变 |
| Q3 | 插件 ABI 描述槽位；活源（udp/tcp/serial）接入通路 | 外部插件可声明描述；活源能建源 |

## 5. 与协议搬迁的关系

自定义协议解析器（protocol-migration L2）直接继承 `CharStreamParser`：

- 描述：`id="psproto"`、domain Autopilot/General、transport File、`paramSchema()` 里一个 `.psproto` 模板 File 字段；
- 解码：头部段 → 选择器命中变体 → 变体段定长/变长，状态机在 `parseChar` 内推进；
- 不再自己写残余缓冲与 vtable 样板。

## 6. 待拍板

1. **描述真源**：解析器自述、驱动委托（推荐），还是驱动侧另维护一份？
2. ~~**`parseChar` 范围**~~ **已定**：`parseChar` 为默认主路径，`feedChunk` 批量逃生门保留（见 §3.2）。
3. ~~**Transport 枚举**~~ **已定**：`File / Serial(COM) / Udp / Tcp / Can`；WebSocket/Bluetooth/SharedMemory 预留，需要时再加。解析器可声明支持其中多种。
4. **插件描述槽**：本批只服务内置 C++ 解析器，还是同步把描述回传槽位加进外部插件 ABI？

## 7. 红线

- 解析器不读文件、不写存储、不认识宿主；结果只经 sink 吐批。
- headless 接口不链 Qt Widgets；core 不加无关新概念。
- 内存纪律：常驻内存与文件大小无关；parseChar 内只攒当前帧 + 一批。
- 老 C ABI 向后兼容：既有插件不改编译不过。

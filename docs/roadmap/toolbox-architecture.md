# 扩展 toolbox 化与 autopilot 方案

> 状态：四领域工具箱已拆（`autopilot.core` / `gnss.core` / `automotive.core` / `robotics.core`），ULog/apbin/MAVLink/sim、ASC/BLF/MDF4、rosbag2/MCAP 源码已迁入对应工具箱；通用 loader 与 CSV/JSON/binary/rustcsv 在 `builtin/common`，CAN/DBC 在 `builtin/automotive/datasources/can`；`extensions/dynamic` 只保留 SDK；飞控默认曲线与分析报告第一版已接入（`autopilot.review.default` / `autopilot.review.report`），并通过统一的“分析”应用库按数据源格式动态呈现（当前数据源与全部应用两组），不新增领域 Ribbon 页。`modules/datasource` 的 `MessageParserRegistry`、`ProtocolCatalog`/`FrameDecoder`、GNSS 通用 `NmeaMessageParser`/`UbxMessageParser`/`Oem7MessageParser` 已落地；OEM7 二进制和 ASCII body 都能按 `oem7_database.json` 展开，`Oem7Source` 已移除 `BESTPOS/INSPVA/BESTATT/BESTVEL` 和 GGA 特判。`NmeaDataLoader`/`UbxDataLoader` 已改为读取 `nmea_database.json`/`ubx_database.json`；`NmeaMessageParser`/`UbxMessageParser` 也已按描述库解字段，代码不再分支 `GGA/RMC` 和 `NAV-PVT/NAV-POSLLH`。MAVLink C++11 生成物、`MavlinkMessageParser`、metadata sidecar、组件注册和测试已落地；`MavlinkSource` 已改为统一 parser 解码，旧 `switch (msgid)` 已删除；`data_loader_tlog` 与 `MavlinkChannelCollector` 已落地，`.tlog` 可从普通文件入口打开；`UlogMessageParser`/`ApbinMessageParser` 已注册，通用 `ProtocolInspectorDock` 已接入数据源模块的“协议检查”面板。ROS parser 未实施。
> 日期：2026-11-16

## 1. 目标

把 `src/extensions/builtin` 从“内置组件集合”升级为四个独立业务工具箱：

- `autopilot`：飞控日志、MAVLink 在线/回放调试、默认曲线、飞行分析报告。
- `gnss`：NMEA / UBX / OEM7、协议帧检查、定位质量、天空图、定位报告。
- `automotive`：CAN 日志、车辆信号、车辆分析报告。
- `robotics`：ROS 2 / rosbag2 / rosbag1 / MCAP、topic 与消息结构检查、TF 树、默认工作区、运行报告。

每个工具箱自包含数据源、分析算法、默认工作区、报告和测试；平台只提供 shell、datasource、plotting、settings、logging、command、dock、report 渲染等通用能力。协议调试面板和 MessageParser 注册由 `modules/datasource` 提供，GNSS、autopilot、robotics 共用。

`src/extensions/dynamic` 不再承载官方内建格式，留给第三方开发者和非核心插件。动态加载能力本身保留。

## 2. 目录基线

```text
src/application/modules/datasource/    # 既有模块，新增 parser 子目录
├── include/datasource/
│   ├── IMessageParser.h               # 协议解析接口
│   └── MessageParserRegistry.h        # 注册、查询、按协议枚举
├── parser/
│   ├── MessageParserRegistry.cpp
│   ├── MessageParserSpec.h            # 注册项：协议、优先级、后缀、sniff、工厂
│   ├── ProtocolCatalog.cpp            # 描述库索引
│   ├── FrameDecoder.cpp
│   ├── FieldValue.cpp
│   └── DecodedFrame.h
├── dock/
│   └── ProtocolInspectorDock.cpp
├── manager/                           # 既有 DataSourceRegistry / Session 等
├── dialog/                            # 既有
├── ribbon/                            # 既有
└── services/                          # 既有 cache / live

src/extensions/
├── sdk/                            # 第三方 C++ / Rust SDK
├── builtin/
│   ├── common/                     # 共享机械层，不属于任何业务域
│   │   ├── loader/                 # PsLoaderCommon、dataset 构建 helper
│   │   └── datasources/
│   │       ├── csv/                # CsvDataLoader
│   │       ├── json/               # JsonDataLoader
│   │       ├── binary/             # BinaryDataLoader
│   │       └── rustcsv/            # Rust cdylib 样板
│   ├── autopilot/
│   │   ├── AutopilotToolbox.cpp/.h
│   │   ├── include/autopilot/      # 对外公开接口
│   │   ├── protocols/
│   │   │   └── mavlink_database.json
│   │   ├── protocol/
│   │   │   └── parsers/
│   │   │       └── MavlinkMessageParser.cpp
│   │   ├── datasources/
│   │   │   ├── ulog/
│   │   │   ├── apbin/
│   │   │   ├── tlog/
│   │   │   ├── mavlink/
│   │   │   └── sim/
│   │   ├── analysis/               # profile / capability / role map / rules
│   │   ├── apps/                   # 默认工作区、分析报告
│   │   └── resources/
│   ├── gnss/
│   │   ├── GnssToolbox.cpp/.h
│   │   ├── include/gnss/
│   │   ├── protocols/              # OEM7 / UBX / NMEA 描述
│   │   ├── protocol/
│   │   │   └── parsers/
│   │   │       ├── Oem7MessageParser.cpp
│   │   │       ├── UbxMessageParser.cpp
│   │   │       └── NmeaMessageParser.cpp
│   │   ├── datasources/            # NMEA / UBX / OEM7
│   │   ├── analysis/
│   │   ├── apps/
│   │   └── resources/
│   ├── automotive/
│   │   ├── AutomotiveToolbox.cpp/.h
│   │   ├── include/automotive/
│   │   ├── datasources/            # ASC / BLF / MDF4 / CAN
│   │   ├── analysis/
│   │   ├── apps/
│   │   └── resources/
│   ├── robotics/
│   │   ├── RoboticsToolbox.cpp/.h
│   │   ├── include/robotics/
│   │   ├── protocols/              # ros2msg / ros1msg 标准消息、schema 映射
│   │   ├── protocol/
│   │   │   └── parsers/
│   │   │       ├── Ros2MessageParser.cpp
│   │   │       ├── Ros1MessageParser.cpp
│   │   │       └── McapMessageParser.cpp
│   │   ├── datasources/            # rosbag2 / rosbag1 / mcap / dds
│   │   ├── analysis/               # TF 树、capability、role map、规则
│   │   ├── apps/                   # 默认工作区、运行报告
│   │   └── resources/
│   └── datasources/                # 通用格式注册：JSON / binary
└── plugins/                        # 第三方、非核心，默认不构建
```

依赖方向：

```text
autopilot / gnss / automotive / robotics
        ├──> modules（datasource / plotting 公开头）
        └──> builtin/common（loader / CSV）

common -> platform
```

工具箱之间不互相 include 私有头；需要协作只走 `include/<domain>` 的公开接口。`common` 不依赖任何工具箱。`modules/datasource` 不依赖任何业务工具箱，只提供 parser 注册、描述索引、字段解码和调试面板。

## 3. 数据源归属

| 格式 | 归属 | 说明 |
|---|---|---|
| ULog | `autopilot/datasources/ulog` | PX4 日志，已有 parser 和测试 |
| apbin | `autopilot/datasources/apbin` | ArduPilot DataFlash，已有 parser 和测试 |
| tlog | `autopilot/datasources/tlog` | MAVLink 回放，补成 `data_loader` |
| MAVLink | `autopilot/datasources/mavlink` | 实时源和 tlog 共用描述驱动解码与 ProtocolInspector |
| sim | `autopilot/datasources/sim` | 飞控仿真源，用于开发和测试 |
| CSV | `builtin/common/datasources/csv` | 通用表格，不归飞控 |
| NMEA / UBX / OEM7 | `gnss/datasources` | 描述驱动，OEM7 用 EDIE `database.json`，禁止硬解码 |
| ASC / BLF / MDF4 | `automotive/datasources` | 已迁入，CAN/DBC 共享 `ps_can_common` |
| rosbag2（mcap / sqlite3） | `robotics/datasources/rosbag2` | topic + schema 驱动解码 |
| rosbag1（.bag） | `robotics/datasources/rosbag1` | 老版本 bag，复用 ros1msg 描述 |
| 独立 MCAP | `robotics/datasources/mcap` | 读通道 schema 记录，不假设是 ROS |
| DDS 在线源 | `robotics/datasources/dds` | 第一阶段只做发现 + 订阅，不做写回 |
| JSON / binary | `builtin/common/datasources` | 通用格式，非核心 |
| rustcsv | `builtin/common/datasources/rustcsv` | Rust 动态库样板，不参与核心注册 |

CSV 的特殊约束：

- loader 在 `common`，注册仍由通用数据源组件负责。
- `DataSourceRegistry::findFileBySuffix` 的 CSV 兜底继续保留，不随 `PS_BUILD_AUTOPILOT` 生效。
- `autopilot` 可以用 CSV 做 profile 映射，但不拥有 CSV parser。

## 4. autopilot 内部设计

### 4.1 数据源

- `ps_autopilot` 静态库包含 ULog、apbin、tlog、MAVLink、sim、analysis。
- `ps_autopilot_component` 只放 `AutopilotToolbox.cpp`，保证静态注册器进入宿主。
- `AutopilotToolbox::contribute()` 注册 `data_loader_ulog`、`data_loader_apbin`、`data_loader_tlog`、`data_source_mavlink`、`data_source_sim`。
- `CommonDataSourcesComponent` 删除 ULog、apbin、MAVLink、sim 的注册，保留 CSV、JSON、binary 等通用格式。
- 文件对话框过滤器改为从 `DataSourceRegistry` 生成，删除 `DataSourceSession.cpp`、`DataSourceCommands.cpp` 中的硬编码扩展名。
- `.tlog` 必须进入 `fileEntries`，不能继续落到 CSV 兜底。

### 4.2 能力模型

不做一个“所有飞控能力必须对齐”的必填 `FlightMeta`。运行时聚合体拆成三部分：

```text
FlightMeta
├── SourceFacts     格式、路径、时间基准、固件/机型提示、参数可用性
├── CapabilitySet   Attitude / Rates / Battery / GPS / RC / Actuators / EKF / Events / Params
└── RoleMap         语义角色 -> 通道候选
```

规则：

- `SourceFacts` 字段允许缺失，不承诺能力。
- `CapabilitySet` 按实际通道和消息推断，不按机型硬编码。
- `RoleMap` 允许一个角色有多个候选通道，按 profile 优先级选择。
- 某个能力缺失时，对应曲线和报告章节整体省略，不补默认值、不伪造数据。
- CSV 默认只有 `Generic` 能力；tlog 能力取决于实际收到的消息。

### 4.3 profile

- `Px4Profile`：ULog 为主，使用 ULog 参数和消息。
- `ArduPilotProfile`：apbin 为主，使用 DataFlash 参数和消息。
- `GenericProfile`：CSV / 未知 MAVLink 日志，按通道名映射。
- profile 只描述“怎么解释数据”，不做报告排版。

### 4.4 一键工作区与报告

统一一条链：

```text
FlightMeta
  -> ReviewPlan（页面、曲线、章节、requires）
  -> ReviewSession（实际加载的通道）
  -> Findings（统计和规则）
  -> ReportDocument（HTML）
```

- `ReviewPlan` 同时驱动“一键默认曲线”和“一键报告”，两边不漂移。
- 命令 id 归工具箱：`autopilot.review.default`、`autopilot.review.report`。
- 报告第一版只做摘要、关键曲线、基础规则、参数快照。
- 报告产物为自包含 HTML，复用画布渲染；第一版不做原生 PDF、不做 AI 诊断。
- 没有能力的章节直接不显示。

## 5. 协议描述驱动与调试

GNSS 工具箱支持 NovAtel OEM7、u-blox UBX、NMEA；autopilot 的 MAVLink 实时源和 tlog 回放、robotics 的 ROS/MCAP 也需要同样的协议检查能力。统一原则：**协议结构由 JSON/运行时描述驱动，代码里不穷举消息 ID，不写 `switch (msgid)`、`if (name == "BESTPOS")` 这类硬解码分支。** 协议调试面板和 parser 注册做成平台能力，所有已安装工具箱共用。

### 5.1 modules/datasource

MessageParser 的注册能力归 `modules/datasource`，不放在 `common`，也不放在某个业务工具箱；parser 实现仍归各业务工具箱。

```text
src/application/modules/datasource/
├── include/datasource/
│   ├── IMessageParser.h         # 协议解析接口
│   ├── MessageParserSpec.h      # 注册项，工具箱可见
│   ├── MessageParserRegistry.h  # 注册、查询、按协议枚举
│   └── DecodedFrame.h           # 统一解码结果
├── parser/
│   ├── MessageParserRegistry.cpp
│   ├── ProtocolCatalog.cpp      # 加载一次，按 protocol + key 查描述
│   ├── FrameDecoder.cpp         # 按描述解码字段
│   └── FieldValue.cpp           # 标量/数组/枚举/位域统一取值
├── dock/
│   ├── ProtocolInspectorDock.cpp
│   └── ProtocolInspectorModel.cpp
├── manager/                     # 既有 DataSourceRegistry / Session
├── dialog/                      # 既有
├── ribbon/                      # 既有
└── services/                    # 既有 cache / live
```

`IMessageParser` 按协议拆开，一个协议一个 parser：

```text
IMessageParser
├── protocolId()                   oem7 / ubx / nmea / mavlink / ros2 / mcap
├── sniff(frame)                   判断这段字节是否属于本协议
├── parse(frame)                   切帧、校验头/长度/checksum，产出 DecodedFrame
└── descriptorKey(frame)           选 JSON/运行时描述里的帧定义
```

注册项不是裸 parser 指针，而是带发现信息的 `MessageParserSpec`：

```text
MessageParserSpec
├── id                 唯一注册 id
├── protocolId         协议标识
├── displayName        调试面板显示名
├── priority           同协议多 parser 时的选择优先级
├── fileExtensions     .tlog / .ubx / .nmea / .mcap 等
├── sniff              可选，字节特征判断
├── factory            创建 IMessageParser
├── descriptorPaths    该协议使用的 JSON / 运行时描述
└── capabilities       全字段解析 / 在线 / 回放 / 只读等
```

注册链：

```text
Toolbox 安装/启用
        -> MessageParserRegistry::add(MessageParserSpec)
        -> modules/datasource 持有注册表
        -> 任何模块按 protocolId / 后缀 / sniff 查询使用
```

- `DataSourceModule` 创建并持有 `MessageParserRegistry`；其他模块通过 datasource 模块的公开接口取用，不自己维护第二份注册表。
- 业务工具箱只注册自己的 parser：GNSS 注册 OEM7 / UBX / NMEA，autopilot 注册 MAVLink / ULog / apbin，robotics 注册 ROS2 / ROS1 / MCAP。
- **只要工具箱安装并启用，parser 就自动进入注册表；工具箱没装，注册表里就没有对应协议，平台不内置协议清单。**
- 其他模块只通过 `MessageParserRegistry` 使用 parser，不 include 任何工具箱私有头，也不 `new` 具体 parser。
- `MessageParserRegistry` 提供 `find(protocolId)`、`all()`、`byExtension(suffix)`、`sniff(frame)`；同 protocolId 有多个 parser 时按 priority 选默认，全部保留给 Inspector。
- parser 只负责本协议的帧边界、头字段和校验；字段解码统一交给 `FrameDecoder`。
- `ProtocolCatalog` 启动时加载一次，按协议和帧 key 建索引；业务工具箱只注册描述文件路径，不直接操作索引。
- `FrameDecoder` 只认描述，不认具体协议。
- `ProtocolInspectorDock` 只消费 `DecodedFrame`，为所有已注册 parser 提供统一界面，不写 `if (protocol == ...)`。
- 在线协议使用环形缓冲，只保留最近 N 帧；暂停、过滤、单帧展开不能阻塞数据源线程。

### 5.2 描述模型

```text
ProtocolDatabase
├── protocol        oem7 / ubx / nmea / mavlink
├── version / dialect
├── frames[]
│   ├── key          messageID / class+id / talker+formatter / MAVLink msgid
│   ├── name
│   ├── header       同步字、长度字段、头类型、CRC / CRC extra
│   ├── fields[]
│   │   ├── name
│   │   ├── offset / 顺序
│   │   ├── type / size / count
│   │   ├── enumRef / bitmaskRef
│   │   ├── scale / unit / role
│   │   └── optional / extension
│   └── checksum
├── enums[]
└── bitmasks[]
```

解码结果统一成 `DecodedFrame`：

```text
DecodedFrame
├── protocol, frameName, timestamp
├── frameKey        messageID / class+id / talker+formatter / msgid
├── rawBytes
├── headerFields    sysid / compid / seq / length / CRC 等
├── fields[]        name / raw / value / unit / offset / enumLabel / bits
├── checksumOk
└── unknown         未命中描述时为 true，仍保留原始帧
```

- 按描述切帧、校验长度和 checksum。
- 按字段描述读取标量、数组、枚举、位域。
- 未命中的帧不丢弃，标为 `unknown`，原样交给调试界面。
- 通道生成由字段的 `role` 映射决定；没有 `role` 的字段只进调试面板，不默认绘图。

### 5.3 GNSS 协议目录

```text
builtin/gnss/
├── protocols/
│   ├── oem7_database.json       # 取自 NovAtel EDIE，609 messages / 266 enums / 3 bitmasks
│   ├── ubx_database.json        # u-blox class/id + payload 字段描述
│   └── nmea_database.json       # 57 个 formatter + 逗号字段描述（pynmea2 生成）
├── protocol/
│   └── parsers/
│       ├── Oem7MessageParser.cpp
│       ├── UbxMessageParser.cpp
│       └── NmeaMessageParser.cpp
├── datasources/
│   ├── oem7/
│   ├── ubx/
│   └── nmea/
├── analysis/
└── resources/
```

- `oem7_database.json` 继续保留现有 MIT 归属说明，见 `builtin/gnss/THIRD_PARTY.md`。
- 只引用 EDIE 的协议数据库，不引入 EDIE 的解码器实现。
- GNSS 注册三个 parser：`Oem7MessageParser`、`UbxMessageParser`、`NmeaMessageParser`，各自只处理本协议帧边界。

### 5.4 OEM7

- 由 `Oem7MessageParser` 处理同步字、`STANDARD` / `SHORT` 头型、长度和 CRC；`FrameDecoder` 按描述解字段。
- `database.json` 的 `messages[].fields` 以 `latestMsgDefCrc` 为键，同一 messageID 可能有多个版本；解码时用头部 CRC 选字段版本，缺失时退回 `latestMsgDefCrc`。
- 字段类型至少覆盖 `SIMPLE`、`ENUM`、`FIXED_LENGTH_ARRAY`、`VARIABLE_LENGTH_ARRAY`、`FIELD_ARRAY`。
- 现有 `Oem7Source.cpp` 里的 `BESTPOS`、`INSPVA`、`BESTATT`、`BESTVEL` 特判和 `switch` 必须删除，改由描述驱动。
- `RXCONFIG`、`RANGECMP` 这类特殊日志用独立 `FrameHandler`，按描述里的 handler 名称注册，不在 parser 里写 `if`。
- 未知 messageID 或未知 CRC 版本：保留帧、显示原始字段和十六进制，不静默丢弃。

### 5.5 UBX

- 由 `UbxMessageParser` 处理 `0xB5 0x62 + class + id + length + payload + checksum` 的帧边界；`FrameDecoder` 按描述解 payload。
- `ubx_database.json` 按 `class + id` 描述 payload 字段，`UbxMessageParser` 按 offset/scale 解字段，不写 `NAV-PVT`、`NAV-POSLLH` 的专用分支。
- 未知 class/id 仍显示帧头、长度、checksum 和原始 payload。
- checksum 错误单独标记，不进入正常通道。

### 5.6 NMEA

- 由 `NmeaMessageParser` 按 `$` 开头、逗号分隔、`*` checksum 的通用结构解析，不穷举 `GGA`、`RMC`、`GSA`、`GSV`。
- `nmea_database.json` 由 `tools/nmea-metadata.py` 从 pynmea2 生成，覆盖 57 个标准 formatter；`NmeaMessageParser` 按 formatter 查字段，未命中的语句保留原始 token。
- 兼容厂商私有语句：没有描述时按原始字段展示，不报错、不丢弃。
- 时间、经纬度、高度、定位质量等常用字段通过 `role` 映射进通道，不按语句名硬编码。

### 5.7 autopilot 协议解析

```text
builtin/autopilot/
├── protocols/
│   └── mavlink_metadata.json    # 由 pymavlink dialect 导出单位/枚举/字段偏移
├── protocol/
│   └── parsers/
│       ├── MavlinkMessageParser.cpp
│       ├── UlogMessageParser.cpp
│       └── ApbinMessageParser.cpp
├── datasources/
│   ├── mavlink/                 # 实时源
│   ├── tlog/                    # MAVLink 文件回放
│   ├── ulog/
│   └── apbin/
```

#### MAVLink

- 以 MAVLink 官方 `mavgen.py` 生成物为准，vendor 到 `src/3rdparty/mavlink/generated`；方言使用 `ardupilotmega`（已包含 common/standard/minimal）。生成脚本 `tools/mavlink-gen.sh` 固定 `--lang=C++11 --wire-protocol=2.0`，构建不跑 Python。
- `mavlink_metadata.json` 由 `tools/mavlink-metadata.py` 从 pymavlink dialect 导出，补 C++ 类没有的运行时字段信息：wire offset、字段顺序、数组长度、单位、枚举和枚举项。
- `MavlinkMessageParser` 使用 C++11 生成物的 `mavlink_frame_char_buffer`，framing 状态（`mavlink_message_t` / `mavlink_status_t`）由 parser 实例私有，多源并存不共享 channel/status；字段按 metadata 的 wire offset 全量解出，不使用手写 `switch (msgid)`。
- 实时源和 tlog 回放都只输出原始 MAVLink 帧，解码统一走 `MavlinkMessageParser`。
- `key` 按 dialect + msgid；同 msgid 在不同 dialect 下字段不同，不能只按数字查表。
- `MavlinkSource.cpp` 已删除 `decodePayload` 和 `switch (msgid)`，统一走 `MavlinkMessageParser`；实时源只挑选需要成曲线的字段，协议解码仍是全量。
- **全协议解析**：`MESSAGE_ENTRIES` / `mavlink_metadata.json` 里定义的所有 msgid 都按描述解码全部字段，不做白名单、不抽样；数组、扩展字段、枚举、单位都要展开。
- 库里没有的 msgid、dialect 或字段定义，标记 `unknown` 并保留原始帧；不伪造字段、不静默丢弃。
- 调试面板显示完整字段：`sysid / compid / msgid / seq / 消息名 / 长度 / CRC / 签名状态`，以及 payload 的每个字段和值。
- 未知 msgid、未知 dialect、字段截断、数组越界都保留原始帧。

#### ULog

- `UlogMessageParser` 解析 ULog 消息头、`FORMAT`、`DATA`、`INFO`、`PARAM` 等消息类型。
- 字段布局来自 ULog 文件内的 `FORMAT` 定义，运行时生成消息描述，不硬编码字段列表。
- 消息列表和字段树进入同一个 `ProtocolInspector`；原始 ULog 字节可展开十六进制。
- 未知 message type、未知 format id 保留原始帧，不崩溃。

#### apbin

- `ApbinMessageParser` 解析 ArduPilot DataFlash 的 `FMT`、`UNIT`、`MULT`、`FMTU`、`PARM` 和数据帧。
- 字段布局来自文件内的 `FMT` / `FMTU` 定义，运行时生成消息描述，不硬编码消息类型。
- 消息列表和字段树进入同一个 `ProtocolInspector`；未知 FMT 或字段截断保留原始帧。

### 5.8 调试界面

新增公共 `ProtocolInspectorDock` 面板，所有已注册 parser 共用：

- 左侧帧列表：时间、协议、帧名、帧 key、长度、checksum / CRC 状态。
- 右侧字段树：字段名、原始值、解析值、单位、字节偏移、长度、枚举标签、位域展开。
- 原始十六进制视图：选中字段时高亮对应字节区间。
- 在线模式：暂停 / 继续、按协议、msgid、sysid、compid 过滤、环形缓冲、清空。
- tlog / bag 模式：按帧回放、跳到指定时间或帧号。
- 未知帧：显示 `unknown`、原始头和 payload，仍可展开十六进制。
- 面板只消费 `MessageParserRegistry` 和 `DecodedFrame`，不为 OEM7 / UBX / NMEA / MAVLink / ROS 各写一套界面。
- 没有安装任何协议工具箱时，面板显示空态，不报错。

### 5.9 验收

- 解码代码中不出现 `BESTPOS`、`INSPVA`、`BESTATT`、`BESTVEL`、`GGA`、`RMC`、`NAV-PVT`、`NAV-POSLLH` 的特判。
- MAVLink 解码代码中不出现 `switch (msgid)`、`ATTITUDE` 等具体消息分支。
- MAVLink 对描述库中定义的全部 msgid 做全字段解析，不抽样、不设白名单；描述库没有的 msgid 保留原始帧并标记 `unknown`。
- 新增一条 OEM7 消息只改 JSON，不改解码代码；新增 MAVLink 消息只改 XML/JSON，不改解码代码。
- EDIE `database.json` 的 609 条消息能按描述通用解码，未知 CRC 版本不崩溃。
- UBX 未知 class/id、NMEA 未知语句、MAVLink 未知 msgid 都能在 Inspector 中看到原始帧结构和校验状态。
- checksum / CRC 错误、长度越界、字段截断都有明确状态，不产生假通道。
- 常用定位、速度、姿态字段通过 role 映射生成通道；其余字段只进 Inspector。

## 6. robotics 内部设计（第四领域）

### 6.1 范围与边界

`robotics` 承接 ROS / MCAP 生态，只解决“机器人运行日志”的加载、结构检查、默认曲线和运行报告：

- 数据源：rosbag2（mcap / sqlite3）、rosbag1（`.bag`）、独立 `.mcap`、DDS 在线订阅（可选）。
- 域内能力：topic / message 结构检查、TF 树完整性、关节状态、里程计轨迹、控制周期与时间戳抖动、传感器健康。
- 不做：CAN 信号（归 `automotive`）、飞控消息（归 `autopilot`）、SLAM / 建图 / 规划算法求解。
- 一个 bag 里混有 image、pointcloud、tf、joint_states 时，全部按 topic 描述解码进 Inspector，只有带 `role` 的字段生成通道。

### 6.2 数据源注册

- `ps_robotics` 静态库包含 rosbag2、rosbag1、mcap、dds、analysis。
- `ps_robotics_component` 只放 `RoboticsToolbox.cpp`，保证静态注册器进入宿主。
- `RoboticsToolbox::contribute()` 注册 `data_loader_rosbag2`、`data_loader_rosbag1`、`data_loader_mcap`、`data_source_dds`。
- `CommonDataSourcesComponent` 不再注册 MCAP、rosbag2，通用格式只剩 CSV、JSON、binary。
- `.mcap`、`.db3`、`.bag` 必须进入 `fileEntries`，不能落到 CSV 兜底。
- rosbag2 的目录型存储（`metadata.yaml` + `.db3` / `.mcap`）要按“目录即一个数据源”处理，不能只按单文件后缀匹配。

### 6.3 消息描述

描述来源有两层，缺一不可：

```text
robot description
├── 静态库   protocols/ros2msg_database.json   标准消息字段、嵌套、数组、常量、枚举
│            protocols/ros1msg_database.json
└── 运行态   rosbag2 metadata.yaml 的 topic / type / serialization_format
             rosbag2 每通道的 schema（ros2msg / ros_msgs / idl / protobuf / jsonschema）
             MCAP 的 Schema 记录（encoding + data）
```

规则：

- 索引键为 `topic + schema hash`；同名 topic 换 schema 版本后不共用描述。
- 字段查找先走运行态 schema，未命中再退静态库；两者都未命中标 `unknown`，保留原始字节和 hex。
- 嵌套消息、定长/变长数组、字符串、`builtin_interfaces/Time` 必须递归展开，`FrameDecoder` 支持嵌套描述。
- 时间以 bag 记录时间为准，`ros_time` 与系统时间都存进 `DecodedFrame`，曲线默认用 bag 时间。
- 不写 `if (topic == "/odom")`、`if (type == "nav_msgs/Odometry")` 这类分支；语义靠 `role` 映射。
- pointcloud、image 默认不进绘图通道，只进 Inspector 和统计（帧数、频率、分辨率、点数量）。

### 6.4 能力模型

```text
RobotMeta
├── SourceFacts   存储格式（mcap / sqlite3 / bag）、压缩、分卷、ROS 版本、时钟源
├── CapabilitySet TF / JointState / Odometry / Imu / Battery / Diagnostics / Command / LaserScan / Image / PointCloud
└── RoleMap       role -> topic 候选
```

- `CapabilitySet` 按实际 topic 的 schema + 字段 `role` 推断，不按 topic 名硬编码。
- 一个 role 允许多个候选 topic，按 profile 优先级选；候选冲突时以 `SourceFacts` 的时间基准一致者优先。
- 能力缺失时对应页面和报告章节整体省略，不补默认值。
- TF：建 tree，检查多 parent、断链、时间外插、重复 frame_id；发现即进 `Findings`。

### 6.5 默认工作区与报告

链条与 autopilot 一致：

```text
RobotMeta
  -> ReviewPlan（页面、曲线、章节、requires）
  -> ReviewSession
  -> Findings
  -> ReportDocument（HTML）
```

- 命令 id：`robotics.review.default`、`robotics.review.report`。
- 默认页面：运行概要、TF 树与退化、关节轨迹、里程计 XY 轨迹、控制周期与时间戳抖动、传感器健康。
- 报告第一版：运行时长、topic 频率/丢帧/抖动统计、TF 完整性结论、能力清单、命中规则。
- 自包含 HTML，复用画布渲染；不做 PDF，不接 AI 诊断。

### 6.6 验收

- 解码代码中不出现具体 topic 名和 `nav_msgs/Odometry`、`sensor_msgs/Imu` 之类的类型特判。
- rosbag2（mcap / sqlite3）、rosbag1、独立 mcap 都能打开并生成通道。
- rosbag2 目录型存储能被识别为一个数据源，分卷和压缩不影响打开。
- 未知 schema、protobuf / jsonschema 无法解码时保留原始帧，Inspector 可见 hex 和通道元信息。
- 嵌套消息和数组字段能展开，字段树层级与消息定义一致。
- TF 断链、多 parent、时间外插有明确结论，不静默忽略。
- 新增一条标准消息只改 JSON，不改解码代码。
- 关闭 `PS_BUILD_ROBOTICS` 后，其他域和平台仍可构建。

### 6.7 风险与未决

- schema 形态多（ros2msg / idl / protobuf / jsonschema），第一阶段只保证 ros2msg / ros1msg 全字段解码，其余按原始字节展示。
- 大 bag（GB 级、分卷 mcap）不能整段读入内存，需要分块读取和惰性索引。
- rosbag2 sqlite3 会引入 sqlite 依赖，需确认构建和打包策略。
- DDS 在线订阅依赖网络与 multicast，容器环境可能不可用，先做可选能力，不阻塞文件回放。
- TF 高频 topic 要进环形缓冲和降采样统计，不能全量留内存。
- 时间基准可能混用 ROS 时间和系统时间，曲线、报告、TF 查询必须统一到 bag 时间。

## 7. dynamic 清理

`src/extensions/dynamic` 删除前先摘出核心件：

| dynamic 内容 | 去向 |
|---|---|
| `data_loader/ulog`、`apbin` | `builtin/autopilot/datasources` |
| `data_source/mavlink` | `builtin/autopilot/datasources/mavlink` |
| `data_source/sim` | `builtin/autopilot/datasources/sim` |
| `data_loader/csv` | `extensions/builtin/common/datasources/csv` |
| `data_loader/common` | `extensions/builtin/common/loader` |
| `sdk/cpp`、`sdk/rust` | `extensions/sdk` |
| `data_loader/asc`、`blf`、`mdf4` | 先留 `builtin/datasources`，`automotive` 建好后迁入 |
| `data_loader/mcap`、`rosbag2` | `builtin/robotics/datasources` |
| `data_loader/json`、`binary`、`rustcsv` | `plugins/` 或独立仓库，默认不构建 |

删除目录不等于删除动态加载能力：`ExtensionRuntime` 的 `dlopen`、manifest 校验、ABI 检查继续保留，外部插件从 `PS_EXTENSION_DIR` 指向的目录加载。

## 8. 实施顺序

### P0：共享层先落地

- 建 `extensions/builtin/common`，迁 `ps_loader_common` 和 CSV，并把 CAN/DBC 归入 `builtin/automotive/datasources/can`。
- 建 `extensions/sdk`，迁 C++ / Rust SDK。
- 更新 GNSS、custom_protocol 对 `dynamic/sdk`、`dynamic/common` 的 include。
- 保持 target 名不变，先保证编译和测试通过。

### P1：autopilot 工具箱与数据源

- 建 `builtin/autopilot`，迁 ULog、apbin、MAVLink、sim。
- 抽 `MavlinkLogReader`，新增 `TlogDataLoader`。
- 改 `AutopilotToolbox` 注册，移除 `CommonDataSourcesComponent` 中对应项。
- 文件过滤器改为注册表生成，补 `.tlog`。
- 补统一文件入口测试：ULog、apbin、tlog、CSV 都能从 `DataSourceSession` 打开。

### P2：协议描述驱动

- 建 `src/application/modules/datasource/parser`，实现 `IMessageParser`、`MessageParserRegistry`、`MessageParserSpec`、`ProtocolCatalog`、`FrameDecoder`、`DecodedFrame`。
- `DataSourceModule` 创建并持有 `MessageParserRegistry`，业务工具箱在安装/启用时注册 parser。
- 建 `modules/datasource/dock`，实现公共 `ProtocolInspectorDock`。
- GNSS 增加 `Oem7MessageParser`、`UbxMessageParser`、`NmeaMessageParser`，安装/启用时自动注册。
- autopilot 增加 `MavlinkMessageParser`、`UlogMessageParser`、`ApbinMessageParser`，安装/启用时自动注册。
- 固化 `oem7_database.json`、`ubx_database.json`、`nmea_database.json`、`mavlink_database.json`。
- 删掉 `Oem7Source.cpp` 的 `BESTPOS`、`INSPVA`、`BESTATT`、`BESTVEL` 特判。
- 删掉 NMEA 的 `GGA`/`RMC` 分支、UBX 的 `NAV-PVT`/`NAV-POSLLH` 分支。
- 删掉 `MavlinkSource.cpp` 的 `decodePayload` 和 `switch (msgid)`，实时与 tlog 共用描述解码。
- MAVLink 对描述库中全部 msgid 做全字段解析；库里没有的保留原始帧，不伪造字段。
- 补未知帧、未知 CRC、checksum 错误、截断帧、MAVLink 未知 msgid、ULog/apbin 未知格式测试。
- 补注册测试：工具箱未安装/未启用时查不到 parser；安装后其他模块只通过 `MessageParserRegistry` 就能找到并使用。
- `ProtocolInspectorDock` 支持在线暂停/过滤、tlog 回放、帧列表、字段树、原始十六进制和字节偏移。

### P3：默认曲线

- 建 `SourceFacts`、`CapabilitySet`、`RoleMap`。
- 实现 PX4 / ArduPilot / Generic 三个 profile。
- 实现 `autopilot.review.default`，按能力生成默认页面和曲线。
- 用 `demo_flight.ulg`、`demo_apbin.bin`、tlog fixture 验证能力缺失时页面不出现空图。

### P4：分析报告

- 实现 `ReviewPlan`、`Findings`、`ReportDocument`。
- 实现 `autopilot.review.report`，输出自包含 HTML。
- 第一版只做摘要、关键曲线、基础规则、参数快照。
- 不做 PDF，不做 AI 诊断。

### P5：dynamic 清理与 automotive / robotics 骨架

- 建 `builtin/automotive` 骨架，迁 ASC、BLF、MDF4（组件注册已建，源码仍在 dynamic）。
- 建 `builtin/robotics` 骨架，迁 MCAP、rosbag2，补 rosbag1 和 DDS 注册位（组件注册已建，源码仍在 dynamic）。
- 补 `RoboticsToolbox::contribute()`，移除 `CommonDataSourcesComponent` 中 MCAP、rosbag2 注册。
- `Ros2MessageParser` / `Ros1MessageParser` / `McapMessageParser` 接入 `MessageParserRegistry`，先接 ros2msg，再补 idl / protobuf / jsonschema 的原始帧兜底。
- JSON、binary、rustcsv 移到 `plugins/` 或删除核心构建入口。
- 删除 `src/extensions/dynamic`，更新 `PS_EXTENSION_DIR`。
- 更新 `AGENTS.md` 的扩展边界，写清 `modules/datasource` 持有 parser 注册表，业务工具箱只注册实现，避免文档继续写“dynamic 承载官方格式”。

### P6：robotics 能力模型与报告（可延后，不阻塞 P5）

- 建 `RobotMeta`：`SourceFacts`、`CapabilitySet`、`RoleMap`。
- 实现 TF 树构建与断链、多 parent、时间外插检查。
- 实现 `robotics.review.default`：运行概要、TF、关节、里程计、控制周期抖动。
- 实现 `robotics.review.report`：topic 频率/丢帧/抖动、TF 完整性、能力清单。
- 用 mcap / sqlite3 / bag 三类 fixture 验证能力缺失时页面不出现空图。

## 9. 验收

- `cmake --build build -j4` 通过。
- `ctest --test-dir build --output-on-failure` 全通过。
- 打开 `.ulg`、`.bin`、`.csv`、`.tlog` 都能生成数据源和通道。
- 未知扩展名仍按 CSV 兜底；关闭 `PS_BUILD_AUTOPILOT` 后，CSV 和平台仍可构建。
- `data_loader_ulog`、`data_loader_apbin`、`data_loader_tlog` 均出现在文件打开过滤器中。
- 一键默认曲线只画实际存在的能力，不出现空图或假数据。
- 报告章节按 `requires` 出现；缺失能力时整段省略。
- OEM7 / UBX / NMEA 解码代码不出现具体消息名、语句名的特判。
- GNSS 的 `Oem7MessageParser`、`UbxMessageParser`、`NmeaMessageParser` 各自有独立测试；autopilot 的 `MavlinkMessageParser`、`UlogMessageParser`、`ApbinMessageParser` 各自有独立测试。
- 工具箱安装/启用后 parser 自动出现在 `MessageParserRegistry`；关闭工具箱后查不到对应 parser，平台不内置协议清单。
- 其他模块能只依赖 `modules/datasource` 公开头，通过 `MessageParserRegistry` 找到并使用 parser，不 include 工具箱私有头。
- 同 protocolId 注册多个 parser 时按 priority 选默认，Inspector 仍能看到全部 parser，不覆盖、不静默丢。
- OEM7 `database.json` 的 609 条消息按描述通用解码；新增消息只改 JSON。
- UBX 未知 class/id、NMEA 未知语句、OEM7 未知 CRC、MAVLink 未知 msgid、ULog/apbin 未知格式都能在 `ProtocolInspector` 中看到原始帧结构和校验状态。
- MAVLink 在线源和 tlog 回放显示完整字段：sysid、compid、msgid、seq、消息名、长度、CRC、签名状态和每个 payload 字段值。
- ULog / apbin 的 `FORMAT` / `FMT` 定义能生成消息描述，Inspector 显示消息类型和字段值，不硬编码消息列表。
- `src/extensions/builtin` 不再出现协议/格式的 `if (suffix == ...)`。
- 核心代码不再引用 `src/extensions/dynamic`；`rg "extensions/dynamic"` 只允许出现在历史文档或插件说明中。
- rosbag2（mcap / sqlite3）、rosbag1、独立 mcap 都能打开并生成通道；`.mcap`、`.db3`、`.bag` 出现在文件过滤器中。
- robotics 解码代码不出现具体 topic 名和消息类型特判；新增标准消息只改 JSON。
- 未知 schema、无法解码的 protobuf / jsonschema 通道保留原始帧，Inspector 可见 hex 与通道元信息。
- TF 断链、多 parent、时间外插能给出明确结论，不静默忽略。
- 关闭 `PS_BUILD_ROBOTICS` 后，其他域和平台仍可构建。

## 10. 风险与未决

- ULog、apbin 源码搬家会改相对 include 和 CMake 路径；第一轮保留 target 名，减少测试面改动。
- ULog / apbin 的字段定义在文件内，`ProtocolCatalog` 必须支持运行时注册描述，不能只支持静态 JSON。
- parser 注册生命周期跟随工具箱安装/启用状态；当前无热卸载，关闭工具箱需要重启后确认 parser 已移除，不能假装支持热插拔。
- `modules/datasource` 不能反向依赖任何业务工具箱；注册项只暴露 `IMessageParser` 和描述路径，否则又会退回硬编码。
- 同 protocolId 多 parser 的优先级、冲突和默认选择要固定；不能按注册顺序碰运气。
- MAVLink 回放逻辑要从 `MavlinkSource` 抽出，不能复制第二套解析器。
- MAVLink 解码要从 XML 生成描述，不能继续维护 `switch (msgid)`；dialect、CRC extra、MAVLink 2 扩展字段和签名帧必须一起处理。
- 在线 MAVLink 帧率高，Inspector 必须用环形缓冲和过滤，不能把全部帧留在内存。
- `.tlog` 从 `data_source` 变成 `data_loader` 后，实时 MAVLink 和文件回放要共用同一 reader，否则行为会漂移。
- CSV 放 `common` 后，autopilot profile 只能消费数据集，不能反向依赖 CSV loader。
- `automotive` 的格式范围尚未确定，ASC、BLF、MDF4 先不急着迁，但不能再留在 `dynamic`。
- 报告范围容易膨胀；第一版锁定摘要、曲线、基础规则、参数快照，不接 AI 诊断。
- OEM7 `database.json` 4.7 MB，启动加载和索引要一次完成；不能每帧重复解析 JSON。
- 同一 OEM7 messageID 有多个 CRC 字段版本；版本不匹配时降级到 `latestMsgDefCrc`，再不行保留原始帧。
- `RXCONFIG`、`RANGECMP` 是特殊日志，必须走独立 handler；不能因为“不硬解码”就在通用解码器里特判。
- UBX / NMEA 的 JSON 描述需要覆盖未知 class/id 和厂商私有语句；未知帧也要可见，不能静默丢。
- 只引用 EDIE 的协议数据库，不复制解码器源码；沿用现有 MIT 归属说明。
- robotics 的格式与 schema 风险见 6.7；其中 sqlite3 依赖和分卷 mcap 内存要影响 P5 的排期。
- `AGENTS.md` 当前仍把 `dynamic` 写成官方格式层，方案实施时必须同步改，否则边界会继续漂移。

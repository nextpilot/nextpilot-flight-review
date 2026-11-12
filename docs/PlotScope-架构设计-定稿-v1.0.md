# PlotScope 架构设计 · 定稿 v1.0

> **一句话**：Qt 6 Widgets 壳 + Rust 数据内核（同进程 C ABI），面向飞控 / 嵌入式 / 车辆 / GNSS / 机器人的高性能绘图分析软件。
> 本文件只放**结论与干货**。推导、被否决的方案、自我更正与源码对照记录在 `PlotScope-架构设计-v1.md`（过程稿，3000 行），需要追溯依据时查那份。

---

## 0. 决策清单（只留结论）

| 编号 | 决策 | 结论 |
|---|---|---|
| D1 | 运行形态 | **Qt 6 Widgets（C++）原生桌面 + Rust 数据内核**，同进程经 C ABI（非 IPC、非 cxx、非 Web 壳） |
| D1b | 图表区渲染 | 图表用 `QWindow` + 自绘 GL/RHI 或二维码图集；**禁用 `QQuickWidget`**（每帧一次全屏纹理拷贝，4K 下 2~4ms） |
| D1c | UI 框架范围 | **主界面全 Widgets**；QML 仅允许"演示大屏/仪表页"且必须独立 `QQuickWindow` 窗口 |
| D2 | Rust 内核依赖 | `arrow-rs` + `memmap2` + `rayon` + `tokio` |
| D3 | 画布选型 | **默认自绘**：Rust LOD + `drawPolyline`，实测 7.2 ms/帧 @20ch×8k（M0-0 结论见 §12）；**JKQTPlotter 降为可选后端**（同数据 13.1 ms，仍 <16.7 ms 帧预算，接入必须由 LOD 喂数）；Qwt 仅对照（许可未核）；要抗锯齿平滑曲线则换 GPU 渲染，另立 spike |
| D3b | 画布禁用项 | Qt Charts / Qt Graphs / QCustomPlot（GPL 或商业授权）不可用 |
| D4 | 数据中间态 | **NDF**（自研列存）+ **Arrow IPC** 做缓存与交换格式 |
| D5 | 脚本 | **Lua 5.4 内嵌**（MIT，静态库），表达式与脚本同一套语法；算子数学在 Rust 向量核心里，Lua 只做编排（`userdata` + 元表，一次调用算一整块）；**不再用 QuickJS** |
| D6 | 协议模板 | YAML/JSON 帧模板 → Rust 侧热编译为状态机 |
| D7 | 停靠 | **`QDockWidget`**（Qt 内建，零第三方许可义务，四区/浮动/tabify/saveState 够用）；Qt-ADS（LGPL-2.1）作升级项，M1 交互评审不够"跟手"再换，换的成本只在 `DockHost` 一层；KDDockWidgets（商业 + GPL）排除 |
| D8 | 缺省构建 | **CMake + Corrosion**（KDAB），Rust crate 随 CMake 一起编 |
| D9 | 插件 ABI | 5 个 C 函数 + `PsBytes`/`PsBatchOut` + `abiVersion` + `ps_abi_selftest()`；四种后端 builtin/dylib/wasm/sidecar |
| D10 | 布局持久化 | `.psl`（JSON，可分享可进 git）+ `.psc-workspace`（项目级）；`semantic`/`view` 分块；幽灵曲线；键序固定、原子写；撤销走命令模式 |
| D11 | 类型系统 | f32/f64 为主 + `kEnum`（i32 码 + 字典）+ `kString`（sidecar）；布尔并入 `kEnum`(0/1) |
| D12 | 许可红线 | 主程序与内核 **BSD-3** 兼容；引入第三方必须逐条核对许可（Qwt License 1.0 待核，未核前只用 JKQTPlotter） |
| D13 | 协议编辑器定位 | **自定义 Loader 的附属编辑器**（子窗口 / `QDialog`，对应 PJ4 dialog vtable、PJ3 `optionsWidget()`）；**不设顶层标签页**，生命周期随 Loader |
| D14 | 工作区分区 | **四可停靠区（数据源/属性面板/脚本/日志）+ 固定绘图区 + 固定带**；绘图区不可停靠 |
| D15 | 页 | 一页 = 一棵 `QSplitter` 树（plot 与仪表混排），页签是页列表；每页独立时间窗，默认不联动 |
| D16 | 工程结构 | `cmake/` `src/{app,kernel,plugin,function,3dparty}` `tool/` `test/`；C++ 与 Rust 都在 `src` 下按语言分目录，`src/kernel` 是 Cargo workspace 根；`app` 只依赖 `ps-abi` 一个头文件 |

---

## 1. 场景与格式清单

### 1.1 六个生态的现实

| 生态 | 真实格式/协议 | 量级 | 我方进度 |
|---|---|---|---|
| PX4 | ULog（`.ulg`，自描述）+ MAVLink | 记录 50~250 Hz/主题，1 h ≈ 100~600 MB | M1 |
| ArduPilot | DataFlash（`.bin`/`.log`，FMT 自描述）+ MAVLink | 同上；大量枚举（参数/模式/故障码） | M1 |
| 嵌入式探针 | SEGGER RTT、SystemView、J-Scope、串口 ASCII/自定义二进制、DataTamer、Influx line | 10~100 kHz 单通道 | RTT M2；SystemView 后置 |
| 车辆总线 | DBC + ASC/BLF/candump + MDF4（ASAM） | 500 kbit/s~1 Mbit/s；1k~10k msg/s；**一帧数十信号** | CAN 家族 M1（待拍板）；MDF4 M2 |
| GNSS | NMEA 0183、NovAtel OEM7（ASCII+二进制）、u-blox UBX、RTCM3 | 1~50 Hz；raw 5~20 Hz | NMEA/OEM7 M2；UBX M2；RTCM 后置 |
| 机器人/自驾 | MCAP、rosbag1/2、Foxglove WS、ROS2 DDS、PCD/PLY、URDF、MP4、LeRobot | 话题 1~100 Hz；点云单帧 0.1~1 MB | MCAP/rosbag2 M3；其余后置 |

### 1.2 我们的格式矩阵（按里程碑）

| 阶段 | 内容 |
|---|---|
| **M0** | CSV/TSV |
| **M1** | ULog + **DFLog(.bin)** + **CAN 家族（DBC + ASC + BLF + candump）** + MAVLink（tlog/流） |
| **M2** | MDF4（多时间基）+ NMEA + NovAtel OEM7 + UBX + 串口/RTT 流 |
| **M3** | MCAP / rosbag2 + MP4 同步回放 |
| **后置** | Parquet、rosbag1 `.bag`、PCD/PLY、LeRobot、LSL/MQTT/ZMQ/Foxglove/Mosaico、RTCM |

### 1.3 与 PlotJuggler 4.1.0 的关系（实测结论）

- PJ4 的加载器全部移出核心，改为 **Marketplace + `pj-official-plugins`** 分发；核心只留薄壳。
- PJ4 主干：CSV / ULog / MCAP / Parquet / **MF4(+DBC)** / **BLF(+DBC)** / **candump(+DBC)** / PLY·PCD / MP4 / LeRobot；流 UDP·MQTT·ZMQ·ROS2·Foxglove·PJ-Bridge·WebRTC·LSL；解析 JSON·Protobuf·ROS1/2·Arrow·Influx·DataTamer。
- **PJ4 的四处空白，即我们的差异化**：①无 ArduPilot DFLog 加载器 ②无 GNSS 原始解析（NMEA/OEM7/UBX/RTCM）③无 Vector ASC（只有 BLF）④无 SEGGER RTT / SystemView。
- 策略：**对齐主干 + 补空白，不整抄清单。**

### 1.4 三条排序判据

1. **DBC 是元数据不是格式**：`factor`/`offset`/`unit`/位域必须映射成 L2 通道元数据变换，不能在解码后重算。
2. **先做"会崩的"，后做"能搬的"**：位域/CRC/变长帧（CAN、GNSS、DFLog）优先于纯搬运（CSV、MCAP）。
3. **能喂给协议编辑器**的格式优先（NMEA/UBX/OEM7/DBC 可直接当模板样例）。

---

## 2. 架构分层

```
┌──────────────────────── Qt 6 Widgets（C++）────────────────────────┐
│ 主窗口 / 停靠 / 通道树 / 属性面板 / hex 编辑器 / 表格 / 图表画布 / 仪表页 │
└───────────────────────────────┬───────────────────────────────────┘
                      C ABI（一张 C 函数表，批量传递）
┌───────────────────────────────┴───────────────────────────────────┐
│                    plotscope-kernel（Rust）                        │
│ 接入编排 │ 解析器宿主 │ NDF 列存 │ Arrow 缓存+mmap │ 查询/LOD │ 摄取 │
│ 表达式引擎 │ 协议模板编译 │ 插件 ABI 宿主                           │
└───────────────────────────────────────────────────────────────────┘
```

**归属判定三问**：① 要遍历/变换数据 → **Rust**；② 只在屏幕上存在 → **Qt**；③ 必须能被跨语言插件替换 → **Rust 宿主 + C ABI**。

**边界四条铁律**
1. 跨 C ABI 只传批量（`{指针, 长度, 采样数, 通道数}`），绝不为单点调用。
2. UI 线程永不阻塞、永不做重活；查询异步，结果是"已降采样的只读切片"。
3. 内核不认识 `QWidget`，Qt 侧不碰 `Chunk`。
4. 曲线一律 `drawPolyline` + 关 AA + **整数 1px** 线宽：Qt 6.2 软件光栅下 AA 描边慢两个数量级（20×1500 段 367 ms vs 3.6 ms），线宽 1.3 这类小数会掉进宽线轮廓器（26.5 ms）。

**容易错判的六处归属**

| 功能 | 归属 | 判据 |
|---|---|---|
| 插件加载、ABI 校验、`selftest` | Rust | 管数据流不管界面 |
| 协议模板编译（YAML→状态机） | Rust | 产物是解码器 |
| 协议编辑器本体（字段树/hex/联动/CRC 试算） | Qt（宿主提供通用控件） | 纯交互绘制；帧结构由 Loader 声明 |
| 单位换算（factor/offset） | **Rust** | 改变数值 → 影响降采样正确性 |
| 图表画布（轴/图例/游标/框选） | Qt | 数据由 Rust 给切片 |
| 仪表页（地平仪/罗盘/表盘） | Qt | `QPainter` + `QPropertyAnimation` |

---

## 3. 数据模型（NDF）

```
Chunk<8192>:
  t:      Float64Array[8192]（列内共享，所有通道共用一根时间轴）
  y[i]:   Float32Array[8192] | Int32Array[8192]（enum）| 字符串 sidecar
  gap:    1 bit / 采样（无法采信的点置位，不删、不插值）
LOD: 桶 [64, 512]，每 chunk 在 512 档为 16 桶，存 min/max/首/尾
内存: 8N + 4N·C + N/8  （N=采样数，C=通道数）
```

- **`kEnum`**：`codes: Int32Array[8192]`（与 f32 等宽，内存公式不变）；字典挂 `ChannelMeta`（`enumMap` + `enumUnknown: keep|gap`）；Arrow 用 `Dictionary<int32,string>`。
- **降采样对 enum 必须分段**：桶内多码全保留变化点，首尾补进入态；`QueryResp` 返回 `StateSegment[]`。
- 三条类型不变量：① 同一 `applicationId` 的字典不可变（改 YAML 必须重解析）② 布尔一律存 `kEnum`(0/1) ③ 字符串列不参与降采样，只返回时间区间内首末值。

---

## 4. 存储与缓存

| 项 | 结论 |
|---|---|
| 中间态 | NDF（内存列存，chunk 化） |
| 缓存文件 | **Arrow IPC File** + 分段 `mmap` + `index.json` |
| 失效判定 | 弱指纹（文件大小 + mtime + 头尾采样哈希），不作强校验 |
| 二次加载验收 | 1 GB ULog **≤400 ms**（热态 ~100 ms） |
| 超内存数据 | 全量走 mmap，内存只驻留窗口 ± 预读区 |

---

## 5. 加工层（脚本与单位）

**四层，按"是否改变数值"分层**

| 层 | 位置 | 例子 | 是否物化 |
|---|---|---|---|
| L1 显示单位 | 轴级 `displayUnit` | `m/s` → `km/h` | 否，画的时候乘 |
| L2 通道元数据变换 | 解析器读出的 `factor`/`offset`/`unit`（ULog、MDF4、DBC） | 原始计数 → 物理量 | 否，**但必须在降采样前应用** |
| L3 派生通道 | 算子（见下） | 微分/积分/滤波/四元数 | 视引用次数 |
| L4 曲线表达式 | 仅当前视图 | `ps.ch("a")*2 + ps.ch("b")` | 否 |

**可交换性判据（决定能不能"后置映射"）**

| 变换性质 | 做法 | 成本 |
|---|---|---|
| 保序仿射 / 反序仿射 / 单调非线性 | 后置映射 `MapOp`，每桶 4 点 | O(桶数) |
| 非单调（如平方） | **必须物化** | O(N) |
| 多输入（如 `a+b`） | **必须物化**，且显式对齐时间轴 | O(N) |

**铁律**：**先算派生/L2，再进 LOD 桶**。否则 min/max 丢尖峰、diff 出假导数。

**算子注册表**：一算子一 YAML（声明式 schema），宿主不实例化即读 schema 并自动生成参数面板：

```yaml
id: derivative
name: 一阶微分
output_kind: scalar
params:
  - {name: window, type: integer, default: 1, min: 1, max: 1000, unit: samples}
monotone: false      # false → 自动物化
affine: false
```

### 5.1 脚本引擎：Lua 5.4 内嵌（D5 修订）

结论：**用 Lua，不用 QuickJS，也不再自研表达式 AST 解析器**。理由不是"Lua 更高级"，是三条硬账：

| 维度 | 内嵌 Lua 5.4 | 内嵌 QuickJS | 自研表达式 DSL |
|---|---|---|---|
| 体积 | 源码约 3 万行，x64 静态库 **250~400 KB** | ~1 MB 级 | 0（但解析器 3~5k 行代码是我们自己维护） |
| 性能（紧凑数值循环） | 每秒数千万次简单算术 | 同机约为 PUC Lua 的 1/2~1/3（M0 复测口径） | 取决于我们写成什么样 |
| 语法学习成本 | 1 小时能写对 | JS 熟手更快 | 用户得学"我们家的方言" |
| 内嵌难度 | 一个 `luaL_newstate` + 白名单库；**无异常、无 C++ ABI** | 要处理 JS 值栈与 GC 句柄 | 要写词法/语法/类型检查/报错定位 |
| 沙箱 | 不开 `io/os/package/load` 即基本封闭；`lua_sethook` 做指令预算，自定义 allocator 做内存上限 | 需自己裁 std | 天然封闭 |
| 许可 | MIT（与 BSD-3 兼容） | MIT | 自家 |

**关键设计：Lua 不碰逐样本循环。** 通道以 `userdata` 交给 Lua，算术走元表，一次运算符调用处理**一整块数组**（攒批的 10 ms 块），真正的加减乘除、滤波、四元数在 Rust 里跑（`rayon` 分片、连续内存、无分支热循环）。Lua 的职责只是"把算子图拼起来 + 分支判断 + 多输出命名"：

```lua
local ps = require 'ps'
local ax, ay = ps.ch('imu.acc_x'), ps.ch('imu.acc_y')
out = ps.sqrt(ax*ax + ay*ay)      -- 3 次 C 调用，每次算整块
out.name, out.unit = 'acc_norm', 'm/s^2'
```

这条路径的量化口径：每脚本每块一次 Lua 调用，200 条脚本 × 100 块/秒 = **2 万次调用/秒**，调用开销完全可忽略；真正的成本在内存带宽（向量算子基本是 memory-bound，2~5 GB/s 级）。

**所以标量循环是"能用但别用"**：写成 `for i=1,n do ... end` 逐样本跑，速度掉到 1/100，等于把实时性赌在解释器上。宿主在日志里给一次显式告警（同一条脚本只警告一次），并把它标成"慢脚本"。

**表达式栏就是 Lua 语法糖**：用户输入 `a*2+b`，宿主包成 `return ps.ch('a')*2 + ps.ch('b')` 再编译。这样"轻量表达式"与"完整脚本"共用一套语法、一个编译器、一份错误定位，省掉整个 DSL。

**沙箱与失败语义（一条都不能省）**：

| 项 | 做法 |
|---|---|
| 库白名单 | 只开 `base`（去掉 `dofile/loadfile/load/require`）、`math`、`string`、`table`；`os`、`io`、`package`、`debug` 全不开 |
| 指令预算 | `lua_sethook` 每 10k 指令查一次墙钟，单块超 2 ms 即中止该脚本 |
| 内存上限 | 自定义 allocator，单脚本 64 MB，超限返回 `LUA_ERRMEM` |
| 不确定性 | 禁 `os.time/os.clock/math.random`（随机需显式传种子），保证同一份数据两次跑出同一条曲线 |
| 出错隔离 | 每脚本独立 `lua_State` + `pcall`；出错只把该派生通道标 `kError`（曲线变灰），不拖停整条流水线 |
| 热重载 | 脚本文本 hash 变化才重编译；编译失败保留上一版继续跑，日志给行号；重载只影响后续块，已物化的历史需显式"重算" |

**为什么不上 LuaJIT**：LuaJIT 数值循环快 5~20 倍，但 2.1 长期停在 beta、x64 之外的平台支持弱、且与我们"避免在边界算术上赌第三方"的原则冲突。留作 M2 的 spike：**只有当"向量算子路径实测不达标"时才考虑**，而不是默认。

---

## 6. 绘图

| 项 | 结论 |
|---|---|
| 库 | **JKQTPlotter**（LGPL 2.1、官方 CMake、Qt6 实测至 6.10、有 secondary axis + LaTeX）；Qwt 仅作 spike 对照（6.3.0 / 2024-05，无官方 CMake，Qwt License 1.0 待核） |
| 降采样 | **min/max 分桶**（可视窗口 + 像素宽度定桶数），非平均 |
| 交互清单 | 框选缩放、滚轮缩放、拖拽平移、游标（多轴同步）、图例带实时值、暂停后回溯、自适应 Y 轴、缓存上限 |
| 自绘起点 | M2 只自绘"实时滚动面板"（GPU 顶点批量上传）；M3 视瓶颈扩展到全量 |
| 工时账单（自绘） | 几何 1~2 周 / 文本 2~3 周 / 交互与算法 3~5 周（这是优先用现成库的理由） |

---

## 7. 仪表页

- `QPainter` 自绘：地平仪、罗盘、表盘、条状指示、告警灯；动效 `QPropertyAnimation`。
- 数值不跳动靠**定点格式化 + 固定宽度布局**。
- 若将来要触屏/大屏演示且 Widgets 效果不达标 → 独立 `QQuickWindow` 接 QML，主界面不动。**不许 `QQuickWidget`。**

---

## 8. 工作区：区域划分与停靠

**四个可停靠区 + 一条固定带 + 一个浮动池。绘图区永不停靠。**

| 区 | 内容 | 可停靠 | 可多实例 |
|---|---|---|---|
| **A 数据源** | 并列多个源（`ulog://`、`can://`、`mavlink://`），每源下三个视图：**数据字段 / 参数 param / 消息 message** | 左、右、底、浮动 | 否（一实例，内部并列多源） |
| **B 绘图区** | 页（Page）容器：一页 = 一棵切分树，叶子是 plot 或仪表 | **否**（主体固定居中，吃掉剩余空间） | 否 |
| **C 属性面板** | 同一个面板，按**点中的对象**换内容：曲线 / 坐标轴 / 画布 / 仪表 / 页 / 参数 / 消息 / 数据源 | 左、右、底、浮动 | 否 |
| **D 脚本** | Lua 脚本行 + 派生通道列表 + 执行日志 | 左、右、底、浮动 | 是（第二实例可只留表达式） |
| **E 日志诊断** | 加载日志、插件告警、丢弃计数、帧时间 | 左、右、底、浮动 | 是 |
| **F 固定带** | 顶部：**Ribbon**（六个任务页签 + 传输控制）+ 页签栏；底部：状态栏（吞吐/延迟/内存/22 ms 预算条） | 否 | — |

### 8.1 为什么绘图区不可停靠

画布是主体而不是工具窗。一旦允许把它拖走，立刻出现"中间空了只剩一排工具窗"的状态，而画布的价值就在于**吃掉全部剩余空间**。PJ4 同样把中间画布区固定。可停靠的只有"辅助信息"。

### 8.2 页（Page）的语义

- 一页 = 一棵 **`QSplitter` 树**，叶子两类：`PlotCanvas`、`InstrumentWidget`，可以同页混排（2 plot + 1 地平仪）。
- 页签（`QTabBar`）就是页的列表：`姿态与振动` `车辆总线` `混排` `仪表大屏` `+`。
- **页签栏只跨绘图区宽度，不横跨窗口**：页属于绘图区，不属于窗口；宽度跟着绘图列走（左/右停靠区怎么拖，它就在中间那段里伸缩）。页签条挂在 `PlotArea` 顶部，不放在 `QMainWindow` 的菜单区——否则左右停靠区会显得被页签"压住"，视觉上关系就错了。
- **"多 tab" 指的是页，不是多窗口**；一页之内竖切 3 个 plot，用 `QSplitter` 的可拖分隔条，比例入库。
- 每页有自己的时间窗（默认继承主源视窗），跨页联动是可选开关，默认不联动（否则 4 页一起重绘，白送 4× 绘制成本）。

### 8.3 两层布局持久化（与 D10 对齐）

| 层 | 存的什么 | 关键字段 |
|---|---|---|
| 工作区层 | 哪些 dock 在左/右/底、浮动位置与尺寸、可见性、分隔条比例 | `workspace.docks[]` |
| 页层 | 每页切分树、叶子类型、绑定的 `源:通道`、坐标轴、单位、LOD、仪表参数 | `pages[].splitTree` |

两份进同一个 `.psl`：`semantic{}`（通道绑定、单位、轴、页结构）必须保留；`view{}`（像素尺寸、停靠位置）可丢 —— 换机器/换分辨率时 width 全错。恢复流程：先按 `semantic` 重建，再按 `view` 尽力贴合，贴不上就退回默认。

**未连接的源不静默消失**：数据源区保留条目并标"未连接"，页里引用它的通道退化为**幽灵曲线**（空壳 + 标记），重新连接同一 URI 即恢复。

### 8.4 数据源区为什么按源分视图

三个源就是三套命名空间：`ulog://uav.attitude/roll`、`can://can0/ENGINE_STATUS/engine_rpm`、`mavlink://1/ATTITUDE/roll`。平铺必然重名。所以：

- 列表项一律显示 `源 › 命名空间 › 字段`，拖到画布时把源前缀一起带过去（绑定键含源，不含源就是错绑）。
- **参数（param）只读快照**：来自 ULog/DFLog 的参数表、DBC 的 `BA_` 属性、MAVLink `PARAM_VALUE`。用途是"这条曲线当时的增益是多少"的对照，不做写入。
- **消息（message）是结构视图**：`vehicle_attitude` 这类主题/报文的字段清单与频率，点一条即展开成曲线组。
- 源的行内状态：`已连接 / 只读回放 / 未连接 / 校验失败`。只有"已连接"能新增绑定。
- **在线源必须带开始/暂停**（文件源不带）：每行一个 17px 的 ▶/⏸ 按钮 + 状态字 + 速率；区头有"全部暂停/全部开始"；状态栏显示 `N 源采集中 · M 暂停`。

**在线源的三个状态与语义**（不能混）：

| 状态 | 含义 | 数据行为 |
|---|---|---|
| 未连接 | 源没开，只有配置 | 不取数，树里全是幽灵 |
| 运行中 | 正在取数 | 进环形缓冲 → 每 10 ms 攒批 → 引擎入列存 |
| 已暂停 | 停止取数，**不清缓冲** | 保留最后 5 s；恢复时在时间线上插一个**缺口标**（gap），不是假装连续 |

暂停不能实现成"停止消费"：那会让上游积压、内存涨到被 OOM 杀掉。正确做法是**背压丢旧留新**（环形缓冲满就覆盖最旧），并累计"丢帧数"显示在状态栏——用户看曲线断没断，先看这个数。

### 8.5 多源同时接入的唯一耦合点

时间轴以**主源**为基准（可切换），其它源按时间戳对齐：`就近 / 线性插值 / 零阶保持` 三选一，在各自的 Loader 编辑器里配（见 §9）。除此之外各源互不干涉：独立解析线程、独立列存、独立 LOD。

### 8.6 顶部为什么用 Ribbon

PJ 现在的界面（实测）：`File` 下拉 + 左侧竖排六个分组（Data / Layout / Streaming / Publishers / Timeseries List / Custom Series），命令和状态混在一列，一屏只露 3~5 个入口，想"画一条曲线"得先去 Timeseries List 里翻。这是 2005 年的 Win32 工具箱审美：3D 边框、灰底、缩进分隔。

Ribbon 的价值不是好看，是**把命令按任务分组、一屏全露**：组名就是任务名（数据源 / 缓存 / 源管理），一屏 12~16 个高频命令，点一次就到。

**库选型（spike 实测 2026-11-12）**：**SARibbon 2.9.6**（MIT；CMake 包 `SARibbonBar::SARibbonBar`；Qt 6.2.4 编译通过；静态库单 target；源码 24 MB）。七页签 + panel + 右侧 `rightButtonGroup()` 全成立；`CompactTwoRow` + 隐藏标题栏 + tab 26 px / panel 标题 15 px / 图标 32 px → **展开 97 px**，`setMinimumMode(true)` → **折叠 28 px**（原型目标 74 / 32 px，展开态高 23 px、折叠态还低 4 px）。两个坑：静态库必须手动 `Q_INIT_RESOURCE(SARibbonResource)`，否则 dark 主题静默回退浅色 QSS；内置观感是 Office 风格，要还原原型那套扁平/1px 分隔必须整体重写 QSS 并换自绘 SVG 图标。

| 项 | 结论 |
|---|---|
| 页签 | **文件** · 数据源 · 绘图 · 仪表 · 脚本 · 协议 · 视图（文件页专管布局/设置/截图/导出，不与数据操作混） |
| 组 | 每组 ≤ 3 个大按钮 + ≤ 2 列小按钮；组名是任务，不是名词堆 |
| 尺寸 | 展开 = 32 px 页签行 + 74 px 组区；折叠 = 32 px；双击页签折叠（Office 行为） |
| 实现 | `QTabWidget` 做页签 + `QToolButton`（`ToolButtonStyle=TextUnderIcon`，54×54）做按钮 + `QLabel` 做组名。**不用 `QToolBar` 堆叠**，那会退回 PJ 的工具栏 |
| 命令源 | Ribbon、右键菜单、快捷键共用**同一份 `QAction` 注册表**：一处禁用三处同步，避免"按钮亮着但命令不可用" |
| 图标 | 单色线条 SVG，20 px / 1.4 px 描边，一个视觉族；不混彩色图标 |
| 代价 | 吃 74 px 垂直空间 → 1080p 笔记本必须能折叠；窗口 <1100 px 自动折叠并隐藏度量芯片 |
| 视觉 | 平涂、1 px 发丝线、无 3D 边框、无渐变；强调色只有一个（琥珀），只标"当前页签 / 当前选中" |

> 定稿立场：Ribbon 只承载**高频命令**。低频配置（Loader 选项、协议模板、对齐策略）仍然走各自对话框（§9），不往 Ribbon 里塞第二排按钮。

### 8.7 属性面板：按"点中的对象"换内容

不叫"检查器"——它检查的不是程序状态，而是**你刚点中的那个对象**。一个面板，八种内容，切换的判据只有"选中了什么"。

| 点中 | 面板显示 | 可改 |
|---|---|---|
| 曲线 | 绑定键 `源›命名空间›字段`、类型（kFloat32/kEnum/kString）、采样率、L1 显示单位、颜色、线宽、所属轴、是否幽灵 | 单位、颜色、线宽、所属轴 |
| 坐标轴 | 所属画布、Y 范围、自动缩放开关、刻度数、单位、是否对数 | Y 范围、自动缩放、刻度数、对数 |
| 画布 | 所属页、切分位置（比例）、曲线数、网格、游标联动、Y 自适应 | 网格、联动、自适应 |
| 仪表 | 控件类型、绑定通道、刷新率、量程、合成耗时、是否 QML | 量程、刷新率、绑定 |
| 页 | 页名、切分树摘要、时间窗（独立/跟随主源）、跨页联动 | 页名、时间窗策略 |
| 参数 | 名字、值+单位、来源、是否可写（恒为否）、用途 | 无（只读快照） |
| 消息 | 消息名、频率、字段清单、来源 | 无（结构视图） |
| 数据源 | URI、Loader 类型与 ABI、运行状态（未连接/运行中/已暂停）、速率、背压与丢帧数、通道数、对齐策略 | **开始/暂停**、断开（暂停并保留缓冲）、设为主源、打开编辑器 |

**命中优先级**（一张画布上三种对象叠着，必须定死顺序）：

1. 左侧刻度带（画布 `x < 42px` 且落在绘图区高度内）→ **坐标轴**；
2. 距任一可见曲线 ≤ 14 px → **曲线**（多条命中取最近）；
3. 其余绘图区 → **画布**；
4. 画布外的页签 → **页**；仪表格子 → **仪表**。

**为什么不做右键菜单式属性**：右键菜单要记"我在哪个对象上右键"，还要处理菜单残留；左键直接选中 + 常驻面板，选中态在画布上要有可见回执（曲线加粗、轴带淡琥珀底、画布描边），否则用户不知道面板在说谁。选中态与 `.psl` 的 `semantic` 一起存（下次打开还原到同一对象）。

### 8.8 拖拽的最小实现集（M0 必做）

1. 拖 dock 标题栏 → 左/右/底三个边缘带高亮（各占 22%），松手即停靠；
2. 拖出边缘 → 变浮动窗（`Qt::Tool`），标题栏可再拖回；
3. 拖分隔条改比例，双击复位默认；
4. tabify（两个 dock 叠成一组 tab）**推迟到 M1**，M0 只做四向停靠 + 浮动，避免第一版就陷进 dock 框架的深水区。

默认尺寸：左 216、右 240、底 132，画布最小 640×360。

---

### 8.9 画布上的五个基本操作

| 操作 | 交互 | 结果 |
|---|---|---|
| 建曲线 | 从数据源树把字段**拖到画布**（落到轴刻度带也一样） | 进本画布的轴，绑定键带源前缀；同字段重复拖不重复建 |
| 选中 | 左键点曲线 / 轴 / 画布 | 属性面板换内容（§8.7） |
| 缩放 | 滚轮：指针在绘图区缩 X、在左侧刻度带缩 Y，锚在指针位置 | 只改视窗与量程，不动数据 |
| 平移 | 拖动：绘图区平移 X、刻度带平移 Y | 拖动阈值 3 px，与点选互不误触 |
| 复位 | 双击画布 | X 回全时长，Y 回原始量程（`BASE` 快照） |

**右键三套菜单，作用域各自独立**：

- **曲线**：属性、隐藏/显示、换色、仅按这条曲线自适应 Y、新建同单位轴（M1）、导出该通道 CSV
- **坐标轴**：Y 自适应、固定当前范围、重置原始量程、对数轴、Y 反向、复制范围到其它轴
- **画布**：Y 自适应、重置时间窗、隐藏全部曲线、导出本画布 PNG

> 为什么不全塞进属性面板：面板管"这个对象现在是什么"，菜单管"以这个对象为作用域做一次动作"。混在一起，面板就退化成命令堆。

**截图与图片另存为**：当前页所有画布合成一张 PNG（带页名与时间窗），不是 OS 抓屏——按 DPI 放大绘制，导出 2× 位图；仪表页由 QPainter 直接渲染进 `QImage`，不依赖窗口可见。

**布局与设置分开导出**：`.psl` 存布局语义（页/切分树/轴范围/绑定/停靠/选中对象），`.psc-settings` 存全局偏好（主题/单位/缓冲/内存上限/线程数/丢帧策略）。都是 JSON，可进 git diff；导入只回填轴范围、绑定显隐、视窗与选中态，不重建数据。

## 9. 协议编辑器（自定义 Loader 的附属编辑器，不是独立模块）

**定位**：它不是第六个模块，而是 `data_loader` 这类 Loader 的"我是怎么解帧的"那份 UI —— 等价于 PJ4 的 **dialog vtable**（`PJ_get_dialog_vtable`）、PJ3 的 `virtual QWidget* optionsWidget()`。宿主给容器和通用控件，帧结构由 Loader 自己声明。

| 项 | 结论 |
|---|---|
| 归属 | 属 Loader 插件，随 Loader 注册/销毁；**不做顶层标签页** |
| 入口 | ①加载向导里"新建/编辑协议" ②数据源列表每行的 ⚙ ③菜单 加载 → 编辑协议 |
| 形态 | `QDialog` + `Qt::Tool` 子窗口：独立标题栏与关闭，不参与主工作区布局持久化 |
| 宿主提供 | 字段树 / hex 视图 / 三向联动 / 位域标尺 / CRC 试算 / 模板编译（Rust 热编译）这套通用控件 |
| Loader 提供 | 同步字、长度规则、ID、字段位域、CRC 参数、候选样例帧、字段名与单位 |
| 无专用 Loader 时 | 内置"通用帧模板"：任意变长帧 + 位域，模板存为 `.psproto` 可复用 |
| 位域 | 起始位 + 位宽 + 有/无符号 + 字节序 + 跨字节；CRC 自动试算 |
| 模板格式 | YAML/JSON → Rust 状态机（编辑不重启，失败回滚上一版） |
| 验收素材 | NMEA / UBX / NovAtel OEM7 / DBC 四套真实帧 |
| 超越点 | JCom 只做**固定帧长 + 字节级**；我们做**变长帧 + 位域** |

**为什么不做标签页**：tab 是给"长期并行观看的对象"（曲线、仪表）用的；协议编辑是**低频、有明确进出**的配置动作，且必须绑定到某一个 Loader。放进 tab 会出现"没有 Loader 的空编辑器"和 tab `/` Loader 生命周期不同步两个烂摊子。子窗口天然绑定调用方：Loader 关掉，编辑器跟着关。

---

## 10. 插件与 ABI

**C ABI 5 函数**

```c
uint32_t           ps_abi_version(void);
const PsPluginInfo* ps_plugin_info(void);            // 清单（编译期内嵌）
int32_t            ps_probe(const uint8_t* head, size_t n);
int32_t            ps_open(const char* cfg_json, PsSink sink);
int32_t            ps_decode(PsCtx* ctx, const uint8_t* buf, size_t n, PsBatchOut* out);
void               ps_close(PsCtx* ctx);
int32_t            ps_abi_selftest(void);
```

- `PsBatchOut`：**host 预分配**，插件零拷贝写回；失败时**插件保留所有权、host 不释放**（成功才转移）。
- 结构体带 `abiVersion` + `reserved[8]`，为后续扩展留位。

**三种写入粒度（照 PJ4，缺一不可）**

| 粒度 | 用途 |
|---|---|
| `append_record`（按名） | 通用、低频 |
| `append_bound_record`（预绑定 handle） | 热路径，免查表 |
| **`append_arrow_stream`** | 列式批量（CSV/Parquet/ULog），零拷贝整批进 |

**封装块归宿主**：插件只 push 行，宿主 `flushPending` 时按 `Chunk<8192>` 封块提交。

**宿主面已实现（M0，已在 `ps_kernel.h` 里）**：`ps_csv_load`、`ps_dataset_channel_count`、`ps_dataset_channel`、`ps_dataset_free`、`ps_lod_minmax`。上面 5 个插件面函数随第一个 Loader 在 M1 落。

**四种后端**

| 后端 | 吞吐 | 何时用 |
|---|---|---|
| builtin | 200~500 MB/s | 我们自己的解析器 |
| dylib（in-proc） | 200~500 MB/s | 成熟第三方 C/C++/Rust |
| sidecar（进程外） | 100~300 MB/s | Python/Node/dotnet **私有格式**（崩了不拖主程序） |
| wasm | 中 | 不可信第三方，需沙箱 |

**准入与分发（三条硬要求）**

1. `plugin check` **必须在子进程里 `dlopen`**——坏 `.so` 一加载就 `abort`，否则主程序/CI 被拖死。
2. 插件清单**编译期内嵌进二进制**，不用外部 JSON（杜绝不同步、被手改）。
3. 闸门分两级：**`abiVersion` 挡不兼容**、**`minSdkVersion` 挡"缺能力"**；宿主版本号不参与 gate。

**双引擎 lockstep**：流式暂停时写入切副引擎、恢复切回；`ensure_topic/field` 两侧用**同一 id** 重放，保证插件缓存的 handle 切换后仍有效（对应"冻结视图 ≠ 停止采集"）。

**坏值策略**：解码层严格（插件解码错必须定位）；存储层宽松（装不进列类型 → 存 null + 每字段每 host 告警一次，不整份报废）。

---

## 11. 性能预算（同进程，端到端 ≤22 ms）

| 阶段 | 预算 |
|---|---|
| 收包 → 解码 | ≤0.5 ms |
| 攒批写入 host 缓冲 | ≤0.3 ms |
| GUI 取 batch | ≤0.2 ms |
| 查询 + LOD | ≤3 ms |
| 顶点上传 | ≤1 ms |
| 绘制 | ≤16.7 ms |
| **合计** | **≤22 ms** |

四条铁律：批量跨 ABI；UI 线程不阻塞；降采样在 Rust 侧完成；曲线 1px 关 AA。

---

## 12. 工程结构与里程碑

```
plotscope/
├── CMakeLists.txt                 # 只做三件事：设选项、加子目录、install
├── docs/                          # ★ 设计文档与可评审原型（随仓走，不进安装包）
│   ├── PlotScope-架构设计-定稿-v1.0.md
│   └── prototype/PlotScope-工作区原型.html
├── cmake/                         # 自写 CMake 模块（不用宏库）
│   ├── Rust.cmake                 # 内核导入：优先 Corrosion，离线退化 cargo 直调（接口一致）
│   ├── FindJKQTPlotter.cmake      # 上游没有官方 CMake 包，得自己找
│   ├── Warnings.cmake             # /W4 + -Wall -Wextra，内核头文件降级为 system
│   ├── Version.cmake              # 版本号单一来源，生成 version.h
│   └── Install.cmake              # 安装布局：bin / plugins / function / share
│
├── src/
│   ├── kernel/                    # ★ Rust 内核源码：Cargo workspace 根（不是编译产物）
│   │   ├── Cargo.toml             # workspace + members
│   │   ├── ps-abi/                # D9 的 5 个函数 + PsBytes/PsBatchOut + abiVersion + selftest
│   │   │   └── include/ps_kernel.h # 唯一对 C++ 暴露的头文件
│   │   ├── ps-ndf/                # NDF 列存、Arrow IPC、mmap、LOD 桶
│   │   ├── ps-parse/              # Loader 侧解析：ULog / DFLog / CAN / MDF4 / GNSS
│   │   ├── ps-ops/                # 向量算子（add/mul/filter/quat…）+ Lua userdata 元表
│   │   ├── ps-script/             # Lua 5.4 宿主：沙箱、指令预算、内存上限、热重载
│   │   ├── ps-align/              # 多源时间对齐（就近/线性/零阶保持）
│   │   └── target/                # cargo 产物目录（.gitignore）
│   │
│   ├── app/                       # ★ C++/Qt6 Widgets 应用
│   │   ├── main.cpp
│   │   ├── shell/                 # Ribbon、DockHost、Workspace、页签栏
│   │   ├── pages/                 # Page = QSplitter 树，叶子装配
│   │   ├── plot/                  # Canvas、Axis、CurveLayer、游标/框选/LOD 调度
│   │   ├── inst/                  # 地平仪、罗盘、表盘、告警灯（QPainter 自绘）
│   │   ├── panels/                # 数据源树、属性面板（QObject 选中 → 属性模型）
│   │   ├── proto/                 # 协议编辑器：Loader 的 options 子窗口（帧模板/位域/CRC）
│   │   └── bridge/                # 只调 ps-abi 的封装层 + QObject 信号适配
│   ├── plugin/                    # ★ 官方 Loader 插件（C ABI 动态库，产品代码而非示例）
│   │   ├── builtin_ulog/          # 每个家族一个目录：源码 + manifest.json + 测试样例
│   │   └── …
│   ├── function/                  # ★ Lua 脚本与算子 schema（随安装包分发）
│   │   ├── ops/*.lua + *.yaml     # 一算子一脚本 + 一份 schema（宿主据此生成参数面板）
│   │   ├── batch/*.lua            # 批处理：重采样、导出、批量单位转换
│   │   └── lib/ps.lua             # 对 ps.ch() 等宿主 API 的薄封装
│   └── 3dparty/                   # ★ 只收"必须锁版本 / 必须打补丁"的源码
│       ├── jkqtplotter/           # 画布首选（git submodule + 我们的小 patch）
│       └── lua-5.4/               # 脚本引擎（保持原样，不打补丁）
│                                  # 其余依赖走 FetchContent，不进仓
│
├── tool/                          # ★ 开发/发布工具，全部命令行
│   ├── ps-plugin-check/           # Qt-free 子进程：dlopen 准入检查（照 PJ4 的思路）
│   ├── ps-bench/                  # 帧时间/吞吐基线，输出机器可读结果
│   ├── ps-pack/                   # 打包：windeployqt + 签名 + 生成 checksum
│   └── ps-icon/                   # 资源与图标生成（SVG → ico/png 多尺寸）
│
└── test/                          # ★ 五类，别混在 src 里
    ├── unit/                      # C++ 单测（Qt Test）：拆分树、属性模型、页签、撤销
    ├── kernel/                    # cargo test：解析器对拍、算子数值、对齐边界
    ├── golden/                    # 样例 → 期望产物（哈希清单 + 小样本，二进制库单独放）
    ├── perf/                      # 性能回归：22 ms 预算、10M 点加载、20 通道 60 fps
    └── fixture/                   # 小体积测试日志（每个 ≤ 2 MB，不含版权数据）
```

**目录规则（不是审美，是边界）**

| 规则 | 后果如果不遵守 |
|---|---|
| C++ 与 Rust 都放 `src/`，**按语言分目录**（`app`/`plugin` vs `kernel`），不设两个顶层代码根 | 两套"代码根"让新人分不清产品入口在哪；`src/kernel/` 当 Cargo workspace 根完全合法，代价只是 `target/` 落在 `src/kernel/target`、CI 缓存路径跟着改 |
| `src/app` 只允许 `#include "ps_kernel.h"`，不得 include 内核内部头 | 一旦 C++ 直接依赖 Rust 内部类型，C ABI 就白设计了，未来换内核实现等于重写 UI |
| `src/plugin` 是**官方解析器**（随包发），第三方插件源码不进仓 | 官方与第三方混住，会让"插件准入检查"这条安全边界失效 |
| `src/3dparty` 只放锁版本/打补丁的库 | 什么都往里塞，仓库三年后 2 GB，clone 一次十分钟 |
| `src/function` 只放 `.lua` 与算子 schema，不放 C++ | Lua 脚本要能热重载、能被用户改，和二进制解耦才能做到 |
| `test/` 五类分目录，`golden` 用哈希清单 | 二进制样例堆进 git，仓库体积按周膨胀 |
| `docs/` 放设计文档与单文件原型，与控制台代码同仓 | 文档离代码越远越快过期；单文件 HTML 保证 clone 下来双击就能评审，不依赖本地服务 |

**CMake 目标（4 个，不多）**

```cmake
plotscope_app        # 可执行：main.cpp + src/app/**（瘦壳，不含业务实现）
plotscope_core       # 静态库：pages/plot/inst/panels/proto，app 链接它
ps_kernel            # Rust staticlib 目标（src/kernel/ps-abi，Corrosion 或 cargo 导入）
ps_plugin_<family>   # MODULE 库：src/plugin/<family>/，输出到 bin/plugins
```

选项：`PS_BUILD_TESTS`（默认 ON）、`PS_ENABLE_LUA`（默认 ON，关掉则只留内置算子）、`PS_USE_QWT_FALLBACK`（M0-0 对照组）、`PS_PLUGIN_DIR`（默认 `bin/plugins`）。

**M0 已落地（2026-11-12，仓库 main `fceab7b`）**

- `cmake/Rust.cmake` 改为 cargo 直调：离线机器无网，Corrosion 的 FetchContent 拉不下来；有网时换回 Corrosion 只改这一个文件，对外接口（`ps_kernel` 目标 + `ps_kernel.h`）不变。
- 内核 6 个 crate 已建：`ps-abi`（`ps_abi_version` / `ps_kernel_version` / `ps_abi_selftest`）、`ps-ndf`（序列 + min/max 抽稀）、`ps-ops`（scale / add / moving_avg）、`ps-align`（最近邻 / 线性）、`ps-parse`（ULog/CSV/BLF/NMEA 嗅探）、`ps-script`（预算占位）；`cargo test` 14 例全绿。
- 主程序：Ribbon 七页签 + 四可停靠区（数据源 / 属性 / 脚本 / 日志）+ 页签式绘图区；自绘画布支持滚轮缩 Y（锚指针）、拖动平移 Y、双击复位、十字光标；状态栏直读 `ps_abi_version()` 与自检结果，证明 C ABI 已打通。
- 数据面已开：`ps_csv_load` / `ps_dataset_channel_count` / `ps_dataset_channel` / `ps_dataset_free` / `ps_lod_minmax`；`PsChannelInfo` 的 `time/value` 指针直指内核内存（UI 零拷贝，数据集释放后失效）。`ps-parse` 的 CSV 解析支持 `, ; \t` 三种分隔符与 ns/us/ms/s 时间单位识别，坏单元格记 NaN 不整份报废。
- 降采样落到 Rust：UI 每次重绘按像素宽度调 `ps_lod_minmax`，只拿 ≤px 个 `(x,min,max)` 三元组；3000 点 CSV 画到三格（每格一个通道，避免混量纲压成直线）。
- 内核单测 19 例（`cargo test`），`ctest` 2/2 通过（ABI 冒烟 + cargo）。
- 未接：JKQTPlotter（可选后端，未进构建）、Lua 5.4、ULog/DFLog 等真实 Loader、NDF 落盘、多 Y 轴、仪表自绘、`.psl`。

**Ribbon / Dock spike（2026-11-12 实测）**

- Ribbon：SARibbon 2.9.6 跑通（MIT、Qt 6.2.4、静态库单 target、24 MB 源码）。实测七页签 + 分组 + 右侧 qchip 位（`rightButtonGroup`）都可用；展开高度 **97 px**、折叠 **28 px**（原型 74 / 32 px）。静态库需 `Q_INIT_RESOURCE(SARibbonResource)`；观感偏 Office，要贴原型必须重写 QSS + 自绘图标。
- Dock：`QDockWidget` 直接够用（四区 + 浮动 + `tabifyDockWidget` + `saveState/restoreState`），零第三方许可义务；Qt-ADS（LGPL-2.1）留作 M1 升级项；KDDockWidgets 含 GPL，排除。
- 取舍：Ribbon 承认"多 23 px 换键盘导航/溢出折叠/无障碍"；QSS 压缩目标 ≤84 px。达不到就退回自绘（现有 `QTabBar + QToolBar` 骨架已在 `src/app/shell`）。

**已实装（2026-11-12 晚）**：SARibbon 2.9.6 源码裁到 3.3 MB（去掉 `docs/`、`example/`、`pyqt6/`、`pyside6/`、`sip/`、`tools/`、`tests/`）放进 `src/3dparty/saribbon`，`src/3dparty/CMakeLists.txt` 里强制 `SARIBBON_BUILD_STATIC_LIBS=ON` / `SARIBBON_BUILD_EXAMPLES=OFF` / `SARIBBON_USE_FRAMELESS_LIB=OFF`（frameless 要 QWindowKit 子模块，内网拿不到），`add_subdirectory(... EXCLUDE_FROM_ALL)`，app 链 `SARibbonBar::SARibbonBar`。`MainWindow::createRibbon()` 从自绘 `QTabBar + QStackedWidget(QToolBar)` 换成 `SARibbonBar`：7 页签 / 文件页三组（布局·导出·截图）/ 右侧状态 action。`RibbonStyleLooseSingleRow` + tab 22 px + panel 标题 10 px → **84 px**（原型 74 px，目标 ≤84 达成；同参数下 CompactTwoRow 94 px、CompactSingleRow 62 px 但组标题会挤压换行）。**已知缺口**：单行模式下每个按钮左侧留图标位，没图标就是空方块——必须补一套自绘 SVG 图标（约 22 个），否则观感不成立。

**M0-0 画布 spike（2026-11-12 实测，改判 D3）**

环境：Qt 6.2.4、4 vCPU 云主机、软件光栅、`QT_QPA_PLATFORM=offscreen` 下 `QWidget::render`；数据 20 通道 × 8000 点，视口 1500×900，预热 3 帧后取 30 帧均值。JKQTPlotter 5.0.0（HEAD `9e9f0af`）静态编译接入。可复跑脚本：`test/perf/canvas_spike/`。

| 方案 | 单帧 |
|---|---|
| 自绘：Rust LOD → 1500 桶 + `drawPolyline`，AA 关、线宽 1.0 | **7.2 ms** |
| 自绘：同数据，AA 开 + `QPainterPath`、线宽 1.3 | 915 ms |
| JKQTPlotter：原始 16 万点/帧（AA 开关无差别） | 60–64 ms |
| JKQTPlotter：喂同样的 LOD（20×1500） | 13.1 ms |

1. 决定 60 fps 的不是图表库，是"1px + 关 AA + LOD"。→ 已写成 §11 铁律 4。
2. JKQTPlotter 过不了 ≤8 ms 的门（13.1 ms），但仍在 16.7 ms 帧预算内。所以它从"首选"降为"可选后端"：当我们确实需要它的坐标轴/图例/框选/导出时才接，且数据必须由 LOD 喂，不能把原始点扔给它。
3. 两条路都做不了抗锯齿平滑曲线（AA 下都超预算一个数量级以上）。真要平滑，换 `QOpenGLWidget`/QSG 批量上传顶点，另立 spike；否则接受 1px 折线这种工程软件惯用观感（PlotJuggler 也这么画）。

**安装布局**（安装器与便携版共用一套）

```
bin/PlotScope.exe
bin/plugins/*.dll          # 官方插件
function/*.lua             # 可热改
share/psc/{ops,icons}/     # schema 与资源
docs/                      # 只放许可与第三方声明（原型与设计文档不进安装包）
```



| 里程碑 | 内容 | 工期 |
|---|---|---|
| **M0-0** | 选型 spike：JKQTPlotter vs Qwt，20 通道 × 8k 点 @60fps，单帧 ≤8 ms | **0.5 天（已完成：自绘 7.2 ms 达标；JKQ 13.1 ms，见 §12）** |
| **M0** | 骨架：按 §12 目录落地、CMake + Rust 内核接通、窗口/停靠/页签、CSV 加载、NDF、单画布 | 3 周 |
| **M1** | ULog + DFLog + CAN 家族 + MAVLink；缓存层；插件 ABI + 脚手架 | **3.5 周**（CAN 进则 6.5） |
| **M2** | 派生通道增量算子、脚本、MDF4、GNSS 解析、实时滚动面板自绘 | 4 周 |
| **M3** | MCAP/rosbag2、多画布/多轴、时间对齐、视频同步 | 4 周 |
| **M4** | 协议编辑器（Loader 的 options 子窗口：位域/CRC/联动）+ 模板编译 | 3 周 |
| **M5** | 仪表页、布局 `.psl`、导出 | 2 周 |

**页面/面板形态**：单窗口多页（页签挂在绘图区顶部，只跨绘图区宽度），每页一棵切分树；停靠面板可浮动；页数不做硬限制，按内存策略回收。

---

## 13. 待拍板（8 条）

| # | 事项 | 影响 | 我的建议 |
|---|---|---|---|
| 1 | **CAN 家族是否进 M1** | M1 3.5 ↔ 6.5 周 | 若"车辆"是首期市场就进，否则推 M2 |
| 2 | 时间对齐（多源 time sync）是否进 M2 | 2~3 周 | 进。这是本域最高频诉求 |
| 3 | 视频与曲线同步回放 | M2 末 vs M3 | M3，先保证曲线 |
| 4 | 脚本语言范围 | M1 表达式（Lua 语法糖）vs 同开完整 Lua | 先上表达式（自动包成 Lua），M2 开完整脚本 |
| 5 | 协议编辑器位域深度 | 全做 +1 周 | 全做（跨字节位域 + CRC 自动校验是我们的卖点） |
| 6 | 与 LCMS 的边界 | 是否共用登录/权限/文件服务 | 只共享"项目/文件"概念，不共用实现 |
| 7 | 开源节奏 | 一次性开源 vs 里程碑开源 | 里程碑开源（M1 后先发加载器 + 绘图） |
| 8 | enum 字典所有权 | 文件里有就用文件，YAML 只能覆盖/加别名？ | 是。文件优先，YAML 覆盖 |

---

## 附录 A：必须守住的不变量（摘 11 条）

1. 一个 `Chunk` 内所有通道共享同一根 `t`。
2. `gap` 位只表示"不可采信"，绝不表示 0，也绝不插值。
3. 同一 `applicationId` 的 enum 字典不可变。
4. LOD 桶必须按时间顺序、互不重叠、覆盖全部采样。
5. 任何变换若为非单调或多输入，进 LOD 前必须物化。
6. 跨 ABI 的数据必须批量、且所有权归属明确（含失败路径）。
7. 内核头文件不得出现任何 Qt 类型。
8. 布局文件缺通道时保留"幽灵曲线"，不静默删除。
9. 缓存指纹不匹配时必须**完整重解析**，不得部分复用。
10. 插件加载失败必须可归因（ABI/SDK/平台/清单四类之一）。
11. 任何 UI 线程调用内核接口都必须是非阻塞的。

## 附录 B：术语

| 术语 | 含义 |
|---|---|
| NDF | PlotScope 自研列存中间态（`Chunk<8192>` + gap 位图 + LOD） |
| LOD | 细节层级；本项目中特指 min/max 分桶降采样 |
| L1/L2 | L1 显示单位（轴级）、L2 通道元数据变换（factor/offset） |
| `.psl` | 单次分析布局（JSON，可分享） |
| `.psc-workspace` | 项目级工作区（含数据源引用与缓存索引） |
| ghost curve | 布局引用的通道在本次数据中不存在时保留的空壳 |

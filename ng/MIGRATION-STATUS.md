# 老架构 → ng 搬迁状态对照清单

> 用户要求："原来有的都要；如果不打算搬，请给出理由。"  
> 本清单逐 module 列出老代码 `src/application` 的用户可见功能点，标注 ng 现状与处置。  
> 图例：✅已搬 / 🔄部分 / ⬜本轮补 / ❌不搬（带理由）

盘点基线：datasource ~108、plotting ~135、scripting ~57、protocol ~66（功能点，含跨类复用）。

---

## 0. Ribbon 页划分（用户明确要求 8 页）

老代码 `src/` **实际只注册 6 页**：文件、数据、绘图、协议、分析、视图。  
"开始"和"脚本"页在 `src/` 中**无注册**（脚本只在设计原型 HTML 与 roadmap 规划中出现）。

| 页  | 老代码           | ng 现状                                           |
| -- | ------------- | --------------------------------------------- |
| 开始 | ❌老代码无         | ✅按用户要求新增（打开数据 + 外观）                           |
| 文件 | ✅             | ✅归位：文件(打开/导入布局/设置) / 导出(保存布局/导出设置/导出数据) / 截图 / 帮助(关于) |
| 数据 | ✅             | ✅归位：常用(快速打开/最近/重载/导出/清缓存) + 来源 gallery(文件格式) + 调试(协议检查) |
| 绘图 | ✅             | ✅归位：页面(新建页) / 布局(网格/增删/左右/上下) / 显示(网格/图例) / 画布操作(放大/缩小/自适应) / 图表类型 gallery / 曲线与轴 / 操作 |
| 脚本 | ❌老代码无（原型+规划有） | ✅按用户要求新增：运行脚本                               |
| 协议 | ✅             | ✅归位：编辑器(新建/打开/校验/协议编辑器) + 检查(协议检查器)          |
| 分析 | ✅             | ✅归位：分析应用 gallery（全部应用/最近使用）                  |
| 视图 | ✅             | ✅归位：各 dock 面板开关 + 外观(切换主题)                   |

> 页/组/action 顺序由 `UiSlot.order` + `UiAction.order` 显式表达（升序，同权重按 id 稳定），
> 与模块/工具箱装载顺序完全解耦；抄老 `RibbonContribution.order` + panel/items 数组顺序的体验。

---

## 1. datasource module（~108 点）

### 已搬 ✅

- 数据加载意图（data.load）、重载、导出、清缓存、最近文件（宿主级 handler）
- 文件格式 gallery（csv/ulog/custom-protocol 等驱动动态生成）
- 派生通道（data.derive：滤波 + 四元数→RPY），存 Workspace 不写回原集
- DataSource 配置持久化（settings）、Dataset 树数据源面板

### 本轮补（UI 形态）⬜

- **数据源面板改为老代码树形形态**：每行一条数据源，点击展开为 dataset 树  
  （字段 / 参数 / 消息 / topic 四类）。现有实现是"卡片+chip+检查器"，形态不对。
- 源行右键菜单：重命名 / 重载 / 时间对齐 / 对比同名通道 / 数据源信息 / 移除
- 字段树：拖拽到画布、双击加曲线、右键派生/画到画布/删除派生
- 消息表右键：添加时间书签

### ❌不搬（带理由）

| 功能                                                          | 理由                                                                                                                                                                 |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| 文件/UDP/TCP/串口 Transport 的完整参数对话框（17 项）                      | SPI v2 已把"传输 + parser + 参数"抽象为 `openPath`/`connectSource` 意图 + 各格式驱动的 schema；GUI 复用同一套 schema 渲染。逐个 transport 的旧对话框是**同一机制的具体参数**，不是独立功能，由格式驱动声明参数即可，不在这层重写 4 套控件。 |
| 解析缓存（DatasetCache SHA256/.pscache/4GiB 淘汰）                  | 缓存是**性能实现细节**，不属于用户可见功能；ng 的 Rust kernel + Dataset 门面已按需取数，缓存策略留给 kernel 层，UI 只暴露"清缓存"一个入口（已搬）。                                                                    |
| RAW/MCAP 录制（StreamRecorder/McapWriter/PsCaptureFormat，17 项） | **录制在 recording 层**（`ng/recording` 已建），是独立关注点；ds module 只提供"连接后录制"的参数入口，录制实现不重复搬。                                                                                  |
| 自定义 .ui 对话框引擎（UiDialogEngine）                               | 属于 DataSourceDialog 的实现机制，非独立用户功能；schema 驱动的参数编辑已覆盖。                                                                                                               |
| 设备调试终端（DeviceDebugDialog）                                   | 实时源调试是低频专业功能，本轮保留接口位（源行"调试终端"按钮），对话框实现列为 P2；理由：无真实 transport 接入时无法验证，先不造空壳。                                                                                        |

---

## 2. plotting module（~135 点，用户体验大户）

### 已搬 ✅

- 12 种图型 gallery（timeseries/xy/scatter/bar/histogram/fft/traj3d/map/surface3d/horizon/compass/gauge）
- 多曲线叠加（同一坐标轴多条曲线，强需求）
- 双 Y 轴（用户选择挂左/右轴）
- 多画布 / 拆分 / m×n 网格 / 页面管理
- plot.channel（像素级 LOD）/ plot.instrument（仪表有界取数）
- 导出 PNG / CSV、图例开关

### 本轮补（操作意图）⬜

| 操作                                      | 老代码                        | ng 落点                              |
| --------------------------------------- | -------------------------- | ---------------------------------- |
| 曲线显隐/颜色/线宽/绑定轴/删除                       | 曲线右键菜单                     | view action `plot.series.*` + 右键菜单 |
| 轴范围/字号/线宽/颜色/网格 X/Y                     | 属性面板 + setAxis*            | view action `plot.axis.*`          |
| 标记（时间书签）增删改/跳转                          | Shift+左键 / [ ]             | view action + 快捷键                  |
| 缩放 in/out、平移、自适应、双击复位                   | 滚轮/框选/工具                   | 画布鼠标事件（已有基础，补齐工具按钮）                |
| A/B 测量                                  | Ctrl+左键                    | view action `plot.measure.*`       |
| 播放头                                     | app.playhead / setPlayhead | view action `plot.playhead`        |
| 画布 hover 读数 + 同页联动                      | hover 十字                   | PlotCanvas 已有基础，补联动                |
| 悬浮工具栏（增/拆/最大化/测量/删）                     | HoverBar                   | CanvasWorkspace 工具按钮               |
| 快捷键组（Delete/Esc/P/空格/Ctrl+0/+/-/←→/[ ]） | keyPressEvent              | 画布快捷键                              |

### ❌不搬（带理由）

| 功能                                   | 理由                                                                                                         |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------- |
| PlottingCanvas.cpp 3800 行的具体绘制算法逐行平移 | 用户明确"架构不抄老代码"；绘制已由独立 `fastplot` 库承担，且术语已对齐 MATLAB/matplotlib（Figure/Axes/Line/Artist）。平移旧绘制代码等于把已解决的事再做一遍。 |
| 3D 曲面（surface3d）/ GPS 地图（map）的完整渲染   | 老代码手册已标注"未完成"；ng 保留 gallery 入口与取数路径，渲染列为 P2，不造半成品。                                                         |
| SeriesEditDialog 的图形化变换算子编辑器         | 派生通道已由 `data.derive` 意图 + schema 参数编辑器覆盖；老 1749 行胖 UI 不平移。                                                 |



---

## 3. scripting module（~57 点）

### 已搬 ✅

- Lua 引擎（脚本运行入口 script.run）

### 本轮补 ⬜

- 脚本页 ribbon（脚本控制台 / 运行脚本）

### ❌不搬（带理由）

| 功能                                                                                                                               | 理由                                                                                             |
| -------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| REPL 元命令（:help/:clear/:who/:whos/:run/:reload/:clc）、ScratchPlot、ps.table/ps.plot 结果面板、500 行截断等（ConsolePanel/ScriptConsole ~14 项） | 用户明确"无脚本编辑器，编辑器一律外部"（老 roadmap 的非目标）。控制台 REPL 是编辑器形态，属被砍范围。ng 保留 `script.run` 意图供 CLI/外部编辑器调用。 |
| Lua 计算库 9 入口（stats/rms/fft/psd/filter/decimate/...，转 Rust kernel）                                                                | 这些是**计算算子**，由 Rust kernel 提供，脚本通过绑定调用；不是 scripting module 的 UI 功能，不重复搬。                        |
| 沙箱清库/64MB 内存闸/2000 万指令预算/受限 require                                                                                              | 引擎安全机制，`ng/modules/script/LuaEngine` 已沿用同类约束；属实现细节，非用户可见功能。                                    |
| batch/ops 示例脚本（ps.grid/ps.write/ps.resample）                                                                                     | 老 roadmap 明确标注"4 函数未注册、愿景脚本"，未落地功能不搬。                                                          |

---

## 4. protocol module（~66 点）

### 已搬 ✅

- `ng_protocol` 纯逻辑库（ProtocolDocument/Spec/SequentialCodec）
- SPI v2 `CustomProtocolParser`（configure/feed/finish）
- 薄编辑器对话框（新建/打开/保存/校验/试算）
- test_protocol 钉死 validate/往返/求值/帧解码/SPI 闭环

### 本轮补 ⬜

- protocol module 外壳 + 协议页 ribbon（新建/打开/校验 + 检查器）

### ❌不搬（带理由）

| 功能                                                                            | 理由                                                                                |
| ----------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| 五页签胖编辑器（协议/设计/系列/JSON/Lua，16 列字段表、段拖拽、变体表、字节预览，27 项）                          | 用户曾明确"1700 行胖 UI 不平移"；ng 薄编辑器以 JSON 文档为权威表示，字段/变体编辑走属性 schema，不重造 5 页签布局。         |
| 旧 offset 帧协议（frame_size/length/sync/crc16_ccitt/msgid/variants）               | 新架构已统一到 `layout:"sequential"` 顺序协议 + SPI v2；旧 offset 帧是待淘汰格式，保留数据兼容但不再做编辑器入口。     |
| CustomProtocolLoader ABI vtable（kFrameLoader/FrameInputVtable/kFrameProtocol） | SPI v2 的 `IMessageParser`/`CustomProtocolParser` 已取代 C ABI vtable，是同功能的新接口，不重复实现。 |

---

## 5. analysis module（少量）

### 已搬 ✅

- 无（module 外壳不存在）

### 本轮补 ⬜

- analysis module 外壳 + 分析页 gallery（全部/最近/收藏）
- 分析应用条目：默认曲线 / 分析报告 / 飞控复盘（来自 autopilot 扩展的领域知识）

### ❌不搬（带理由）

| 功能                              | 理由                                                                                                           |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| 飞控复盘面板（姿态/位置/电源/GPS 分页）的具体业务 UI | 分析应用属**领域工具箱**（第 3 底座），不是通用 modules；ng 只提供 analysis 调度机制 + gallery 外壳，具体复盘面板由 autopilot 领域扩展贡献，符合"器官不懂业务"红线。 |

---

## 6. shell 框架

### 已搬 ✅

- 意图层（Intent/IIntentHandler）、Contribution 系统、UI 插槽（UiSlotRegistry）
- ADS docking、快捷键（作为 action 属性）、主题切换

### 本轮补 ⬜

- 8 页 ribbon 归位
- 视图页面板开关（dock.toggle）

### ❌不搬（带理由）

| 功能                                                                            | 理由                                                                                   |
| ----------------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| 老 CommandManager/SettingsManager/SessionManager 的 26 个 Manager/Service/Bridge | ng 已概念收敛为 5 个抽象（Dataset/Intent/Contribution/Module/ToolPack），这是架构优势，不回退到老 Manager 堆。 |

---

## 7. 统一 Selection + 属性面板 + 变换体系（本轮补齐的两处缺口）

用户指出："属性面板 + 通用 selection 仍然没有——dock.right 只有 CAN 面板；plot.selection.changed 只有曲线选择，没有'当前对象：源/表/列/卡片/轴'统一模型。"并给出 Lua 三场景（衍生 / 曲线轴变换 / checksum）+ 自说明 + 在线调试需求。本轮全部补齐。

### 已搬 ✅

- **统一 Selection 模型**（`core/Selection.h`）：kind = 源/数据集/表/列/参数/消息/曲线/轴/协议字段/画布，纯数据（QVariantMap），headless 与脚本可读；统一 topic `core.selection.changed`（旧 `plot.selection.changed` 留作兼容别名）。由 `AppContext` 持有，视图之间星形解耦。
- **通用属性面板**（`gui/views/PropertyPanel.cpp`）：`dock.right` 默认面板（**替换原 CAN 面板的默认占用**，用户决策）。它是 `PropertySchema` 契约的纯视图，按 selection.kind 找 `PropertyProvider` 拿 schema、找 `PropertySetter` 写回；零业务类型知识。
- **属性提供者**：数据源树（源/数据集/表/列/参数/消息）、画布工作区（曲线/轴/画布）都已注册 provider + setter。点选即联动。
- **统一变换注册表**（`core/Transform.h`）：每个算子/表达式/Lua 都是 `TransformDescriptor`，带**能力位**（Derived/AxisX/AxisY/Checksum）+ 自说明 + 示例 + 参数 schema。UI 按能力过滤，运行时 evaluator 再校验一次——"选错就死了"被双保险挡住。
- **变换编辑器 + 在线调试**（`gui/shell/TransformEditorDialog.cpp`）：按能力过滤选变换、展示自说明、填参数/写表达式/Lua；**单点(live)** 与 **批量(offline)** 两种在线调试。
- **三场景接入**：
  - 数据源衍生：列右键 → 变换编辑器（CapDerived）→ `data.derive` 带 transform 请求；
  - 曲线：曲线右键 → 属性/Y 轴变换（CapAxisY），X 轴时间 offset 在画布副本上施加，原始数据零改动；
  - 协议 checksum：编辑器"Checksum 变换"按钮（CapChecksum）。
- **表达式求值**（Rust，`ps-ops/src/expr.rs`）：零依赖递归下降，`5*x+10/sin(x)` 类表达式，编译一次逐点求值；作为 filter `"expression"` 并入既有 ABI，三条 Rust 路径自动支持。
- **Lua 变换**（C++，`modules/script/LuaEngine.cpp` 的 `LuaTransform`）：`transform(x, ctx)` 约定，支持单点/批量，`ctx.t/index/params`；与原有 `ps.*` 脚本门面隔离。

### 执行位置决策（用户 3 次澄清后定稿）

| 变换类型 | 执行位置 | 原因 |
|---|---|---|
| 内置算子（负号/比例/滤波/微分…） | **Rust kernel**（ps-ops/filters） | 数据源侧运算都在 Rust，有缓存、高效 |
| 数学表达式 | **Rust kernel**（ps-ops/expr） | 同上；单点 live 友好，批量是循环，有缓存无需向量化 |
| Lua 代码 / 脚本 | **C++**（TransformEvaluator → LuaTransform） | checksum / 曲线的 Lua 便于在线调试单点/批量 |

数据不可变铁律：parser 解析完直接入 Rust Dataset，**原始数据不允许改**；变换（衍生/提取前预处理）都是只读副本，结果作为 generated series 叠加，不写回原集。

### 验证

- `test_transform`（headless）：Selection 往返、能力过滤、**表达式走 Rust 结果正确**、**内置算子取负正确**、**Lua 走 C++ 带 ctx 正确**、能力错配被挡（ecap）。
- `test_gui`：dock.right 主面板为属性面板、property-panel 自注册、选中画布→属性面板按 provider 显示 schema。
- 截图：`docs/screenshots/ng-properties-panel.png`、`ng-transform-editor.png`、`ng-transform-checksum.png`。

---

## 8. Ribbon 页/组/action 顺序对齐老设计（本轮补齐）

用户指出："ribbon 上 action 的顺序，还有分组也要参考我以前老的设计。"此前 ng 的 action 完全没有排序机制——
`actionsIn()` 按绑定先后返回（工具箱来源在前、骨架来源在后），顺序取决于模块装载顺序；group 的 order 全是默认 0，
靠声明顺序碰巧对上。本轮把顺序变成可控、可测的显式契约。

### 已做 ✅

- **UiAction 增加 `order` 字段**；`UiSlotRegistry::actionsIn()` 按 (order, id) 稳定排序，`slotsOfKind()` 按
  (order, label) 排序，`childrenOf()` 按 group.order 排序——与装载顺序彻底解耦。
- **8 页顺序**：开始/文件/数据/绘图/脚本/协议/分析/视图（相对关系抄老 RibbonContribution.order）。
- **文件页**抄老 ShellComponent：文件组(打开/导入布局/设置) + 导出组(保存布局/导出设置/导出数据) + **独立截图组** +
  帮助组(关于)；补 `settings.export` 落地为"导出设置为 JSON"（dispatcher 特判）。
- **数据页**抄老 DataSourceRibbon：常用组(快速打开/最近/重载/导出/清缓存) + 来源 gallery(常用/文件格式) +
  调试组(协议检查)；格式 gallery 分类顺序常用→文件格式。
- **绘图页**抄老 PlottingModule：页面组(新建页 page.new) + 布局组(网格 page.grid/添加/删除/左右/上下) +
  显示组(网格/图例) + 画布操作组(放大/缩小/自适应) + 图表类型 gallery(常用/曲线/地图3D/仪表，组内顺序照抄) +
  曲线与轴/操作两组(ng 追加，承载老右键菜单里的曲线操作)。
- **协议页**编辑器组顺序(新建/打开/校验/编辑器)抄老 ProtocolModule；**分析页** gallery 全部应用→最近使用。
- 诚实边界：老"移动"是工具模式开关、"边框"是 canvas flag，ng 无对应 view action，**不造空按钮**；
  "导出快照"与"导出 CSV"语义重复，合并为一个。

### 验证

- `test_gui` 新增 2a-4 段：钉死 8 页顺序、绘图页 7 组顺序、分组归属（新建页在页面组/增删拆分在布局组/
  自适应在画布操作组/截图独立成组）、组内 action 按 order 升序、gallery 分类顺序。Debug+Release 双 11/11 全绿。
- 截图：`docs/screenshots/ng-ribbon-{开始,文件,数据,绘图,脚本,协议,分析,视图}.png`，逐页核对分组与顺序。

# 脚本子系统：控制台 + 自动化

> 基线：`origin/main @ 2649a98`（2026-11-17）。本文只讨论 `modules/scripting`。

## 0. 本版定调（先看这 12 条）

1. **两条产品线，一个模块**。
   - **控制台（Console）**：人在场，MATLAB command 式 REPL，取任意通道随便算、即时看数看图；
   - **自动化（Automation）**：人不在场，批量加载/导出/录制出文件，且**可被事件触发**。
   两者共用对象模型、数据面、计算库、引擎；差别只在执行语义与产物归属。
2. **没有"算子"这个概念**。可复用的逻辑就是一个**普通的 `.lua` 文件**，放 `scripts/lib/`，用 `require` 调。不做文件头 schema、不做参数面板、不做算子注册表、不做版本机制——**版本由 git 管，参数就是函数参数**。这是本版删掉的最大一块：原来"算子"的独特价值是"能被曲线变换下拉选中并生成参数控件"，那个消费方不存在了，算子就退化成普通函数。
3. **不碰绘图侧的曲线变换**。曲线变换已有独立实现（`shared/filter` 的 `FilterCatalog` + 内核 `ps_filter_apply_series`），归 plotting/datasource，**不属本方案范围**。本模块不接管、不提供下拉项、不写 recipe。
4. **主入口是控制台**。对标 MATLAB command window：持久 workspace、表达式回显、多行续行、`clear/who/whos/help/run` 元命令。脚本文件不是另一个入口，而是控制台的 `run <file>`。
5. **自动化能被事件触发**。绑定走 `scripts/on/<event>.lua`（文件即配置）；P1 只做 `sourceLoaded` 一个触发点，其余留扩展口；默认关闭 + 重入保护 + 隔离 + 全局暂停。
6. **命令表不能当 API**：`data.*` 全部是 `void` lambda（`DataSourceCommands.cpp:35-70`），没有返回值、没有错误、没有进度、不可取消。两条线都必须走**返回 `Result` 的服务方法**。
7. **只有自动化需要作业模型**。自动化线引入 `Job`（状态/进度/错误/取消），不承诺全局回滚，改为"操作清单 + 补偿动作"；控制台同步执行，只受指令预算约束。
8. **句柄只需两级**：source / channel（`SourceData.id` 已有，通道名在源内唯一）。canvas 句柄只服务 P2 的"提升为正式曲线"。
9. **脚本不进 `.psl`**：脚本是独立目录（`ShellPaths/scripts`）里的普通文件，本模块不读不写工程文件。
10. **现有 stub 全未兑现**：`resources/scripts/batch/resample.lua` 用 `ps.grid/ps.channels/ps.write/ps.resample`，四个函数一个都没注册（`ScriptEngine.cpp:291-311` 只注册了 8 个）。API 面按这类脚本的真实需求定，否则示例是假的。
11. **语言只用 Lua**。Python 现有通道是"子进程 + JSON 全量序列化 + 15 s 超时"，拿不到宿主，做不了"操作所有数据"。Python 留作外部离线工具（§3.5 有逐项对比）。
12. **切片**：引擎搬迁与骨架 → 只读数据面 → **控制台（REPL）** → 自动化 Job + 事件触发 → 实时录制 → 批处理 →（P2）提升为正式曲线、子进程断点调试。

## 1. 现状基线（可核对）

| # | 事实 | 证据 | 影响 |
|---|---|---|---|
| 1 | 命令 handler 全是 `void`，无错误/进度/取消 | `CommandManager.h`（`Handler = std::function<void(...)>`）；`DataSourceCommands.cpp:35-70` | 自动化另建服务层，不复用命令表 |
| 2 | 无撤销栈 | `src` 全量搜 `QUndo` 零命中 | 不承诺回滚，只做补偿动作 |
| 3 | 加载是**同步阻塞**，无进度、无线程 | `DataSourceDock::loadFile`（`entry->load(...)` 直调，无 `QThread/QtConcurrent`） | 必须包成 Job，否则自动化一加载就冻 UI |
| 4 | 加载有解析缓存 | 同上（`DatasetCache::load/save`） | 自动化重复打开可命中缓存，批处理要吃这个便宜 |
| 5 | 数据面能力已有实现 | `DataSourceSession.h:36-92`：`loadFile/openPath/connectSource/connectLiveSource/closeSources/setSourcePaused/exportActiveCsv/reloadChannels` | 服务方法可直接复用，不需新造加载能力 |
| 6 | 录制已有独立落盘器（有界队列、丢批计数、后台线程） | `LiveSession::startRecording/stopRecording`、`StreamRecorder`（Csv/Raw/Mcap） | 自动化录制只需包状态与取消语义 |
| 7 | 实时源有溢出/丢批状态 | `LiveSession`（`kChannelCapacity=262144`、`overflow()`、`droppedBatches()`） | 脚本可查状态，别让脚本猜 |
| 8 | 脚本能力只剩纯函数 runner，无宿主对象 | `ScriptService.h`（`modules/runtime/include/`）、`ScriptComponent.cpp:34-40` 只 `registry.addScriptRunner(...)` | P1 新建宿主服务面；旧 runner 槽位整体退场 |
| 8a | 绘图侧曲线变换已独立实现，且不再走脚本 | `shared/filter`（`src/application/shared/filter/include/filter/FilterCatalog.h`，13 个内置算子）+ `ps_filter_apply_series`（`src/application/kernel/include/ps_kernel.h:65-68`） | 本方案不涉及 |
| 8b | 内核已有曲线级求值入口，指针进指针出，不走 dataset | `ps_filter_apply_series` | Lua 调 Rust 直接接这个，不需要新建 ABI |
| 8c | `extensions/builtin/transform` 已删除 | commit `993711c` | 方案里"算子执行归 transform"的说法作废 |
| 9 | Lua 沙箱已摘危险库，有 64 MB / 2000 万指令预算 | `ScriptEngine.cpp:313-321`、`trackedAlloc`（`:117-120`）、`lua_sethook(LUA_MASKCOUNT, 20000000)`（`:474`） | 安全基线可复用；缺取消标志、路径白名单、**受限 `require`**（`:315` 把 `require` 一起摘了） |
| 10 | Python 走子进程 + JSON，15 s 硬超时，无宿主 | `ScriptEngine.cpp:25-89`（`findExecutable("python3")`、`QTemporaryFile`、`:50-51` 逐点拼 `QJsonArray`、`:75` `waitForFinished(15000)`） | 不进主链路 |
| 11 | 批处理脚本是未实现的愿景 | `resources/scripts/batch/resample.lua` 用的 4 个函数均未注册 | API 面按它对齐，S5 才验收批处理 |
| 12 | 画布无稳定 id，`saveState` 不存派生样本 | `PlottingCanvas.h`、`saveState`/`restoreState` | "提升为正式曲线"留 P2 |
| 13 | 跨模块只能过 `ModuleBridge`（全 `MainWindow*` 形参） | `modules/runtime/include/ModuleBridge.h` | 新服务面不得再过此路 |
| 14 | 内置计算函数是 C++ 逐元素算，根本没走 Rust | `l_moving_avg`（`ScriptEngine.cpp:224-242`）从 Lua table `readVector` 逐点取、C++ 循环、再塞回 | 必须改走 `ps_filter_apply_series` 块级入口 |

## 2. 两条产品线

| 层 | 能力 | P1 | 说明 |
|---|---|---|---|
| **A 控制台** | | | 人在场，同步、可反复 |
| A0 数据访问 | 源/通道/元信息、按窗切片、多源对齐、交叉取数 | ✅ | 需要 source/channel 句柄 |
| A1 计算库 | stats / peaks / rms / derivative / integral / movingAvg / filter / resample / **fft / psd** / correlate / find | ✅ | 纯数字，不碰 UI；**全部落 Rust 内核**（§3.6），Lua 只调 |
| A2 结果呈现 | 数值表、临时图（scratch）、游标取值 | ✅ | 临时图**不进文档**、不存 `.psl` |
| A3 提升为正式曲线 | 把临时结果写进文档画布 | P2 | 需要 canvas 稳定 id + 快照事务 |
| **B 自动化** | | | 人不在场，产物落盘 |
| B0 数据作业 | 加载/关闭/重载、导出 CSV、缓存清理 | ✅ | 全部异步 Job |
| B1 实时与录制 | 连接/断开、暂停恢复、录制、丢批溢出查询 | ✅ | 同一 Job 模型 |
| B2 批处理 | 多文件循环、失败策略、汇总、续跑 | ✅ | 自动化的产出形态 |
| B3 事件触发 | `scripts/on/*.lua` 绑定，P1 只做 `sourceLoaded` | ✅ | 默认关闭，见 §2.5 |
| **C 脚本文件** | `.lua` 普通文件，`require` 复用，git 管版本 | ✅ | 没有 schema、没有注册表、没有参数面板 |
| **D 禁区** | 直接碰 `QWidget`、写内核 dataset、自由文件 IO、自由 `require` | ❌ | 永久 |

### 2.1 覆盖面矩阵（脚本能操作到哪一层）

结论：**业务对象与数据能力可覆盖约 80%，UI 交互永久不覆盖。** 判据是"这件事改的是文档/数据，还是改的只是呈现"。

| 类别 | 代表能力 | 脚本 | 前提改造 |
|---|---|---|---|
| 数据源生命周期 | 加载、关闭、重载、暂停/恢复、切换活动源 | ✅ A0 | 同步 `loadFile` 包 Job；`closeSources` 拆单源粒度 |
| 数据读 | 通道枚举、元信息、时间/量程、按窗切片、实时状态 | ✅ A0 | 切片走 `ps_lod_minmax`，禁止整条铺表 |
| 实时与录制 | connect/disconnect、pause/resume、record/stop、丢批查询 | ✅ B1 | 录制错误出参改 `Result` |
| 导出 | CSV、工程快照 | ✅ B0 | 显式源 + 路径 + 覆盖策略（现在只导活动源） |
| 批处理 | 多文件循环、失败策略、汇总、续跑 | ✅ B2 | 新写（现在只有假的 `batch/resample.lua`） |
| 派生通道 | 注册/删除 `quat`/`filter` 派生通道 | P2 | 走 datasource 的变换服务；脚本只调用，不实现算法 |
| 绘图对象 | 建页/画布、加删曲线、轴、书签、播放头 | P2 | canvas 稳定 id；书签走服务表（现为 `MainWindow*` 桥） |
| 协议 | schema 校验、编解码 | 可选 | `protocol.validate` 目前无参数版 |
| 设置项 | 读写强类型 key | P2 白名单 | `SettingsManager` 已具备，需 key 白名单与校验 |
| 工程文件 | 打开/保存/导入布局 | 改造后可行 | 现有 `file.open`/`layout.save` 会弹对话框，需无 UI 参数版 |
| 对话框与表单 | 设置窗口、协议编辑器、变换对话框 | ❌ | 脚本要的是结果，不是让脚本点控件 |
| 窗口呈现态 | Dock 布局、主题切换、视口缩放、选中态、工具按钮状态 | ❌ | 这些不进文档，脚本操作只会绑死在 UI 内部结构 |
| 菜单/快捷键本身 | 点菜单、按快捷键、模拟鼠标 | ❌ | 模拟输入不稳定且无必要（对齐 PJ4 的边界） |

两个推论：

1. **"能脚本化"≠"现在就能"**。不少业务动作只写在 UI 事件里（拖字段树加通道、右键菜单的变换 Apply、对话框的确认分支），服务层没有对应方法。要脚本化，必须先把这些分支下沉成带 `Result` 的服务方法——这是主要工作量，比写 Lua 绑定大得多。
2. **命令表不是覆盖面**。现有约 30 条业务命令里，弹窗类（`file.open`、`data.quickOpen`、`file.settings`、`protocol.new`）即使被脚本调用，也只会弹个窗等人点，等于卡死自动化。所以 `ps.app.command()` 必须白名单，且只放**无对话框**的命令。

### 2.2 自动化用法（产物是文件）

产出的东西是 **CSV / PNG / 汇总报告**，不是"在界面上点出一堆"。这条决定 API 按"产物"设计，不按"UI 动作"设计。

**A 批处理导出汇总（价值最高，第一优先）**

```lua
-- batch_export.lua：一个目录的架次 → 每架次关键 CSV + 一张汇总表
local files = ps.app.glob("data/2026-11/*.ulg")
local rows = {}
ps.app.batch(files, function(path)
  local job = ps.app.load(path); job:wait(60)
  local src = ps.app.source(path)
  ps.app.exportCsv(src, "out/" .. src.name .. ".csv")
  rows[#rows + 1] = {
    name = src.name,
    ch = #src:channels(),
    altMax = ps.stats(src:channel("vehicle_altitude"):slice(0, math.huge, 2000)).max,
  }
  ps.app.close(src)                    -- 补偿动作，避免内存堆积
end)
ps.app.report("out/index.md", rows, { title = "11 月架次汇总" })
```

**B 门限巡检（找异常，打书签，不产图）**

```lua
-- inspect_limits.lua：所有架次里找超限片段，直接打时间书签
ps.app.batch(ps.app.glob("data/*.ulg"), function(path)
  local src = ps.app.load(path):result():source()
  src:channel("attitude.roll"):find(function(v) return math.abs(v) > 45 end,
    function(t) ps.app.marker(t, "roll>45°") end)
end)
```

**C 数据清洗（预处理，供下次秒开）**

```lua
-- clean.lua：raw → 100 Hz 统一栅格 → NDF
for _, f in ipairs(ps.app.glob("data/raw/*.bin")) do
  local src = ps.app.load(f):result():source()
  ps.app.derive(src, "resample", { hz = 100, out = src.name .. ".ndf" })
  ps.app.close(src)
end
```

**反推能力（按用例归并，防止 API 面失控）**

| 用例 | 必需 | 后置 |
|---|---|---|
| A | `glob`、`load/close`、`channels/slice`、`stats`、`exportCsv`、`batch`、`report` | 出图（PNG）、曲线样式 |
| B | `glob`、`slice`、`find`、`marker` | 报告 |
| C | `glob`、`load`、`derive`、`close` | 进度、续跑 |

判断：**先跑通 A 的纯数据版**（CSV + 汇总，不出图）。它同时压到"读→作业→导出→批处理→报告"五条链，跑通后 B/C 只是改脚本、不动代码。

### 2.3 控制台用法（主入口）

数据已经加载好，人在边上试，改参数、看数、再看图：

```lua
>> local src  = ps.app.currentSource()
>> local roll = src:channel("attitude.roll"):slice(0, math.huge, 8000)
>> local rate = ps.derivative(roll, 0.01)
>> ps.show(rate, { title = "roll 变化率" })
>> ps.table{ stats = ps.stats(rate), peaks = ps.peaks(rate, 30), rms = ps.rms(rate) }
```

交互形态（对标 MATLAB command window，不是"一个多行文本框"）：

| 区域 | 内容 |
|---|---|
| 左 | 对象浏览器：源 / 通道，双击插入代码片段 |
| 中 | 控制台 REPL：提示符 `>>`、逐行执行、表达式回显、上下键历史、Ctrl+C 中断、多行续行 |
| 右 | 结果面板：数值表、临时图、历史结果可回溯 |

**REPL 语义（四条，都是硬要求）**

1. **持久 workspace**：控制台是一个长期存活的 `lua_State`，变量跨命令保留。与"跑脚本"是两套语义，见下表。
2. **表达式回显 + `ans`**：先试 `luaL_loadstring("return " + 行)`，失败再按语句编译；`1+2` 显示 `3`，无赋值时结果存 `ans`。
3. **多行续行**：编译错误尾部为 `'<eof>'` 说明语句没写完，继续收行；否则当错误报出（带 `文件:行`）。
4. **显式 GC 与 reload**：`clearDangerous` 已经摘掉 `collectgarbage`（`ScriptEngine.cpp:315`），控制台长跑必须能回收——提供 `ps.gc()`（内部 `lua_gc`）。另外 `require` 有缓存，改完 `lib/` 里的文件必须 `reload <模块名>` 才生效，见 §2.4。

**元命令（MATLAB 式，行首匹配，优先于 Lua 解析）**

| 命令 | 行为 |
|---|---|
| `clear` / `clear x` | 清空 workspace / 清指定变量 |
| `who` | 列变量名（`lua_next` 扫 `_G`，排除标准库与 `ps`） |
| `whos` | 变量名 + 类型 + 长度（table `#`、string 长度、number 整数/浮点） |
| `help` / `help ps.xxx` | 打 API 短说明（注册时带 doc 串）；无参列全部 |
| `run <file> [args]` | 在当前 workspace 里执行脚本，变量可见（同 MATLAB `run`） |
| `reload <lib>` | 清 `package.loaded[lib]`，下次 `require` 重新读盘（改完 lib 必用） |
| `clc` | 清屏，不清变量 |

解析顺序：行首是这七个词之一 → 走元命令；否则一律按 Lua 表达式/语句。要调同名 Lua 函数就用带命名空间的写法（`ps.run()`）。

**两套执行语义（必须写清，否则用户会困惑）**

| 入口 | state | 变量可见性 | 预算/取消 |
|---|---|---|---|
| 控制台逐行、`run <file>` | 控制台 session 的持久 state | 共享 workspace（`run` 后变量留在控制台） | 500 万指令，Ctrl+C 中断 |
| 事件脚本、自动化、CLI `--run` | 每次新建独立 state | 隔离，互不污染 | 按各自 Job 预算，可取消 |

三条硬约束：

1. **控制台只读数据**，唯一副作用是临时图与结果表；数据在内存里，改参数只重跑、不重载。
2. **临时图是 scratch 视图**：复用 `PlottingCanvas` 的渲染能力，但挂在独立视图上，不进 pages、不进 `.psl`、不需 canvas id。
3. **同步执行**：500 万指令预算 + 可中断；不许把控制台也塞进 Job 队列，否则改个参数要排队。

### 2.4 脚本文件组织（普通 Lua，无 schema）

```text
<ShellPaths>/scripts/
├── lib/      可复用函数，require 加载（如 roll_rate.lua）
├── on/       事件脚本，文件名即事件名
└── batch/    自动化入口脚本与示例
```

- 脚本就是 `.lua` 文件，**没有文件头元数据**。注释就是注释，不被任何解析器读。
- 复用靠标准 `require`：`local r = require("roll_rate")` → `r.rate(ch, 9)`。函数参数就是"参数"，不需要 `@param` 声明。
- **版本由 git 管**。原来那套 `usr.xxx.v1.lua` + `op@version` 是为"工程重放"服务的，那件事不在本方案，因此整套版本机制删掉。
- **回归测试不另建机制**：用例就是一个普通 `.lua` 文件（`lib/roll_rate_test.lua` 或 `tests/scripts/*.lua`），用 `plotscope_app --run <file>` 跑，靠退出码判定。这条同时就是 CI 入口。

**受限 `require`（必须新做，因为现在是摘掉的）**

`clearDangerous`（`ScriptEngine.cpp:315`）把 `require`/`package` 一起摘了，所以多文件复用现在**用不了**。恢复方案：**`package` 不恢复，`require` 自写一个替换顶上去**。理由有两条：标准实现在 `package.searchers` 上取路径，`package` 摘掉后它必然失败；而标准文件 searcher 把 `?.lua` 里的 `?` 当任意字符串展开，`require("../../etc/passwd")` 照样能穿出 `scripts/lib/`。

| 项 | 做法 |
|---|---|
| 名字白名单 | 只允许字母数字、`_`、`-`、`.`、`/`；拒空、超 128 字符、以 `/` 开头、含 `..`、含 `\`、含 `:` |
| 路径 | `<configDir>/scripts/lib/<name>.lua`（`.` 转 `/`），必须是已存在的普通文件 |
| 缓存 | 用 registry 的 `_LOADED`（等价 `package.loaded` 语义）；控制台 `reload <name>` 清一项 |
| 递归 | 加载前先写 `_LOADED[name]=true` 哨兵，`a→b→a` 不会无限递归；失败路径回滚哨兵 |
| `package` | 继续摘掉，脚本拿不到，也不能 `package.loadlib` |
| 报错 | 找不到 → `脚本库中找不到模块：<name>（<path>）`；非法名 → `非法模块名：<name>` |
| chunk 参数 | 模块名作为 `...` 传入，脚本可 `local name = ...` |

**沙箱联动（实现时踩到的坑）**：只从 `_G` 摘 `os/io/package/debug` 不够——`luaL_openlibs` 把这些表同时登记进了 `_LOADED`，`require("os")` 会把 `os` 原样交回来，沙箱形同虚设。`clearDangerous` 必须同步清掉 `_LOADED` 里的同名项。

**非绘图脚本裸跑**：`ScriptEngine::run` 原契约要求脚本必须产出序列（`ps.plot` 或全局 `out`），这是绘图派生语义。控制台/自动化脚本可能什么都不输出，因此 `run` 增加 `requireOutput` 参数，默认 `true` 保持旧行为，脚本子系统入口传 `false`。

### 2.5 事件触发（自动化线的第二条入口）

自动化有两种进入方式：人敲命令跑（批处理），和事件一来就自动跑。后者才是"无人值守"的真正形态。

绑定方式沿用"文件即配置"：`scripts/on/sourceLoaded.lua` 存在即绑定，不另存 JSON 配置。好处：与脚本库同一套约定、可进版本库、删文件即解绑。

**P1 只做一个触发点**

| 事件 | 语义 | 阶段 |
|---|---|---|
| `sourceLoaded` | 任一路数据源加载完成（含批处理中的每一路） | **P1 必做** |

选它的理由：它正好对应"把一批日志丢进去，自动处理"，是自动化最原始的动机；而且它天然带一个上下文（哪一路源），不需要额外的参数传递设计。

**以下是候选，不在 P1 承诺**（接口留好，加一个事件只是加一个枚举 + 分发分支）：实时源连接/断开/丢批、录制开始/停止、工程打开/关闭、定时（cron 式）、监工目录变化。

**四道闸（一条都不能少）**

1. **默认关闭**：设置里显式开启"允许事件脚本"；不开时 `scripts/on/` 里放什么都不执行。
2. **重入保护**：事件脚本里若触发同源事件（例如自己又 `load` 一路），按深度上限（建议 3）与同事件压制切断，超出记 `ererecurse`。
3. **隔离**：事件脚本跑在独立 state，失败只记日志与操作清单，不影响 App、不影响下一次触发。
4. **全局暂停**：一个"停用所有触发器"开关，出问题能一键掐死；每次触发起止都写 `LogManager`。

事件脚本属自动化语义：可用 `ps.app.*` 与 Job，但**回调本身在主线程，不许阻塞**，耗时活必须丢给 Job 再 `wait`。

### 2.6 调试（不装 IDE，但要能查错）

不做断点面板，但"写错了怎么查"必须有答案。砍到两层（原来的"L1 现场转用例"可以去掉——控制台 workspace 持久，出错时输入变量都还在，直接改一行重跑就是复现，不需要另存用例机制）：

| 层 | 能力 | 阶段 |
|---|---|---|
| **L0 定位** | 语法错在 `luaL_loadfile` 阶段就拿到 `文件:行`，此时**不执行任何代码**；运行期错带 C 侧 `luaL_traceback` 栈；错误码区分 `esyntax/eruntime/etooslow/eoom/enoent` | S2 必做 |
| **L2 断点** | `plotscope_app --debug <script>` 起**子进程**，该进程内开 `package/debug/io` 与 luasocket，VSCode `lua-debug` / ZeroBrane 可下断点单步 | P2 可选 |

三条红线：

1. **主进程永不开 `debug/io`**。`clearDangerous` 照旧摘干净；调试会话只能落在独立子进程，且**自动化/事件路径没有任何开关能打开它**。
2. **traceback 由 C 侧 `luaL_traceback` 生成**，不依赖脚本可见的 `debug.traceback`——否则脚本能篡改栈信息，错误报告就不可信。
3. `--debug` 开的子进程要在报告里标注"非沙箱运行"，不和正常结果混在一份产物里。

L2 放 P2 的理由：控制台有持久 workspace，大部分"看中间变量"的需求直接敲变量名就满足了；真需要单步时再上子进程 + `mobdebug`，不用提前背这个包袱。

## 3. 模块归属与依赖

### 3.1 新模块 `src/application/modules/scripting`

| 项 | 内容 |
|---|---|
| target | `ps_application_scripting` |
| 拥有 | 两条线共用一个模块：**控制台**（REPL + 结果面板 + scratch 临时图 + 计算库）、**自动化**（Job 引擎 + 操作清单 + 批处理 + 事件分发）、对象浏览器、脚本目录、Lua 引擎、`script.*` 命令 |
| 允许依赖 | `shell` 宿主接口、`modules/runtime`、`ps_kernel` C ABI、Qt、Lua |
| 禁止依赖 | `modules/datasource`/`modules/plotting` 私有头、`MainWindow*`、直接调画布方法 |
| 装配 | 由 `plotscope_app` 链接；Dock 走 `DockContribution`，命令域 `script.*` |

### 3.2 不抽独立的 `modules/lua`（本版改了）

原方案打算抽 `modules/lua` 给"算子运行"和"脚本"共用。**算子没有了，只剩一个消费者**，抽独立 target 就是纯负担。改为：

```text
modules/scripting/
├── include/       ScriptHostService 的消费方接口
├── lua/           Lua 引擎（沙箱/预算/取消/table 桥接/require）——内部目录，不是独立 target
└── ...
```

引擎代码从 `extensions/builtin/script` 搬进来即可，依赖已经核实干净：`ScriptEngine.cpp` 只 include `ScriptEngine.h` + Qt + `lua.h/lauxlib.h/lualib.h`，而 `ScriptEngine.h` 只往上追溯到 `ScriptService.h`（本就在 `modules/runtime/include/`），没有任何 extension 宿主 API，搬运不会牵出新依赖。

若将来真出现第二个消费者，**按仓库现约定走 `src/application/shared/lua`，不新建 `modules/lua`**——`shared/filter` 就是这个模式的既有先例（`AGENTS.md §2` 把它列为"共享 UI 库"，`§3` 给它单列一行依赖约束）。现在提前拆只是给自己加依赖矩阵。

### 3.3 数据面契约：`ScriptHostService`

datasource/plotting 各自实现并注册，scripting 只消费（**不放 `QWidget*`，不传 `MainWindow*`**）：

```cpp
// modules/runtime/include/ScriptHostService.h
namespace ps::scripting {

struct Result { bool ok; QString code; QString message; };   // code: enoent/eformat/etimeout/ecanceled/...

struct SourceInfo  { int id; QString name, alias, path, format; bool live, paused; };
struct ChannelInfo { QString name, unit; quint64 count; double t0, t1, vMin, vMax; };
struct LiveStatus  { bool connected, recording; QString recordPath; qint64 dropped, samples; bool overflow; };

struct HostService {
    // A0 读
    virtual QVector<SourceInfo>  sources() = 0;
    virtual QVector<ChannelInfo> channels(int sourceId) = 0;
    virtual Result slice(int sourceId, const QString &ch, double t0, double t1,
                         int maxPoints, QVector<double> &t, QVector<double> &v) = 0;
    virtual LiveStatus liveStatus(int sourceId) = 0;
    // B0/B1 作业
    virtual JobHandle loadFile(const QString &path, const QString &loaderOptions) = 0;
    virtual Result    closeSource(int sourceId) = 0;
    virtual JobHandle reloadSource(int sourceId) = 0;
    virtual JobHandle exportCsv(int sourceId, const QString &path, bool overwrite) = 0;
    virtual JobHandle connectLive(const QString &extensionId, const QString &configJson) = 0;
    virtual Result    disconnectLive(int sourceId) = 0;
    virtual Result    setPaused(int sourceId, bool paused) = 0;
    virtual Result    startRecording(int sourceId, const QString &fmt, const QString &path) = 0;
    virtual Result    stopRecording(int sourceId) = 0;
};
void setHostService(HostService *svc);
HostService *hostService();   // 未注册 → 脚本报 "eneedhost"
}
```

与实现一致的两处细节：`Result` 带 `ok = true` 默认与 `static failure(code, message)`；`JobHandle` 带 `bool valid() const { return id >= 0; }`，**`-1` 为无效**——源 id 从 0 起，用 0 当无效值会把第一个源判成失败。访问器叫 `hostService()` 而不是 `host()`，避免与 `HostContext` 撞名。

### 3.4 需要同步改的硬约束

- `AGENTS.md §2`：目录表加 `modules/scripting`
- `AGENTS.md §3`：允许依赖补一行；跨模块规则补"或 `runtime` 的能力服务表"
- `AGENTS.md §9`：`ps_application_scripting` 的 link 边界；**它不得被 extensions 反向依赖**
- 旧扩展退场：`extensions/builtin/script` 删掉（只剩一个 `addScriptRunner` 注册，`ScriptComponent.cpp:34`），`Registry::addScriptRunner`（`Component.h:143`）/`runScript`（`Component.h:155`）与 `ScriptService` 的 runner 一并摘除。注意 `AGENTS.md §2` 现在把脚本列为 `extensions/builtin` 里的 C++ 组件之一，删掉后要同步这段描述。

### 3.5 语言选型：只用 Lua

结论先写：**控制台与自动化只用 Lua，Python 不进入数据面，只保留为"外部纯计算工具"。**

依据不是偏好，是两套现成实现的硬约束：

| 维度 | Lua 5.4（现方案） | Python 3（现有 `runPython`） |
|---|---|---|
| 接入方式 | 进程内 C API，状态可复用（`ScriptEngine.cpp:466`） | 子进程 + 临时 `.py` + JSON 管道（`:25-89`） |
| 数据面 | 可持 userdata 回调宿主，按窗取切片 | 只能把整条通道变成 JSON 数组；`:50-51` 逐点拼 `QJsonArray` |
| 10M 点代价 | 零拷贝 + `ps_lod_minmax` 抽样 | 序列化 2×10M 个 double，内存与时延都不可接受 |
| 沙箱 | 摘 globals（`:313-321`）+ 分配器上限 64 MB（`:117-120`）+ 指令 hook（`:474`） | 进程内无法限制；只能整进程 kill，15 s 硬超时（`:75`） |
| 崩溃隔离 | `lua_atpanic` 兜回宿主，单次运行失败 | 子进程不连带宿主，但每次调用付进程启动成本 |
| 单次开销 | ≈0（状态复用） | 30–100 ms/次；10 架次 × 多次调用明显拖慢 |
| 科学计算生态 | 无（靠 Rust 内核补） | numpy/scipy 强 |
| 标杆 | PlotJuggler 用 Lua | PJ4 同为 Lua |

**为什么生态劣势不致命**：重活（滤波/重采样/PSD）走 Rust 内核，脚本层只做编排与轻计算。一旦允许 Python 拿 numpy 直连数据，就会有人把 1000 万点塞进 Python，然后卡在 JSON 序列化上——现有实现就是活证据。

**Python 的保留位置**：离线批处理的外部工具（用户自己的 `.py` 管线），输入输出走文件，不进 `ps.app.*`，不改造现有 `runPython`。

### 3.6 数学能力在哪（诚实盘点）

"Lua 做 FFT/滤波不方便"这个判断是对的，但结论不是换语言，而是**这些数学必须由内核提供**。架构 v1 已经把这条钉死（`architecture-v1.md:208-219`：Lua 不碰逐样本循环，算术走元表一次算一整块；标量循环速度掉到 1/100）。

现有内核已经是一个"算子注册表"形状——`ps_dataset_apply_filter(ds, input, filter_id, params_json, output)`（`ps_kernel.h:56`），加算子只需往 `ps-ops` 里加分支。但**现在的算子集里没有 FFT、没有 PSD、没有 IIR/FIR、没有抗混叠重采样**，这点必须承认：

| 已有（`ps-ops/src/filters.rs:28-187`，13 个） | 缺 |
|---|---|
| `none / absolute / scale / derivative / integral / moving_average / moving_rms / moving_variance / outlier_removal / samples_counter / binary_filter / time_since_previous / normalize` | FFT / IFFT、Welch PSD、butterworth biquad、FIR(自定义 taps)、polyphase 重采样、find_peaks、互相关 |
| 重采样只有 `nearest / linear` 插值（`align.rs:4,17`），无抗混叠 | 降采样前必须先低通的正确实现 |

补齐清单（按"控制台第一批要用"的优先级，全部是 `ps-ops` 的活）：

1. **统计**：mean/std/min/max/skew/kurtosis —— 纯遍历，最容易，S2 就要
2. **IIR 滤波**：butterworth biquad（low/high/band）+ 裸 biquad 系数入口（方便从 MATLAB/Python 抄系数）
3. **FFT / PSD**：`rustfft` 或 `kissfft`，输出幅值谱；Welch 分段 + 窗 + 平均
4. **重采样**：decimate（先低通再抽点）、polyphase resample
5. **峰值与相关**：find_peaks（带高度/距离/突出度）、xcorr

一个必须定的模型问题：**FFT 的 x 轴是频率，不是时间**。现有通道模型是"时间序列"，频谱塞不进这个语义。两个选项：(a) 扩通道模型带 `x_kind`；(b) FFT 结果不进通道，直接作为 `{f, mag}` 表交给控制台的 scratch 视图（scratch 本来就接受任意 x/y）。短期选 (b)，要进文档画布时再走 (a)。

**Python 在这件事上的正确位置**：算法孵化器。新滤波器先在 numpy 里试系数、验口径（比如 Welch 的窗长与重叠对结果的影响），确认后固化成 Rust 算子。它不是运行时，是设计阶段的草稿纸。

### 3.7 Lua 怎么调到 Rust 内核（机制与三个坑）

能调，但不是"Lua 认识 Rust"，中间隔着两跳：

```text
Lua 源码
  → Lua C 函数（lua_pushcfunction 注册的 closure）
      → extern "C" 符号（Rust 侧 #[no_mangle] pub extern "C" fn）
          → ps-ops 的向量实现
```

Lua 在语言层面只认"C 函数指针"，而 Rust 的 `extern "C"` 导出符号对链接器来说就是 C 符号——所以这条路成立。仓库里已有先例：`ps-abi/src/lib.rs:19-31`。

**坑一：传 table 会让 Rust 白算。** 现在 `l_moving_avg`（`ScriptEngine.cpp:224-242`）是先从 Lua table `readVector` 逐元素捞出来、在 C++ 里循环算、再 `pushVector` 塞回去——**根本没走 Rust**，光搬运就 N 次 `lua_rawgeti`。零拷贝的唯一办法是传指针：`userdata` 持有通道的 `const double*` + 长度，C 函数 `lua_touserdata` 取回来，整块交给 Rust，结果再包成 `userdata`。

**坑二：dataset 级 ABI 脚本够不到，但曲线级 ABI 已经有了。**

```c
int32_t ps_filter_apply_series(const char *filter_id, const char *params_json,
                               const double *t, const double *v, uint32_t n,
                               double *out_t, double *out_v, uint32_t cap,
                               uint32_t *out_n, char *err, uint32_t err_cap);
```

（`ps_kernel.h:65-68`）指针进、指针出、不需要 dataset——Lua 侧的 channel userdata 正好能喂进去。**不需要新建 ABI**；只需注意它复制了时间轴（`out_t` 必填），10M 点会多一次 80 MB 复制，切片场景要传 `maxPoints` 控制长度。

**坑三：f32/f64 口径。** `filters.rs` 的签名是 `apply(id, t: &[f64], v: &[f64], p)`，曲线级入口也是 f64——**走 filter 路径不会掉精度**。但 `ps-ops/src/lib.rs` 里 `scale/moving_avg` 那批早期向量函数是 f32（`lib.rs:9`、`lib.rs:20`）。规矩：**Lua 侧一律 f64，只接 `filters.rs` 这条；f32 那批留给渲染热路径**。

错误怎么回 Lua：Rust 返回 `i32` + `err` buffer，C 函数直接 `luaL_error(L, "%s", err)` 抛出。这样错误天然带 Lua 行号和 traceback，正好接上 §2.6 的 L0 定位。

依赖是否合规：`modules/*` 允许 link `ps_kernel` 的 C ABI（`AGENTS.md:41-43`），所以 `modules/scripting` 直接链 C ABI 是合法的。

## 4. 对象句柄（数据面）

```lua
local src = ps.app.source("flight.ulg")   -- SourceRef{id}
local ch  = src:channel("att.roll")       -- ChannelRef{source=id,name}
local seg = ch:slice(0, 60, 4000)         -- {t={...}, v={...}}，抽样后 ≤4000 点
local job = ps.app.load("flight.ulg")     -- JobRef{id}
```

| kind | 稳定键 | 失效条件 | 访问失效 |
|---|---|---|---|
| source | `SourceData.id` | 源关闭/重载/工程切换 | 报 `esrcgone`，不返回空表 |
| channel | `(source.id, name)` | 通道被删/改名/源重载 | 报 `echgone` |
| job | `JobHandle.id` | 作业结束且已回收（默认保留 5 min） | 报 `ejobgone` |

规则：

1. 句柄只在**本次运行**内有效，脚本源码不得缓存 id 字面量作长期契约。
2. 切片一律 `ch:slice(t0,t1,maxPoints)`，默认 `maxPoints = 2×像素宽`，走 `ps_lod_minmax`（`ps_kernel.h:79`）；禁止把整条通道铺成 Lua 表。
3. 数据只读：脚本拿不到内核 `PsDataset*`，计算结果只进 scratch 或导出文件，不写回数据集。
4. **计算入参优先传句柄，不传 table**：`ch` 是带 `const double*` 的 userdata，内核算子直接吃指针（零拷贝）；`ch:slice()` 返回的 `{t,v}` 只用于 ≤ 1 万点的探索或看形状。同一个函数两套入参都接受，但内部走同一条 Rust 块级 ABI（§3.7）。

## 5. 作业模型（只服务自动化线）

```text
Job 状态机：queued → running → (done | failed | canceled)
字段：id / kind / target / progress(0..1，可空) / message / error{code,msg} / startedAt / endedAt
```

- **同步等待**：`job:wait(30)` 内部跑事件循环，超时返回 `etimeout` 且**不杀作业**（可继续 `onDone`）。
- **回调**：`job:onDone(function(j) ... end)`；回调在主线程执行，禁止阻塞。
- **取消**：`job:cancel()` → 加载类在下一个 checkpoint 中止并释放临时数据集；录制类停止并落盘（不留半文件）。
- **并发**：写数据面 **串行队列**（同时最多 1 个 load/reload），避免两个 loader 抢同一个 `SourceStore`；录制与加载可并行。
- **补偿动作**：自动化不回滚，只给逆操作 —— `load` 的补偿是 `close`，`export` 的补偿是显式删文件（需白名单），`record` 的补偿是 `stop`。运行时把每个副作用记进**操作清单**（脚本结束打印，UI 可查）。
- 与 UI 一致：P2 把 UI 的加载也切到同一 Job 实现，消除"UI 同步阻塞、脚本异步"的双轨。

## 6. 安全模型

| 项 | 目标 |
|---|---|
| Lua 沙箱 | `clearDangerous` 继续摘 `os/io/debug/dofile/loadfile/collectgarbage`；**`require` 按 §2.4 受限恢复**，`package` 表搜完即摘 |
| 预算 | 内存 64 MB；指令预算 P1 降到 **500 万**，hook 内检查取消标志 |
| 文件访问 | **只经 API 传路径，不给自由 IO**：路径过白名单（工作目录、导出目录、最近文件目录、脚本目录），规范化后拒绝 `..` 穿越、符号链接逃逸、设备路径 |
| 覆盖保护 | `export*` 目标已存在默认失败（`eexist`），需 `overwrite=true` 显式允许 |
| 录制 | 目标路径同规则；异常退出删除未完成的临时文件 |
| Python | 不进主链路；无法访问 `ps.app.*` |
| 控制台 | 与自动化同一沙箱与预算；额外限制：结果面板只接受 `v/t/table`，不接受任意对象，防脚本把 UI 塞满 |
| 崩溃隔离 | Lua 报错只终止本次运行；`lua_atpanic` 兜底回宿主，不允许穿透到 `main` |
| 调试例外 | 只有 `--debug` 起的**子进程**可开 `package/debug/io`；主进程与自动化路径无开关可开 |

## 7. API 面

```lua
-- A0 读（两条线共用）
ps.app.sources()                    -- [{id,name,alias,path,format, live,paused}]
ps.app.source(name|id)              -- SourceRef
ps.app.currentSource()              -- 当前活动源（控制台最常用）
src:channels()                      -- [{name,unit,count,t0,t1,vMin,vMax}]
src:channel(name)                   -- ChannelRef
ch:slice(t0,t1,maxPoints)           -- {t,v}；maxPoints 省略=2×像素宽
src:liveStatus()                    -- {connected,recording,dropped,samples,overflow}

-- A1 计算库（纯数字，全部落 Rust 内核）
ps.stats(v) / ps.peaks(v, thr) / ps.rms(v)
ps.derivative(v, dt) / ps.integral(v, dt) / ps.movingAvg(v, n)
ps.filter(v, {kind="lowpass", fc=10, fs=100})
ps.resample(v, t, hz)
ps.fft(v, {fs=100}) / ps.psd(v, {fs=100, nfft=1024})
ps.correlate(a, b) / ps.find(v, pred)     -- find 返回超限区间 [{t0,t1,vmax}]
ps.align(srcA, srcB)                      -- 多源时间对齐

-- A2 结果呈现（即时、不落盘）
ps.show(v, {title=..., color=..., axis=...})   -- 临时图（scratch 视图）
ps.table{ stats=..., peaks=..., rms=... }      -- 数值表，可多组并列
ps.clearResults()

-- A3 提升为正式曲线（P2）
ps.promote(v, canvas, {name=..., color=...})

-- B0 作业
local job = ps.app.load(path)       -- 可选 {loader="csv", options={...}}
job:wait(30) / job:onDone(fn) / job:cancel()
ps.app.reload(src) / ps.app.close(src)
ps.app.exportCsv(src, path, {overwrite=true})
ps.app.glob("data/*.ulg") / ps.app.report(path, rows, {title=...})
ps.app.clearCache()

-- B1 实时与录制
local live = ps.app.connectLive("mavlink", {port="/dev/ttyUSB0"})
live:pause() / live:resume() / live:disconnect()
live:record("mcap", path) / live:stopRecord()

-- B2 批处理
ps.app.batch(files, function(path) ... end, {failFast=false, onProgress=fn})

-- 杂项
ps.gc()
```

映射表：

| API | 现有实现 | 缺口 |
|---|---|---|
| `ch:slice` | `ps_dataset_channel` + `ps_lod_minmax` | 包一层按窗抽样；`maxPoints` 默认 |
| `ps.derivative/movingAvg/integral` | `l_derivative/l_moving_avg/l_integral`（`ScriptEngine.cpp:224-260`），**C++ 逐元素算，不走 Rust** | 改走块级 ABI（§3.7），与内核同名算子合并为同一实现 |
| `ps.stats/peaks/rms` | 无（Lua 侧现算） | 新写；大数组下放内核 Rust 算子 |
| `ps.filter/ps.fft/ps.psd/ps.resample` | 机制已有：`ps_dataset_apply_filter`（`ps_kernel.h:56`）；Lua 侧无 API，**内核也没有 FFT/PSD/IIR/抗混叠重采样** | 先补 `ps-ops` 算子（§3.6 清单），再包 Lua API；不允许在 Lua 里逐点实现 |
| `ps.show` | `PlottingCanvas` 渲染能力 | 需 scratch 视图（不进 pages/`.psl`） |
| `ps.app.load` | `DataSourceSession::loadFile` → `DataSourceDock::loadFile` | 同步阻塞 → 包 Job + checkpoint |
| `ps.app.close` | `closeSources` / `closeLiveSource` | 单源粒度需返回 `Result` |
| `ps.app.reload` | `reloadChannels` + `data.reload` | 抽成 Job，带失败码 |
| `ps.app.exportCsv` | `exportActiveCsv`（只导活动源/可见窗） | 需显式源 + 路径 + 覆盖策略 |
| `ps.app.glob/report` | 无 | 新写；路径走白名单 |
| `ps.app.connectLive` | `connectLiveSource/connectSource` | 配置 JSON 直传 + 状态返回 |
| `live:record` | `LiveSession::startRecording(fmt, config, path, err)` | 统一错误码，去掉 `QString*` 出参 |
| `live:pause` | `setSourcePaused` | 统一到 live 句柄 |
| `ps.app.batch` | 无 | 新写：串行 + 失败策略 + 汇总 |

### 7.1 S1 落地清单

已实现（`LuaAppApi.cpp` + `ScriptHostBridge.cpp`）：

| 面 | 函数 | 说明 |
|---|---|---|
| `ps.app` | `sources()` / `source(name\|id)` / `currentSource()` | 返回 `SourceRef` 表，字段 `id/name/alias/path/format/live/paused`；`currentSource` 取 store 末尾（控制台接活动源后语义不变） |
| `SourceRef` | `channels()` / `channel(name)` / `liveStatus()` | `channels()` 全扫算 `vMin/vMax`；`liveStatus()` S1 只填 `connected` |
| `ChannelRef` | `slice(t0,t1,maxPoints)` | 二分定位 + stride 抽样，默认上限 4096 点；窗口外返回空表 |
| 作业 | `load(path,{loader=})` / `exportCsv(src,path,{overwrite=})` / `close(src)` | 同步完成，`JobRef` 的 `wait/cancel/onDone` 退化为立即返回 |
| CLI | `plotscope_app --run <script.lua>` | 退出码 0 成功 / 1 脚本错 / 2 脚本不可读；`print` 走 stdout，诊断走 stderr |

未实现（留在后续切片）：A1 计算库、`ps.show/ps.table`、`ps.app.glob/report/batch/reload/clearCache`、`connectLive/record`。

两个已知边界：

- `channels()` 的 min/max 是 O(n) 全扫。1000 万点单通道 9 ms，50 源 × 4 通道约 1.8 s，仍在 S1 验收内，但通道更多时会线性增长。S2 控制台若要频繁列通道，改走内核缓存的通道统计。
- `loadFile` 失败只返回 `JobHandle{-1}`，loader 的 `err` 被丢弃，Lua 侧只能报通用 `eload`。S3 接作业模型时把 `Result` 带出来。

## 8. 开工切片

### 8.0 开工前置（三条，开工前必须落地）

| # | 前置 | 为什么是阻塞 | 谁做 |
|---|---|---|---|
| P1 | **基线对齐到 `2649a98`** | 已执行：`stash → fetch → rebase → stash pop`（README 一处冲突已解）。当前 HEAD `2649a98`，`ps_filter_apply_series` 在 `kernel/include/ps_kernel.h:65-68` 可查，`shared/filter` 就位 | 已完成 |
| P2 | **`PlottingSession.cpp:1165` 必须先摘掉 lua 项** | `Registry::runScript`（`Component.cpp:78`）的唯一调用方就是这一行（`m_mw->extensions().registry().runScript(dlg.scriptText(), {in})`，`2649a98` 上是 `:1165`）。要删 `m_scriptRunner` 槽与 `runScript`，必须同时改它，否则编译直接断。而它属绘图侧 | 绘图侧（本方案不改这一行，见 §8 S0 的"不动绘图侧"） |
| P3 | **`ps-ops` 五个数学算子另有排期** | stats / biquad / FFT / Welch PSD / decimate 在 `src/application/kernel/ps-ops`，是 Rust 内核的活，不是 `modules/scripting` 的活。S2 的验收标准是"出频谱图与滤波结果"，硬依赖它们 | 内核（本模块只负责把 Lua API 包在它们外面） |

P2 的处理策略：**S0 不删槽位**。先让 `modules/scripting` 并行建起来、旧扩展暂留；等 P2 完成后，再单独一次 commit 清 `addScriptRunner` / `runScript` / `m_scriptRunner` 这三个符号。代价是短期内留一个没人注册的空槽，收益是 S0 当天就能编译通过、两边零耦合。

### 8.1 切片

| 片 | 范围 | 验收 | 不做 |
|---|---|---|---|
| **S0 引擎搬迁与骨架**（已完成） | Lua 引擎从 `extensions/builtin/script` 搬进 `modules/scripting/lua/`；受限 `require`（§2.4）；`ScriptHostService` 数据面契约；`modules/scripting` 模块 + `script.info` 命令；`HostContext::registerCommand` 让模块不必兜到 MainWindow；AGENTS 同步 | 编译通过；`ctest` **31/31**（含新增 `script_library`）；`require("roll_rate")` 可加载，`require("os")` / `require("../../etc/x")` / `/abs` / `C:/` / `\\` 全拒；环依赖不死循环；失败后哨兵已清 | 不写 `ps.app` API；不删旧扩展与 runner 槽（等 P2）；不动绘图侧；`ps_filter_apply_series` 接入留到 S2 |
| **S0.5 死后清理（等 P2 完成后）** | 删 `extensions/builtin/script`；摘 `Registry::addScriptRunner`（`Component.h:143`）、`Registry::runScript`（`Component.h:155` / `Component.cpp:78`）、`ScriptService` 的 runner 声明 | `grep addScriptRunner\|runScript\|m_scriptRunner` 零命中；`ctest` 全绿 | 与 S0 混做（P2 未完成时会被编译打断） |
| **S1 只读数据面**（已完成） | `sources/source/currentSource/channels/channel/slice/liveStatus`、`load/exportCsv/close` 的 Lua 对象面（`ps.app.source` / `ps.app.channel` / `ps.app.job` 三个 metatable）；`ScriptHostBridge` 落在 `datasource` 模块，`ScriptHostService` 之上只有接口；**`--run <script.lua>` CLI 闭环**（无 UI，自建 `ExtensionHost` 激活内置 loader） | 编译通过；`ctest` **33/33**（新增 `script_library` / `script_app_cli` / `script_slice_perf`）；1000 万点 `slice` 到 4096 点 = **64 KB / 1.8 ms**；`channels()` 对 1000 万点全扫 min/max = 9.1 ms；50 源 `sources()` = 0.00 ms；`plotscope_app --run` 跑通"加载→取通道→切片→导出 CSV"，退出码 0/1/2 | 不做任何副作用（除导出）；`connectLive/record` 返回 `eunsupported`；`reload/glob/report/clearCache` 未暴露；`loadFile` 失败只回 `eload`，不携带 loader 原始错误 |
| **S2 控制台（先出价值）** | 计算库 Lua API + **内核补齐（前置 P3）：stats、biquad/butterworth、FFT、Welch PSD、decimate**；**REPL（持久 workspace / 表达式回显 + `ans` / 多行续行 / `ps.gc` / Ctrl+C）**；元命令 `clear/who/whos/help/run/reload/clc`；结果面板（表 + scratch 临时图，支持频率域 x 轴）；L0 错误定位 | 加载一份日志后逐行敲 5 条命令出统计表、频谱图与滤波结果，合计 < 3 s；`1+2` 回显 `3`；`who/whos` 正确列出；`run script.lua` 后变量留在 workspace；改 lib 后 `reload` 生效；改参数重跑 < 1 s；语法错报 `文件:行` 且不执行代码 | 不做 Job、不做文档写入、不出正式曲线 |
| **S3 作业模型 + 事件触发** | Job 状态机、串行写队列、`load/close/reload/exportCsv/report/glob`、`wait/onDone/cancel`、操作清单；**事件分发器 + `scripts/on/sourceLoaded.lua` + 四道闸** | 无人值守"加载→导出→汇总→关闭"跑通；取消后 500 ms 内回空闲且无半成品文件；放一路日志进 `sourceLoaded` 自动跑完；死循环脚本被预算掐断且后续触发不受影响 | 不做实时；不做其他事件源 |
| **S4 实时源与录制** | `connectLive/pause/resume/disconnect`、`record/stopRecord`、状态与丢批查询 | 连接→录制 MCAP 30 s→停止，可被 `mcap_loader` 回放；取消不留半文件 | 不做多源同时录制 |
| **S5 批处理** | `ps.app.batch`、失败策略、进度、汇总、续跑 | 10 架次循环，单架次失败继续并汇总；中断后续跑不重复处理 | 不做分布式/多进程 |
| **S6 提升为正式曲线（P2）** | canvas 稳定 id、`ps.promote`、快照事务、页面/曲线/轴 | 提升的曲线重开工程仍在；失败回滚页面 JSON 逐字节不变 | 不与 S2–S5 混做 |
| **S7 子进程断点调试（P2 可选）** | `--debug <script>` 子进程、`mobdebug`/luasocket 接入、VSCode `lua-debug` 与 ZeroBrane 文档 | 断点能停在脚本内并读局部变量；主进程沙箱不变（负向测试仍绿） | 不在 App 里造调试 UI |

依赖：P1 → S0 → S1 → S2 → S3 → { S4, S5 }；S0.5 依赖 P2；S2 依赖 P3；S6、S7 独立立项。

排序理由：**控制台（S2）先上**，它不需要 Job、不需要 canvas id、不需要文件白名单，最快验证 `ps.app` 形态；**自动化（S3）紧跟**，因为"人在场调试出来的脚本"要立刻能变成"人不在场跑"，否则两条线永远接不起来。S4/S5 是自动化的两个专用场景，可以并行。

**S1 里那个 CLI 最小闭环是刻意提前的**：它只需要 `sources/load/export` 三个方法就能跑，1–2 天工作量，但能最早回答一个要命的问题——`ps.app.*` 这套命名和句柄形态（`src:channel(name):slice()`）用起来顺不顺手。如果这里发现形态不对，改起来只动 S1，不至于等 S2 做完控制台才发现要返工。

## 9. 测试矩阵

| 用例 | 断言 |
|---|---|
| 句柄失效 | source/channel/job 各一组：关闭/重载后访问报错误码，不返回空表、不崩 |
| Job 语义 | wait 超时不杀作业；onDone 只触发一次；cancel 幂等 |
| 取消干净 | `load` 取消后 `SourceStore` 数量不变、临时数据集已释放 |
| 导出保护 | 目标存在且未 `overwrite` → `eexist`；`overwrite=true` 覆盖成功 |
| 路径白名单 | `../etc/passwd`、符号链接逃逸、设备路径全部拒绝 |
| 录制完整性 | 取消后无半文件；正常停止可被 `mcap_loader` 读回，样本数匹配 |
| 预算与取消 | 死循环被 500 万指令打断；[停止] 200 ms 内返回 |
| 沙箱 | `io/os/debug/dofile/loadfile/collectgarbage` 为 nil；`package` 表不可见 |
| 受限 require | `require("roll_rate")` 成功；`require("os")`、`require("../x")`、绝对路径、非 `scripts/lib` 下的文件全部 `enoent` |
| require 缓存 | 改 `lib/roll_rate.lua` 不 reload 时行为不变（证明缓存生效）；`reload("roll_rate")` 后行为更新 |
| 错误定位 | 语法错报 `文件名:行` 且语法错时未执行任何代码；运行期错带 `luaL_traceback` 栈且行号指向实际出错行 |
| 主进程不可调试 | 主进程任何配置下 `debug/package/io` 均为 nil；`--debug` 只在子进程生效，自动化路径传该参数被拒绝 |
| 并发安全 | 两个脚本同时 `load` 串行化，不出现源重复或 id 冲突 |
| 性能 | 1000 万点 slice 不复制整条；批处理 10 文件总耗时线性、无内存爬升 |
| 控制台响应 | 8000 点通道 `stats/derivative/show` 合计 < 300 ms；连续改参数 10 次无内存爬升 |
| REPL 语义 | `1+2` 回显 3；`x=1` 不打印；未写 `end` 的函数被续行接住；`clear` 后 `x` 为 nil |
| 临时图隔离 | `ps.show` 后 `.psl` 无变化、`markDirty` 未触发；关窗后 scratch 视图释放 |
| 计算库正确性 | `movingAvg/derivative/integral/filter/resample` 与内核算子结果一致（逐点比对，容差 1e-9） |
| 块级 ABI 零拷贝 | 1000 万点 `ps.movingAvg(ch, 5)` 全程无 `lua_rawgeti`、无 table 分配；峰值 RSS 增量 < 3×数据量 |
| 精度口径 | 走 f32 内核路径与 f64 直算在同一输入上偏差 < 1e-6 |
| 事件默认关闭 | 未开启时 `scripts/on/` 有文件也不执行；开启后触发一次且只一次 |
| 事件重入 | 事件脚本里再 `load` 触发同事件 → 被深度上限切断并记 `ererecurse`，App 不冻 |
| CLI 回归 | `--run tests/scripts/xxx.lua` 返回正确退出码；失败时输出首错位置；CI 里可直接跑 |

新增测试进 `tests/unit`（`add_test(NAME scripting_*)`）；改 UI 做 GUI 冒烟（AGENTS §12）。

## 10. 风险与取舍

| # | 风险 | 处理 |
|---|---|---|
| R1 | 自动化不可撤销（已导出、已关源） | 明说：只给补偿动作 + 操作清单；关键操作写 `LogManager` |
| R2 | 加载同步阻塞，包 Job 要动主路径 | S3 先在会话层加 checkpoint，不改 loader；UI 切换放 P2 |
| R3 | 服务表变成第二个 `ModuleBridge` | 只放数据与作业，不放 `QWidget*`、不传 `MainWindow*`；评审按 AGENTS §3 卡 |
| R4 | 两条线共用一套 `ps.*`，命名互相渗 | 控制台与自动化共用 `ps.app.*` 与 `ps.stats/ps.show`（本来就该共用）；不设第三套命名 |
| R5 | 受限 `require` 恢复出漏洞 | 三条硬约束：path 写死、只留 Lua searcher、`package` 表搜完即摘；负向测试覆盖 `require("os")/require("../x")/绝对路径` |
| R6 | 录制与批处理抢磁盘 | 录制走有界队列；同一时刻只允许一个写作业 |
| R7 | 控制台 scratch 视图与正式画布两套渲染 | scratch 复用 `PlottingCanvas` 渲染代码，只换容器，不另写一套绘制 |
| R8 | Python 长期无数据面 | 明确写入非目标；需要时单独立项做 RPC |
| R9 | 没有版本的裸脚本被改语义，旧结论静默变化 | 明说：版本由 git 管，脚本改动要进 commit；自动化产物里写 `git rev` 与脚本路径，事后可追溯 |
| R10 | `scripts/` 变成垃圾场（一堆试跑脚本） | 不设入库门槛（用户明确要"普通 lua 脚本"），改为轻约束：`batch/` 只放入口脚本、`lib/` 只放被 require 的文件、临时试验留在控制台不落盘 |
| R11 | 脚本模块悄悄长出第二条渲染路径 | 本模块不 import plotting、不碰 `PlottingCanvas`、不写 recipe；评审按 AGENTS §3 卡依赖 |
| R12 | 纯文本编辑没有实时反馈，写脚本像盲写 | 控制台 `run` + `reload` 就是反馈回路；不假装做文件监听，也不做编辑器 |
| R13 | 为了能调试而开 `debug` 库，等于把沙箱拆了 | 调试只走 `--debug` 子进程；主进程与自动化路径永不开放，负向测试卡死 |

## 11. 已定（不再讨论）

这几条原来挂在"待拍板"，现在方案内部已经自洽，写在这里作为**已决记录**，避免开工时被反复翻出来：

| # | 已定 | 依据 |
|---|---|---|
| 1 | 控制台先于自动化（S2 排在 S3 前） | "人在场调出来的脚本"要能立刻转成"人不在场跑"；顺序反了会做两套 API |
| 2 | 自动化必须带 Job | 加载是同步阻塞（`DataSourceDock::loadFile` 直调 `entry->load`），不包 Job 就冻 UI |
| 3 | 脚本目录独立于 `.psl` | 脚本要进版本库、要能 `require`、要不污染工程文件 |
| 4 | `require` 受限恢复 | 不恢复就只能单文件脚本，长脚本无法拆分（现状 `ScriptEngine.cpp:315` 把 `require` 摘了） |
| 5 | Python 不进主链路 | 子进程 + JSON 全量序列化承载不了 10M 点（§3.5） |
| 6 | 事件 P1 只做 `sourceLoaded` | 先验证机制，加事件源只是加枚举 + 分发分支 |
| 7 | 不做"算子"抽象 | 用户裁决：算子就是普通 Lua 脚本，直接调用；版本由 git 管 |
| 8 | 不接管绘图侧曲线变换 | 用户两次裁决："不需要你来考虑" |

## 12. 剩下的真待定

1. **S2 是否等内核数学算子（前置 P3）齐了才开**（推荐：不等。S2 先做 REPL + 已有算子 + stats，FFT/PSD 的 UI 槽位留好，等内核合入再接；否则控制台要等内核排期）。
2. **调试做到哪一层**（推荐：S2 只做 L0；L2 子进程断点 P2 按需上）。
3. **频域结果怎么表达**（推荐：短期 `{f, mag}` 表进 scratch，不进通道模型；要进文档画布时再给通道加 `x_kind`）。
4. **元命令清单**：`clear / who / whos / help / run / reload / clc` 七个够不够（`cd/pwd/dir` 这类文件命令暂不做，路径走 API 白名单）。
5. **数组的 ABI 形态（S2 开工前必须定，暂缓）**：两条路——A 新增数组句柄 ABI（`ps_array_new/op/free`），数组是内核侧一等对象，Lua 的 userdata 只包句柄，能写 `y = x + 1`、`(x+1)*2`，零拷贝，代价是多 3–5 天的 ABI 与生命周期设计；B 不新增 ABI，算子直接吃 `PsChannelInfo` 的 `t/value` 指针，省事但中间结果无处存放，`(x+1)*2` 写不出来，只能退化成 `ps.scaleEx(ch,1,2)` 这类嵌套参数。
   现状：**S1 已经按 B 的形态落地**（`ch:slice()` 返回 `{t,v}` 两个 Lua table，`channels()` 直接读指针）。选 A 的话，`slice` 的返回值要改成数组对象，属计划内返工，不影响已完成的 CLI 闭环与测试。
   相关实测（用于决策，`lua` 5.3.6，1000 万点）：Lua 逐元素 `y[i]=a[i]+1` 666 ms / 结果表 256 MB；内核同规模扫描 9 ms / 80 MB。另：`ps-ops` 的 `add`/`scale`/`moving_avg` 已有实现但**未导出 C ABI**，且是 f32（通道数据是 f64），无论选 A 还是 B 都要先解决这个出口与精度问题。

## 13. 非目标

- 不做脚本 IDE：无 LSP、无 App 内断点 UI、无包管理、**无脚本编辑器**（编辑器一律外部）。
- **不做"算子"抽象**：没有文件头 schema、没有注册表、没有参数面板、没有版本机制。
- 不做脚本管理 UI：目录就是文件，列表就是 `ls`。
- 调试能力止于"定位 + 复现"；断点单步靠外部调试器连子进程（P2 可选）。
- 不把 Python 接进数据面（理由见 §3.5）；Python 只做离线外部工具。
- 不接管绘图侧的曲线变换（归 plotting/filter）。
- 不开放自由文件/网络访问（只经白名单 API 传路径）。
- 不暴露 `QWidget*` / Qt 对象 / `PsDataset*` 给脚本。
- 不承诺全局回滚；只给补偿动作与操作清单。
- 不做 UI 控制（菜单、Dock、设置项自动化）。

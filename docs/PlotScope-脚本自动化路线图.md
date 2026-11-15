# PlotScope 脚本自动化路线图

> 状态：**规划中，暂不实施**。本文只定义方向、边界与分阶段计划，不代表已实现能力。
> 触发背景：现有“脚本 · Lua 5.4”Dock 只能绑定 1–4 条曲线做派生计算，无法操作数据源、画布、曲线对象，也不适合作为 App 自动化入口。

## 1. 定位

脚本能力不是 MATLAB Command Window 的复刻，而是 **PlotScope 的自动化层**：

- 读取整个 App 的对象：数据源、通道、页、画布、曲线、时间标签
- 修改绘图状态：增删曲线、样式、坐标轴、页/画布
- 做临时分析：统计、峰值、相关、滤波、重采样
- 后续扩展为数据管线与批处理：派生通道、变换配方、加载/导出、实时源控制

目标不是“多写几个 Lua 函数”，而是补一层稳定的 **对象模型 + 命令总线 + 事务**。

## 2. 能力分层

| 层 | 能力 | 默认开放 | 风险 |
|---|---|---|---|
| L0 读 | 枚举源/通道/画布/曲线，取任意通道数据，统计与峰值 | 是 | 低 |
| L1 改绘图 | 新建页/画布、加删曲线、改样式、改 axes、加时间标签 | 是 | 中，需要撤销 |
| L2 数据管线 | 派生通道写回源、注册变换、持久化 recipe | 需显式授权 | 高，涉及数据一致性 |
| L3 App 自动化 | 加载/关闭源、实时源起停、录制、导出、批处理 | 需显式授权 | 高，涉及异步与外部副作用 |
| L4 UI 控制 | 菜单、Dock、设置、截屏 | **不开放** | 不稳定且无必要 |

**实施顺序：L0 + L1 → L2 → L3。L4 不做。**

## 3. 架构约束

### 3.1 对象句柄，不暴露 QWidget

脚本拿到的是稳定句柄，不直接持有 `PlottingCanvas*` / `SourceData*`：

```lua
local src = ps.app.source("demo_flight.ulg")      -- source handle
local ch  = ps.app.channel(src, "attitude.roll")  -- channel handle
local cv  = ps.app.canvas("车辆总线 · plot")       -- canvas handle
```

句柄在对象删除后失效；脚本再访问返回明确错误，不产生悬空指针。

### 3.2 所有写操作走命令总线

UI 点击和脚本调用同一条命令：

- `AddSeriesCommand`
- `RemoveSeriesCommand`
- `SetSeriesStyleCommand`
- `SetAxesCommand`
- `AddCanvasCommand`
- `AddPageCommand`
- `AddTimeMarkerCommand`

命令负责：状态更新、属性面板刷新、撤销栈、日志。脚本不得直接调 UI 方法绕过状态同步。

### 3.3 一次运行 = 一个事务

- **预览**：所有写操作进入 staging，画布显示临时结果；脚本失败或用户取消则整体回滚。
- **应用**：commit 到文档状态，并进入撤销栈。
- staging 期间禁止异步外部副作用（加载/删除/录制）；这类操作走 L3 作业模型。

### 3.4 异步作业

加载文件、连接实时源、导出、批量处理不能同步阻塞脚本：

```lua
local job = ps.app.load("flight.ulg")
ps.app.wait(job, 30)          -- 或 ps.app.onDone(job, fn)
```

作业有状态、进度、错误和取消入口；脚本超时不影响主线程。

## 4. API 草案（P1）

### 4.1 读

```lua
ps.app.sources()                     -- [{id,name,alias,path,format,live,channels}]
ps.app.channels(source)              -- [{name,unit,count}]
ps.app.channel(source, name)         -- {name,unit,time,value,sampleRate}
ps.app.pages()                       -- [{title,index}]
ps.app.canvases()                    -- [{title,page,series,active}]
ps.app.series(canvas)                -- [{name,label,color,width,style,marker,axis,...}]
```

### 4.2 改绘图

```lua
ps.app.addSeries(canvas, {
  name = "roll_rate",
  time = t,
  value = v,
  color = "#f2b134",
  width = 2,
  style = 0,
  marker = 1,
  axis = 0,
})

ps.app.setSeries(canvas, "roll_rate", { width = 1, style = 1, color = "#58a6ff" })
ps.app.removeSeries(canvas, "roll_rate")
ps.app.clearSeries(canvas)

ps.app.setAxes(canvas, {
  xlabel = "time / s",
  ylabel = "roll / deg",
  gridX = true,
  gridY = false,
  minorGrid = true,
  box = true,
  flipY = false,
  legendPos = 3,
  fontSize = 12,
})

ps.app.addCanvas({ title = "振动细节" })
ps.app.addPage({ title = "第 10 秒故障" })
ps.app.activateCanvas("振动细节 · plot")
ps.app.addTimeMarker({ time = 10.0, text = "故障起点" })
```

### 4.3 分析

```lua
ps.stats(v)                  -- n/min/max/mean/median/rms/std
ps.peaks(v, threshold)       -- 峰值索引
ps.movingAvg(v, n)
ps.derivative(v, t)
ps.integral(v, t)
ps.resample(v, t, hz)
ps.correlate(a, b)
```

### 4.4 输出与持久化

- 脚本本身随 `.psl` 保存，可命名、可复用。
- 脚本生成的曲线标记为“脚本生成”，默认不写回源。
- L2 派生通道才写入 source 和 recipe；重启可重放。

## 5. 分阶段计划

### P1：对象骨架与绘图控制

范围：

- L0 读 API
- L1 写 API（曲线/画布/axes/时间标签）
- staging / preview / apply 事务
- 对象浏览器：脚本 Dock 内列出 sources/channels/canvases，可点击插入代码

验收：

- 脚本能列出所有源和通道，读取任意通道数据
- 脚本能向指定画布加曲线、改样式、改 axes
- 预览修改可整体回滚；应用后可撤销
- 脚本失败不留下半改状态

### P2：数据管线

范围：

- 派生通道写回 source
- 变换 recipe 持久化
- `.psl` 保存脚本、脚本生成物和 recipe
- 重开工程可重放

验收：

- 脚本生成的派生通道重启后仍可复现
- 删除源时相关 recipe/派生通道一致清理
- 同一 recipe 重放结果与首次运行一致

### P3：App 自动化

范围：

- 加载/关闭源
- 实时源起停、录制开始/停止
- 导出数据/图片
- 批处理：脚本对多个文件顺序执行
- 作业状态、进度、错误、取消

验收：

- 脚本可完成“加载文件 → 分析 → 出图 → 导出”的无人值守流程
- 作业失败可回滚或明确部分完成状态
- 不在 UI 线程执行长任务

## 6. 非目标

- 不做通用 IDE：不提供完整 LSP、调试器、包管理。
- 不开放任意文件/网络访问；需要时走显式授权 API。
- 不照搬 MATLAB 全部函数；只做飞控数据分析常用集。
- 不暴露 QWidget 指针或 Qt 对象给脚本。
- 不允许脚本绕过命令总线直接改内部状态。

## 7. 风险与取舍

| 风险 | 处理 |
|---|---|
| 脚本改坏工程 | 事务 + 撤销 + 预览默认只读 |
| 大数组跨 Lua/Qt 复制卡顿 | 通道数据用只读视图/句柄，按需切片，不整包复制 |
| 长任务阻塞 UI | L3 作业模型，主线程只做提交 |
| API 膨胀难维护 | 统一 `ps.app.<verb>` 命名，命令与 API 一一对应 |
| Lua/Python 能力不一致 | 先统一对象 API，再决定 Python 是否只做数据计算 |
| 脚本持久化缺失 | 脚本随 `.psl` 存源码，生成物标记来源 |

## 8. 对现有脚本 Dock 的处理

当前 Dock 保留为“快速派生计算”入口，不直接升级成自动化控制台。P1 落地后：

- Dock 增加“对象”页签/对象浏览器
- 运行按钮区分“预览（只读）”和“应用（提交）”
- 脚本库支持命名保存
- 默认脚本从“单曲线微分”改为“读取通道 → 计算 → 加曲线”的完整示例

## 9. 开工顺序

1. 定义 App 对象模型与命令总线接口（不碰 UI 细节）
2. 实现 L0 只读 API + 对象浏览器
3. 实现 L1 命令与事务
4. 补测试：句柄失效、预览回滚、应用撤销、脚本错误恢复
5. 再评估 L2/L3，不在 P1 混入数据写回和异步作业

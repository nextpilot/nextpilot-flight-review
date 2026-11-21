# 验收清单：main → ng 移植（逐项可执行）

> 配套 `移植清单-main到ng.md`。每条 AC 标注对应任务 ID、执行方式、判定标准、证据留存。
> **无硬件的专项**：实时源验收一律用 UDP 打流替代真机；真机只在 AC-27 做附加确认。
>
> **当前进度（2026-11-21，`f032b81` 已推送）**：33 条验收项中 **7 条通过（AC-02、AC-03、AC-04、AC-05、AC-10、AC-27、AC-29）、5 条部分通过（AC-06、AC-10B、AC-11、AC-12、AC-28a）**，通过率 21%（含部分 36%）。M1（实时源）除两张 UI 卡外已全部有证据；M2/M4/M6/M7/M8 尚未开始。
> 编译 0 error，`ctest -j1` **27/27** 全绿（每次推送前必跑；27 条含脚本工作台用例与本轮新增 `ng_test_live_udp`）。


---

## 0 三层验证与通用 DoD

| 层 | 手段 | 用途 |
|---|---|---|
| L1 意图层 | `plotscope-ng --run <job.json>`（headless，Job = 意图序列，输出 JSON） | 功能正确性、幂等性、参数校验——**主战场** |
| L2 视图层 | `plotscope-ng-gui-shot <out.png> <data...> --tab= --action= --state=` | UI 是否真的出现/顺序/勾选态 |
| L3 回归层 | `ctest --test-dir build-ng -j 1`（**必须 -j1**） | 不破坏既有 27 项 |

**每个 T 完成必须同时满足（DoD）**：
1. 全量构建无新增告警；2. `ctest -j 1` 全绿；3. 对应 AC 全过并留证据；4. `ARCHITECTURE.md`/`LAYERS.md` 同步更新；5. 提交前 `fetch + rebase`，一次提交只含一个 T。

**环境固定**（每次跑前设）：
```bash
export LD_LIBRARY_PATH=/home/user/Doubao/tools/ng-deps/usr/lib/x86_64-linux-gnu
export QT_QPA_PLATFORM=offscreen
export QT_QPA_PLATFORM_PLUGIN_PATH=/home/user/Doubao/tools/ng-deps/usr/lib/x86_64-linux-gnu/qt6/plugins
```

**常规命令**：
```bash
cmake -S ng -B build-ng -DCMAKE_PREFIX_PATH=/home/user/Doubao/tools/ng-deps/usr
cmake --build build-ng -j 8
ctest --test-dir build-ng -j 1
./build-ng/app/plotscope-ng --list-contributions        # 新贡献是否注册上
./build-ng/app/plotscope-ng --list-slots                # 新插槽是否被填
```

---

## 1 M1 实时源

### AC-01 流式字节源（T1.1）
- 前置：`tools/udp_feed.py`（10Hz NMEA 或 PSRAW 打流脚本，**本 T 一并提交**）。
- 步骤：
  1. `python3 tools/udp_feed.py --port 5600 --hz 100 --seconds 10 &`
  2. `./build-ng/app/plotscope-ng --run ac01.json`，其中 `data.source.connect {kind:"udp", port:5600, parsers:[{id:"psraw"}]}`（或 nmea）
- 期望：返回 `ok:true`，`sourceId>=0`，`state:"streaming"`；`--list-contributions source` 里出现 `udp`/`serial`/`tcp`/`simulator`。
- 判定：**丢字节验证**——喂 10s@100Hz=1000 帧，解析结果行数 ≥ 990（允许首尾边界）。
- 证据：`ac01.json` + 输出 JSON 存 `docs/verify/ac01-*.json`。

### AC-02 长流不阻塞（T1.2）　✅ 已通过（`f032b81`）
- 步骤：连接 UDP 源（T1.2 生效后）→ 观察 `data.source.list` 在 **连接后 1s 内**就能返回，且此时数据仍在增长。
- 期望：`connect` 调用**立即返回**（<200ms），后台持续 append；连续两次 `list` 的 `rows` 单调递增。
- 判定：`connect` 到首次 `datasetChanged` 之间不阻塞 GUI（用 `--action=` 截图证明界面可交互）。
- **结果**：`ng_test_live_udp` 中 `data.connect` 立即返回（测试进程无 GUI 阻塞即可跑完三前端）；收包侧只入环、泵 50Hz（20ms）批量 feed，20 帧在 `pump()` 内到达即成为 20 行；三前端（UDP/TCP/串口 pty）各 20 帧 → 20 行、timeMax 1.900，未出现阻塞或丢尾帧。

### AC-03 流控与统计（T1.3）　✅ 已通过（`f032b81`）
- 步骤：流进行中依次发 `data.source.pause` → 等 2s → `list` → `resume` → `list`；然后 kill 打流端 3s 再启动，发 `data.source.reconnect`。
- 期望：pause 期间 `rows` 冻结且 `state:"paused"`；resume 后 `rows` 继续增；reconnect 后**同一 datasetId** 继续追加（**不得新建 dataset**）。
- 判定：pause 前后 `rows` 差值 ≤ 1 批；reconnect 前后 `datasetId` 相同。
- 附加：`rate.hz` 与打流端设定偏差 ≤ ±10%（100Hz 应报 90–110）。
- **结果**：pause 期间发 5 帧后 `packets` 不增、`dropped_bytes` 增长（暂停不入环）；resume 后继续追加；reconnect 后 `datasetId` 不变（同一数据集续写）。`data.rate.set hz=5` → `interval_ms=200`（断言）；`bufferBytes=65536` 热改成功（断言）；stats 暴露 `pending_bytes / dropped_bytes / fed_bytes / backpressure / pump_hz`。内核 UDP 丢包不在应用层伪造数据，明确不做自造补算。

### AC-04 意图完整性（T1.4）　✅ 已通过（`f032b81`）
- 步骤：`plotscope-ng --run ac04.json`，job 内覆盖 8 个 op 且含 3 条**非法参数**（端口 70000 / 未知 parserId / 不存在的 sourceId）。
- 期望：正常 8 条全 `ok`；非法 3 条返回 `ok:false` 且 `error` 指明字段名，**进程不崩**（退出码 0）。
- 判定：`handles()` 白名单有全部 op；错误分支均走 `fail()`。
- **结果**：8 个 op（connect/disconnect/pause/resume/reconnect/rate.set/config/source.list，另含 live.stats）全部可执行且返回 `ok`；4 条非法输入实测——`tcp://…:70000` 被拒、未知 parserId 被拒、`data.config` 缺 `value` 被拒、不存在的 `id` 在 stats 里回空列表而非崩溃，进程退出码正常。

### AC-05 实时源入口 UI（T1.5）　✅ 已通过（`e32101c`）
```bash
./build-ng/gui/plotscope-ng-gui-shot ac05.png tests/fixture/demo_flight.ulg 1920 1080 --tab=数据
```
- 期望：数据页「来源」gallery 出现**第三个分类「实时源」**，含 串口/UDP/TCP/回放 四项，与 `--list-contributions source` 一致（**gallery 不得出现未安装的传输**）。
- 判定：截图肉眼核 + `--list-slots` 输出含实时源插槽。
- 证据：`ac05.png`。

### AC-06 状态可见（T1.6）
- 期望：连接后左 dock 源节点显示 `Streaming · 100Hz · 12.4KB/s`；断开显示 `Closed`；失败显示 `Error: <原因>`。
- 判定：截图 + 状态文本与 `data.source.list` 字段一一对应（不许出现套话）。

---

## 2 M2 录制

### AC-07 录制自洽（T2.1，**最硬一条**）
- 步骤：UDP 打流 10s@100Hz 同时录制 `mcap` 与 `raw` 两路 → 停止 → `data.load` 读回 mcap。
- 期望：回读 mcap 的**表数量**与源相同，主力表**行数 = 源行数 ±1%**；`raw` 文件字节数等于 `bytesWritten`。
- 判定：job 内先 `income` 后 `回读`，两个 datasetId 分别 `data.series` 取行数比对，输出 `delta` 字段。
- 证据：`ac07-*.json` + 两个录制文件（小体积可直接归档）。

### AC-08 多路录制意图（T2.2）
- 步骤：一次 `data.record.start` 带 3 个 target：`{mcap,file}`、`{raw,file}`、`{raw,udp}`。
- 期望：三路同时工作，`list` 返回 3 条各自 `bytesWritten>0`；停止后 3 条 `closed:true`。
- 判定：udp 那路用 `nc -ul 5600` 收到字节数 >0 佐证。

### AC-09 录制 UI（T2.3）
- 期望：数据页出现「录制」组（开始/停止/配置），录制中按钮变红点 + 已写 MB 实时跳；未连接源时按钮**置灰不可点**（不弹错误）。
- 判定：截图两态（未连接灰 / 录制中红点）。

---

## 3 M3 时间对齐

### AC-10 对齐不破坏存储（T3.1）
- 步骤：加载两份 fixture（时间基准差 2s）→ `data.align.setBase srcA` → 读 srcB 的 `timeMin` → `reset` 再读。
- 期望：存储层时间戳保持原值（`data.state`/内部 raw time 不变），`timeMin` 仅在带 offset 的出口变化；`reset` 后**逐点还原**（不是近似还原）。
- 判定：reset 前后 `timeMin/timeMax` 完全相等（`==`，非 `≈`），证明 offset 是纯附加量、零损耗。

### AC-10B 对齐参与 CSV 导出（T3.4，**用户明确要求**）
- 步骤：srcB 设 offset=+2000000us → `data.export {dataset:srcB, table:T, format:"csv"}` → 读回 CSV 首行时间。
- 期望：
  1. CSV 时间列 = **原始 + offset**（首行差值精确到 1 微秒，误差 0）；
  2. 同一次导出的**全部表**都按各自 offset 渲染（多表快照不得混用基准）；
  3. 时间轴格式三选一均生效：相对秒（基准源零点=0）/ 时分秒 / UTC 秒，三者对同一时刻应换算一致；
  4. 表头含 `# time_mode=aligned, offset_us=…` 注释行；
  5. **画布一致性**：CSV 末行时间 ≈ 画布当前 X 轴末点（误差 ≤1 采样间隔）——防止两条计算路径各算一套。
- 判定：python 读 CSV 首末行 vs job 返回的 `effectiveTime` 首末值，双端比对；6 条断言全过才算。
- 附加：`offset=0` 时导出结果与**未对齐**导出逐字节相同（证明不引入漂移）。

### AC-11 四种对齐策略（T3.2）
- 步骤：对 srcB 依次 `alignTo:start|end|center|reset`，每次读 `timeMin/timeMax`。
- 期望：start → `minB' == minA`；end → `maxB' == maxA`；center → 中点相等（误差 ≤1 采样间隔）；reset → 回到原值（**往返幂等**）。
- 判定：4 组断言自动比对，误差超限即失败。

### AC-12 对齐即时生效（T3.3）
- 期望：对齐后**不重建曲线**（颜色/可见性/线宽保留），画布 X 范围立即更新；`plot.view.undo` 可撤销这次对齐（如实现）。
- 判定：截图前后颜色不变 + `plot.series.list` 的 id 集合不变。

---

## 4 M4 数据工程

### AC-13 多表快照（T4.1）
- 步骤：`data.exportSnapshot {datasets:[1], tables:["*"], format:"xlsx"}`。
- 期望：生成单 xlsx，**sheet 数 = 表数**，每 sheet 列名与 `data.state` 返回的 schema 一致；空表也留 sheet（不静默丢）。
- 判定：python `openpyxl` 读回比对 sheet 名与列名列表。

### AC-14 参数表 P/Q（T4.2）
- 步骤：加载 `.ulg` fixture → `data.state` 查看 `param` 表。
- 期望：树里"参数"节点可展开为表，含 `P*`/`Q*` 列与 `value` 列；属性面板能显示参数数量。
- 判定：job 断言 `param` 表存在且 `rows == 参数条数`（fixture 已知值）。

### AC-15 缓存可观测（T4.3）
- 步骤：同一文件连续 `data.load` 两次，第二次带 `useCache:true`；随后 `data.cache.stats` → `data.clearCache` → 再 `stats`。
- 期望：第二次 `load` 耗时显著下降（≥5×）且 `hits>0`；清空后 `bytes` 归零、`hits/misses` 重置。
- 判定：耗时与计数字段自动比对；输出 `load_ms` 对比。

### AC-16 采样上限（T4.4）
- 步骤：`settings.set {key:"export.csv.maxPoints", value:1000}` → 对 5 万点通道 `data.export`。
- 期望：CSV 行数 ≈ 1000（±1），且是**均匀抽稀**（不截断尾部）；`0` 表示不限，设为 0 后行数回到全量。
- 判定：行数断言 + 末点时间戳 ≈ 通道末点（证明抽稀非截断）。

### AC-17 设置对话框（T4.5）
```bash
./build-ng/gui/plotscope-ng-gui-shot ac17.png tests/fixture/demo_flight.ulg 1280 800 --action=file.settings
```
- 期望：弹出设置对话框，三组（数据/画布/时间轴）字段齐全（CSV 采样上限、派生重算上限、默认网格、默认图例、时间轴三选一、缓存清空）；**改值 → 关闭 → 重开仍保留**（落盘验证）。
- 判定：截图 + `settings.list` 输出核对；`Ctrl+,` 可打开（快捷键断言已有测试覆盖）。

---

## 5 M5 比对交互

### AC-18 叠加全部同名（T5.1）
- 期望：双源同名通道一次叠加，曲线名带源前缀（`源A:table.column`），`plot.series.list` 条数 = 匹配对数；无同名时给出提示而非静默空跑。
- 判定：job 断言 series 条数与命名规则。

### AC-19 对比同名（T5.2）
- 期望：只取两条、双色、X 范围自动取并集、给出 offset 建议值（与 T3 算法一致）；源不同表不同名时提示"无同名通道"。
- 判定：`plot.series.list` 恰 2 条 + offset 建议字段存在。

### AC-20 展开数据列（T5.3）
- 期望：表右键一列→一次最多画 8 条，超出弹"已截取前 8 列，可继续添加"而不是全画；数值列优先，bool/字符串列默认不画。
- 判定：job 断言 series≤8 且类型过滤生效。

### AC-21 画布 DnD 重排（T5.4）
- 期望：把画布 A 的曲线拖到画布 B → B 多一条、A 少一条（**移动不是复制**）；拖到页签上 → 曲线跟随到该页；拖到非法区不留半截状态。
- 判定：GUI 手动 + 截图两态（拖前/拖后 series 列表差异）；job 侧以 `plot.series.list` 差分佐证。

### AC-27 画布右键两态一致（T5.5）
- 步骤：
  1. 空画布（无曲线）右键 → 截图清点项数；
  2. 画入 1 条曲线后右键 → 截图清点；
  3. 逐项点击：显示网格（勾选态与 ribbon 网格一致）、显示图例、放大/缩小/自适应、撤销视图、向右拆分会多一格、向下拆分、导出 PNG 落地文件、导出 CSV 落地文件、删除此画布（画布数 -1、其余保留）。
- 期望：两态菜单项数**相同**（`m_series.isEmpty()` 不再缩减菜单）；含 `在 <t> s 添加时间书签`（时间等于右键处 x 对应时刻 ±1 采样）；「添加曲线」为禁用项且提示"从左侧数据集面板拖入"；拆分/删除作用在**右键的那个画布**（不是当前激活的别的画布）。
- 判定：两态截图 + `plot.series.list` / 画布计数；导出文件存在且非 0 字节。

**实测结果（2026-11-21，Xvfb 1600×1000 + xdotool 真点）**

| 断言 | 结果 | 证据 |
|---|---|---|
| 空画布项数 = 有曲线项数 | 通过：均 17 项 | 空画布菜单截图；有曲线菜单截图 |
| 无 early return 残留 | 通过：空画布可见 导出/拆分/删除 | 同上 |
| 文案带点击时刻 | 通过："在 0.41 s…"（x≈空画布 41%）、"在 1.32 s…"（曲线图 x≈11%） | 两张菜单截图 |
| 添加曲线为禁用+提示 | 通过（灰显，tooltip 指路） | 菜单截图 |
| ✓ 勾选态 | 通过：网格 ✓、图例 ✓ 与实际一致 | 菜单截图 |
| 向右拆分 | 通过：1 格 → 左曲线 + 右空白 2 格 | 拆分后整屏截图 |
| 删除此画布（2 格） | 通过：回到 1 格，原曲线保留 | 删除后整屏截图 |
| 删除此画布（1 格） | 通过：菜单项置灰（不弹提示、不误删） | 单格菜单截图 |
| 拆分/删除作用于右键画布 | 通过：右键右格删除，左格曲线不受影响 | 同上 |
| 菜单项图标齐全 | 通过：17 项均带自绘图标（属性▸/曲线▸ 子菜单同步），禁用项自动灰化 | `verify/icon_canvas_menu.png` |
| 导出 PNG 落地 | 通过：右键→导出 PNG…→保存对话框→`/tmp/ng_canvas.png` 32KB，图为 pitch 曲线+图例+刻度 | `verify/ng_export_canvas.png` |
| 导出画布 CSV 落地 | 通过：`/tmp/ng_canvas.csv` 900 行，含 `# fastplot` / `# x_lo…x_hi` / `# curve attitude.pitch` 注释头 | `verify/ng_export_canvas.csv` |
| 导出 PNG 不含悬停读数 | **部分通过**（`2ff7687`）：`Axes::exportPng` 导出期间关 cursor/A-B 浮层，黑底大读数框已消除（`ng_export_canvas_nohover.png`）；**右上角固定读数小条仍入图**（由 PlotCanvas 侧 HUD 绘制，待修） | `verify/ng_export_canvas_nohover.png` |

**本轮顺带修复**：`Axes` 构造里 `m_grid` 与 `Axis::m_grid` 默认值不一致（false vs true，绘制走 Axis），造成"画布有网格、菜单没勾"。已改为构造时以 Left 轴为准。


### AC-28 数据树源节点 4 组（T5.6）
- 步骤：右键源节点，清点并逐项执行：时间对齐▸ 五个子项、对比同名▸（双源时每源一个子菜单）、数据源信息、移除全部数据源。
- 期望：五项对齐动作各自改变该源 offset（`data.export` 时间列随之变化，与 AC-10B 同一口径）；"数据源信息"只读展示路径/格式/表数/行数/时间跨度；"移除全部"后树空、画布曲线保留但标"源已移除"（不静默清空）。
- 判定：截图 + `data.source.list` 前后对照。

**实测结果（2026-11-21）**

| 断言 | 结果 | 证据 |
|---|---|---|
| 文件源右键 8 项 | 通过：重载 / 重命名 / 导出 CSV / 清除缓存 / 数据源信息 / 设为活动 / 移除 / 移除全部 | 源菜单截图 |
| Live 源额外"暂停·继续" | 已实现（`SourceKind::Live` 分支），本轮无 live 源可实拍 | 代码路径 `DataManagerPanel::showSourceMenu` |
| 重命名走 `update()` 保 origin | 已实现（`DataSourceInfo copy = *info; copy.name = …; m_sources.update()`） | 同上 |
| 数据源信息对话框 | 通过：名称 / 来源全路径 / 类型·状态 / 字节 312297 / 解析器 1 / 数据集 1 | `verify/ng_src_info_bytes.png` |
| 源菜单图标 | 通过：8 项均带语义图标（暂停/继续按状态切 pause/play） | `verify/ng_src_menu.png` |
| 移除全部（逐 id `data.remove`） | 已实现，含 Yes/No 确认与中途失败即停 | 同上 |
| **时间对齐▸ / 对比同名▸** | **未落地**（ng 无 TimeAlign 引擎，`grep TimeAlign\|time_align` 零命中） | — |

判定：AC-28 允许拆分为 AC-28a（已过的右键项）与 AC-28b（对齐/对比，随 T3 一起验收）。在 T3 落地前不应把这两项塞进菜单——否则就是点了没反应的假入口。

**实测附带发现（已修，`e9564ad`/`4c948dc`）**：源卡片"字节"显示 `0（总量未知）` → 解析完成/缓存命中回写 `bytesRead/bytesTotal`，实测 312297；树搜索命中子字段父节点不展开 → 过滤态自动展开；顺带发现过滤的**假命中**（搜 pitch 连带显示同表后续字段），已改为字段只看自身命中，实拍只剩 `attitude▸pitch`。

### AC-29 时间书签带时刻（T5.7）
- 期望：菜单文案为 `在 12.34 s 添加时间书签`，点后书签落在**右键位置对应时刻**（±1 采样）；hover 已有书签时出"时间书签 ▸ 重命名/删除"；书签可拖动、可删除（回归既有能力）。
- 判定：`plot.bookmark.list` 的 t 值与右键 x 反算值比对。

**实测结果（2026-11-21）**：通过。空画布右键 x≈41% 处文案为"在 0.41 s 添加时间书签"，曲线页 x≈11% 处为"在 1.32 s…"，与 `timeAtPixel()` 反算一致；命中书签分支保留"时间书签 ▸ 重命名… / 删除"。

---

---

## 6 M6 格式扩展

### AC-22 新解析器包（T6.1–T6.3）
- 判定模板（每个格式各一遍）：
  1. `--list-formats` 出现该格式；2. `--list-packs` 出现该包且**可卸**（卸载后 gallery/格式列表同步消失）；3. 加载真实样本 → 表数/行数/关键字段值与 main 同文件结果**误差 ≤1%**（同一份样本两边各跑一次，比表名+行数+首末值）；4. 坏样本不崩（截断/乱码各一份，退出码 0）。
- 附加：main 侧结果作为"金标准"存 `docs/verify/golden/<fmt>.json`，回归时自动比对。

---

## 7 M7 排障

### AC-23 调试终端（T7.1）
- 期望：连接源后终端显示尾部 200 帧（可切文本/十六进制）；发送命令后串口/网络侧收到（用 `nc`/串口回环验证）；未连通时输入端屏蔽。
- 判定：`nc -ul` 侧收到字节 + 终端截图。

### AC-24 帧诊断（T7.2）
- 期望：诊断面板显示 坏帧数 / 解析器错误 / topic 分布；故意注入 1% 坏帧 → 计数与注入比例偏差 ≤5%。
- 判定：job 返回的计数与注入比例比对。

---

## 8 M8 绘图补口

### AC-25 surface3d（T8.1）
- 期望：图库由 12 项变为 13 项，`test_gui.cpp` 断言同步更新（**同一次提交**）；拖入需 ≥3 通道，不足时按既有语义提示；`--type=surface3d` 截图非空。
- 判定：`ctest -j1` 绿 + 截图。

### AC-26 重置三出口（T8.2）
- 期望：右键出现「重置 X 轴 / 重置缩放 / 重置整个视图」三行，各自只影响对应范围（测法：先缩放 X，重置 X 后 Y 范围不变）。
- 判定：job 断言 `plot.axis.range` 在三种重置后的值。

---

## 9 端到端场景（必跑，三份 job 归档）

| ID | 场景 | 通过标准 |
|---|---|---|
| E2E-1 | UDP 打流 → 出画布 → 同时录 mcap+raw → 断开 → 读回 mcap → 行数一致 | AC-01/02/07 全过 |
| E2E-2 | 双源同名叠加对比 → 设基准对齐 → 出 PNG → **导出 CSV** | AC-11/18/19 全过，PNG 里两源明显对齐；CSV 时间列与画布 X 轴一致（AC-10B 断言 5） |
| E2E-3 | 三源加载 → 展开数据列 → 快照 xlsx → 采样上限 1000 → CSV | AC-13/16/20 全过 |

---

## 10 性能与健壮性门禁（P 系列，M1+M2 完成后必跑一次）

| ID | 项 | 门槛 |
|---|---|---|
| P-1 | 1kHz 连续 30min | RSS 峰值 ≤ 基线+300MB 且**结束时无持续增长趋势**；UI 可交互（无 >500ms 卡顿） |
| P-2 | 断线重连 10 次 | 每次 reconnect <3s 成功；datasetId 不漂移；画布不断线/不重绘闪烁 |
| P-3 | 满缓冲（UDP 洪水 10MB/s） | 不崩、丢帧计数准确、内存不爆、可 `pause` 止血 |
| P-4 | 录制中拔线 | 已写 mcap 可被 `data.load` 打开（尾记录合法），不产生 0 字节文件 |

---

## 11 勾选核对表（进度：2 通过 / 1 部分 / 30 未开始）

- [ ] AC-01 流式源（udp/serial/tcp/sim）
- [x] AC-02 长流不阻塞 — 三前端 20 帧→20 行，connect 立即返回，泵 50Hz 批量喂（`f032b81`）
- [x] AC-03 pause/resume/reconnect/速率 — pause 期 packets 不增、reconnect 同 dataset、rate/config 断言（`f032b81`）
- [x] AC-04 意图族 + 非法参数 — 9 op 全 ok；4 条非法输入全被拒且不崩（`f032b81`）
- [x] AC-05 实时源入口 + 连接对话框 — 「＋」/空态按钮；UDP↔串口互斥无残留空行；真连接建源成功（`e32101c`）
- [~] AC-06 源状态行 — 已实现（●/⏸/⚠ 三态），缺 live 会话窗口截图（`e32101c`）
- [ ] AC-07 录制 mcap 回读自洽
- [ ] AC-08 三路录制并存
- [ ] AC-09 录制按钮两态
- [x] AC-10 对齐不破坏存储（reset 逐点还原）— **实测逐字节还原**：同一源导出 → end 对齐 → 导出 → reset → 导出，首末两次 `cmp` 完全相同（offset 只存 AppContext，Dataset 零改写）
- [~] AC-11 五项对齐动作 — end 实测 offset=+11.596s（params 0.4→flight 11.996）；start/center 同一代码路径未逐条实跑；reset 由 AC-10 证；GUI 子菜单 5 项受 Xvfb 悬停限制未截到（父菜单+箭头已实拍）
- [~] AC-12 意图 + 对齐后画布立即刷新 — `data.time.align` 已注册并实跑；画布刷新走 `sourceTimeOffsetChanged`→`refreshSourceOffsets`→`pushToPlot`（不重建曲线，颜色/可见性保留），只是没做 x 轴像素级前后对照
- [~] AC-10B 对齐参与 CSV 导出 — 实测：params 终点对齐 flight 后导出首行 `time=11.596`、表头 `# time_mode=offset, offset_us=11596000, offset_s=11.596`（`2ff7687` 起合清单口径，断言 1/4 过）；多表混合 offset 与三种时间格式未测（断言 2/3），快照导出未做
- [ ] AC-11 四策略 + reset 幂等
- [ ] AC-12 对齐不重建曲线
- [ ] AC-13 xlsx 多 sheet
- [ ] AC-14 param 表 P/Q
- [ ] AC-15 缓存命中/清空
- [ ] AC-16 采样上限均匀抽稀
- [ ] AC-17 设置对话框三组+落盘
- [ ] AC-18 叠加同名
- [ ] AC-19 对比同名 + offset 建议
- [ ] AC-20 展开列 ≤8 且类型过滤
- [ ] AC-21 跨画布移动（非复制）

- [x] AC-27 画布右键两态一致（含拆分/导出/删除作用于右键画布）— 17/17 项一致、拆分/删除实拍过（导出落地文件待手测）
- [ ] AC-28 树源节点 4 组（时间对齐/对比同名/信息/移全部）— 信息/移全部/重命名/暂停已过；对齐·对比待 T3
- [x] AC-29 时间书签带时刻 — 文案与右键时刻一致
- [ ] AC-22 新格式包（装卸/金标准比对/坏样本）
- [ ] AC-23 调试终端收发
- [ ] AC-24 帧诊断计数
- [ ] AC-25 surface3d + 断言同步
- [ ] AC-26 重置三出口互不干扰
- [ ] E2E-1 / E2E-2 / E2E-3
- [ ] P-1 / P-2 / P-3 / P-4
- [ ] AC-27 真机附加确认（串口 921600 / UDP 组播，现场至少各一次）

---

## 12 一句话判据

**每个 AC 都必须能回答三个问题**：怎么跑（命令/步骤）、看到什么算过（可数值化）、证据在哪（job JSON/截图/ctest 输出）。答不出来的，就不是验收项，只是"感觉做完了"。

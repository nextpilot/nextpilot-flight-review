# roadmap：把 main 的能力搬进 ng

本目录（`docs/roadmap/migration/`）是「main → ng 移植」的唯一权威计划与验收记录。代码在 `nextgen-architecture` 分支上同步推进，本目录随每次落地一并提交，所以**看板上的勾与提交号是一一对应的事实**，不是计划口径。

## 文档索引

| 文件 | 作用 | 什么时候看 |
|---|---|---|
| `01-移植清单-main到ng.md` | 33 张任务卡（M1–M8）+ 每卡实现要点 + 依赖顺序 | 想知道"要做什么、依赖谁" |
| `02-验收清单-main到ng.md` | AC-01…AC-29，每条带勾选状态与证据（测试名/截图/CLI 实跑） | 想知道"做没做到、凭什么说做到了" |
| `03-main-vs-ng-对比清单.md` | main 与 ng 的行为差异逐项对照（早期盘点，作为蓝本口径） | 想核对"main 长什么样、ng 差在哪" |

## 进度看板（截至 2026-11-21 晚，T4.1 落地后）

33 张卡：**完成 24 · 部分完成 1 · 未开始 8 = 73%（含部分 74%）**。编译 0 error，`ctest -j1` **30/30 全绿**。

| 里程碑 | 卡数 | 完成 | 部分 | 未开始 | 进度 | 状态一句话 |
|---|---|---|---|---|---|---|
| **M1 实时源** | 6 | 6 | 0 | 0 | **100%** | 三前端 + 字节环后台泵 + 统计 + 全套意图 + 连接对话框 + 状态行（live 绿点/录制红点）全部落地并截图 |
| **M2 录制** | 3 | 3 | 0 | 0 | **100%** | `data.record.start/stop/list` 双路 raw+mcap 实测落盘、mcap 回读字节级自洽、数据页「录制」组 + 状态行录制段 |
| **M3 时间对齐** | 5 | 5 | 0 | 0 | **100%** | 偏移收敛到 AppContext、5 策略、源右键入口、CSV 导出叠加、8×8 选格 |
| **M4 数据工程** | 5 | 3 | 1 | 1 | **60%** | T4.1 多表快照（自写 XlsxBook + `data.exportSnapshot`，源右键「导出全部表」Excel/CSV 两路，实拍 3000 行 → 1 sheet 8 列）、T4.3 缓存可观测、T4.4 采样上限；T4.5 设置对话框数据组（画布/时间轴组待接线）；T4.2 参数表未动 |
| **M5 比对交互** | 7 | 7 | 0 | 0 | **100%** | 12 项右键 + 书签 + 叠加/对比同名 + 展开数据列 + 同页曲线移动 + 源节点对齐▸/对比同名▸ 全收 |
| **M6 格式扩展** | 3 | 0 | 0 | 3 | 0% | mdf / rosbag / blf·dataflash·apbin，重写型 |
| **M7 现场排障** | 2 | 0 | 0 | 2 | 0% | 调试终端、帧诊断面板 |
| **M8 绘图补口** | 2 | 0 | 0 | 2 | 0% | surface3d、重置三出口 |

### M1 细项（当前唯一"半截"的里程碑）

| 卡 | 内容 | 状态 | 证据 |
|---|---|---|---|
| T1.1 | UDP / 串口 / TCP 字节源 | ✅ | `cb12809`；三前端各 20 帧 → 20 行、timeMax 1.900 |
| T1.2 | 字节环 + 50Hz 后台泵、EOF 不关源 | ✅ | `f032b81`；readyRead 只入环，泵 256KB/tick |
| T1.3 | pause/resume/reconnect、速率、丢帧 | ✅ | `f032b81`；`dropped_bytes` + `parse_errors` |
| T1.4 | 意图族（含 `rate.set` / `config`） | ✅ | `f032b81`；`hz`→`interval_ms=200`、`bufferBytes=65536` 有断言 |
| T1.5 | 数据页入口 + 连接对话框 | ✅ | `e32101c`；两态截图 `verify/ng_live_dlg_{udp,serial}.png` |
| T1.6 | 面板状态行：状态/速率/字节/错误 | ✅ | `826a4de`；live 绿点 `● 名称 · Hz · KB/s · N 包`，暂停 ⏸ / 错误 ⚠ / 背压标注；录制段红点前置（窄 dock 先看到"在录什么"）。截图见下 |

实拍（`--tab=数据` 时按类型 raise 数据源面板，左 dock 默认页是脚本库，见 `shot_gui` 注释）：

- 录制组：常用(5) → 录制(开始/停止) → 来源(CSV/PROTO/ULOG) 三组齐显，无折叠 `▾`。
- 状态行：`● raw 0.0 KB · ● live-demo · 0.0 Hz · 0.0 KB/s · 0 包`（红点 = 录制中，绿点 = live 会话）。
- 证据边界：状态行的**数字正确性**由 `ng_test_live_udp` 的 stats 断言覆盖（hz/kb_s/packets 实测）；截图证明的是**排版与两段共存**，一次性截图进程活不到收帧。

### M2 细项

| 卡 | 内容 | 状态 | 证据 |
|---|---|---|---|
| T2.1 | 录制编排器（源 → Recorder → 泵内写字节） | ✅ | `f91acf6`；`pumpLive` 里先录后解析（录制吃同一块字节），disconnect 自动收口防脏文件 |
| T2.2 | 意图 `data.record.start/stop/list` | ✅ | `f91acf6`；raw+mcap × file 两路同开，各 160B 落盘，stop 后 `recordings` 清空 |
| T2.3 | 数据页「录制」组 + 状态与已写字节 | ✅ | `826a4de`；状态行红点段读 `data.record.list` 的 open 条目 + bytes |

**M2 闭环已补（`cd5cc78`）**：mcap 的 Schema 记录改为自包含 `{"parser":"custom-protocol","options":{模板}}`；`data.load` 把 mcap 当**容器**（消息体拼回字节流 → 同一套摄取），实测录 20 帧 → 回放 `rows=20`、`timeMax=1.900`，与源一致。消息粒度仍是泵批次，但块边界切在帧中间也不影响（解析器吃流），所以不再是缺陷。
AC-08 三路并存也已实测（`95f58c3`：raw/file + mcap/file + raw/udp，udp 端收到 7 包 176B）。
遗留：录制中按钮无两态（红点在状态行）；未连接源时按钮未置灰。

## 口径与约定（避免下次又对不上）

1. **谁改代码谁改本目录**：一次落地 = 一次代码提交 + 一次 roadmap 更新，两者同批推送；看板数字与最新提交号必须一致。
2. **"完成"的定义**：代码落地 + 至少一条可重复执行的验证（单测断言 / CLI 实跑数字 / GUI 截图）。只有代码没有验证的，一律记"部分完成"。
3. **范围**：脚本页、脚本引擎、控制台**不在本次范围**（用户明确另行设计）。**流式业务解析器（MAVLink/OEM7 等）属业务方提供**，不在"底座"范围内，M6 各包同样另行立项。
4. **分支纪律**：只 rebase 到 `origin/nextgen-architecture`，**绝不** rebase/push 到 `origin/main`（那条线是 PJ4 迁移）。

## 验证命令（复制即用）

```bash
# 配置（build cache 的源目录是 ng/，不是仓库根）
cmake -S ng -B build-ng -DCMAKE_PREFIX_PATH=/home/user/Doubao/tools/ng-deps/usr
# 单 target 增量（秒级，首选）
cmake --build build-ng --target ng_test_live_udp -j 8
# 全量回归（必须 -j1）
LD_LIBRARY_PATH=/home/user/Doubao/tools/ng-deps/usr/lib/x86_64-linux-gnu \
QT_QPA_PLATFORM_PLUGIN_PATH=/home/user/Doubao/tools/ng-deps/usr/lib/x86_64-linux-gnu/qt6/plugins \
ctest --test-dir build-ng -j 1
```

GUI 重启必须 `pkill -x plotscope-ng-gu`（进程名被内核截断为 15 字符，`-x plotscope-ng-gui` 匹配不到，会叠实例、截图拍到旧窗口）。

无硬件验证走 `ng/tests/test_live_udp.cpp`：`QApplication(offscreen)` + `App::startup(false)` + `app.host().execute(intent)`，UDP 用 `QUdpSocket` 真发包、串口用 `openpty()` 造 pty 对，单次 3 秒出结果。

## 下一步顺序（建议，未拍板）

1. **录制自包含回放**：mcap schema 带模板 + `data.load` 按 schema 选解析器 → 才可能过 AC-07 的硬口径（读回行数）。这是 M2 唯一没验的一块。
2. **M4 剩余**：T4.1 多表快照导出、T4.2 参数表（M4 现在 50%）；T4.5 画布/时间轴设置组仍缺消费方，先不摆假入口。
3. **T4.1/T4.3/T4.4**：数据工程（参数表/缓存可观测/采样上限），与 T3.4 导出管线同批更省。

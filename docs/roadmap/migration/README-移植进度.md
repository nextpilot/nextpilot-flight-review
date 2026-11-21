# roadmap：把 main 的能力搬进 ng

本目录（`docs/roadmap/migration/`）是「main → ng 移植」的唯一权威计划与验收记录。代码在 `nextgen-architecture` 分支上同步推进，本目录随每次落地一并提交，所以**看板上的勾与提交号是一一对应的事实**，不是计划口径。

## 文档索引

| 文件 | 作用 | 什么时候看 |
|---|---|---|
| `01-移植清单-main到ng.md` | 33 张任务卡（M1–M8）+ 每卡实现要点 + 依赖顺序 | 想知道"要做什么、依赖谁" |
| `02-验收清单-main到ng.md` | AC-01…AC-29，每条带勾选状态与证据（测试名/截图/CLI 实跑） | 想知道"做没做到、凭什么说做到了" |
| `03-main-vs-ng-对比清单.md` | main 与 ng 的行为差异逐项对照（早期盘点，作为蓝本口径） | 想核对"main 长什么样、ng 差在哪" |

## 进度看板（截至 `f032b81`，2026-11-21）

33 张卡：**完成 11 · 部分完成 1 · 未开始 21 = 33%（含部分 36%）**。编译 0 error，`ctest -j1` **27/27 全绿**。

| 里程碑 | 卡数 | 完成 | 部分 | 未开始 | 进度 | 状态一句话 |
|---|---|---|---|---|---|---|
| **M1 实时源** | 6 | 4 | 0 | 2 | **67%** | 三前端（UDP/串口/TCP）+ 字节环后台泵 + 统计 + 全套意图已通；只剩 T1.5 连接对话框、T1.6 状态行两张 UI 卡 |
| **M2 录制** | 3 | 0 | 0 | 3 | 0% | 录制引擎（Recorder/RawWriter/Mcap）已存在，差的是"谁喂它"的接线 |
| **M3 时间对齐** | 5 | 5 | 0 | 0 | **100%** | 偏移收敛到 AppContext、5 策略、源右键入口、CSV 导出叠加、8×8 选格 |
| **M4 数据工程** | 5 | 0 | 0 | 5 | 0% | 参数表/多表快照/缓存可观测/采样上限/设置对话框 |
| **M5 比对交互** | 7 | 2 | 1 | 4 | 29% | 画布右键 17 项、时间书签改"在 t 处添加"已收；叠加/对比同名/展开数据列/拖拽重排未动 |
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
| T1.5 | 数据页「实时源」分类 + 连接对话框 | ⬜ | 未开始（纯 UI，需按 dock 现有视觉语言设计） |
| T1.6 | 面板状态行：状态/速率/字节/错误 | ⬜ | 未开始，与 T1.5 同批做 |

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

无硬件验证走 `ng/tests/test_live_udp.cpp`：`QApplication(offscreen)` + `App::startup(false)` + `app.host().execute(intent)`，UDP 用 `QUdpSocket` 真发包、串口用 `openpty()` 造 pty 对，单次 3 秒出结果。

## 下一步顺序（建议，未拍板）

1. **T1.5 + T1.6**：把 M1 从 67% 推到 100%，两张都是 UI 卡，一批设计一起落。
2. **T2.1–T2.3 录制接线**：引擎已在，接线成本低，且立即能让 M7 有东西可看。
3. **T5.1/T5.2 叠加与对比同名**：M5 现在 29%，这两张是核心动作。
4. **T4.3/T4.4 + T8.2**：独立小项，适合穿插填空。

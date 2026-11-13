# PlotScope 功能对齐矩阵（PJ3 3.9.3 / PJ4 4.1）

口径：
- PJ3 = `facontidavide/PlotJuggler` 3.9.3 核心 + 同仓库插件 + `PlotJuggler` 组织下的官方插件仓库。
- PJ4 = 当前 main 核心 + `pj-official-plugins` 官方插件。
- “对齐”不是插件数量相同，而是同一工作流可完成、同一输入结果一致、同一布局可恢复。
- 目标取 PJ3 与 PJ4 的**并集**：PJ4 丢掉的 PJ3 能力也要补，不能拿“PJ4 还没做完”当理由。

状态：✅ 已有；⚠️ 部分/需验证；❌ 没有。

## 1. 文件加载

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| CSV | ✅ | ✅ | ✅ | — |
| ULog | ✅ | ✅ | ✅ | — |
| MCAP | ✅ | ✅ | ❌ | B |
| Parquet | ✅ | ✅ | ❌ | B |
| MF4/MDF | ❌ | ✅ | ❌ | B |
| BLF / candump | ⚠️ CAN 插件 | ✅ | ✅ ASC / ✅ BLF，DBC 解码待确认 | B |
| LeRobot | ❌ | ✅ | ❌ | C |
| MP4 | ⚠️ 仅视频查看 | ✅ 作为数据源 | ❌ | C |
| 3D（PLY/PCD） | ❌ | ✅ | ❌ | C |
| ROS1 bag | ⚠️ README 声称，3.9.3 代码仅见 ROS2 | ❌ | ❌ | C |
| ArduPilot .bin | ❌ | ❌ | ❌ | 不做 |

## 2. 实时流与录制

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| MQTT | ✅ | ✅ | ❌ | C |
| ZeroMQ | ✅ | ✅ | ❌ | C |
| UDP | ✅ | ✅ | ❌ | C |
| 通用 WebSocket | ✅ | ❌（只剩 Foxglove/PJ Bridge） | ❌ | C |
| LSL | ✅ | ✅ | ❌ | C |
| ROS2 | ✅ | ✅ | ❌ | C |
| Foxglove Bridge | ❌ | ✅ | ❌ | C |
| PJ Bridge | ❌ | ✅ | ❌ | C |
| WebRTC | ❌ | ✅ | ❌ | C |
| ZCM | ✅ | ❌ | ❌ | 不做 |
| 流录制（MCAP） | ❌ | ✅ | ❌ | C |

## 3. 消息解析

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| JSON / CBOR / BSON / MessagePack | ✅ | ✅（已补） | ❌ | B |
| Protobuf | ✅ | ✅ | ❌ | B |
| ROS1/ROS2 msg | ✅ | ✅（ros1msg/ros2msg/idl） | ❌ | C |
| Arrow | ❌ | ✅ | ❌ | C |
| DataTamer | ✅ | ✅ | ❌ | C |
| Influx line | ✅ | ✅ | ❌ | C |

## 4. 绘图前数据变换

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| Transform Editor | ✅ | ✅ | ❌ | A |
| 12 个内置过滤器 | ✅ | ✅ | ❌ | A |
| 四元数 → RPY | ✅ 插件 | ✅ 插件（单 topic 三列） | ❌ | A |
| 自定义函数（Lua/Luau） | ✅ Lua | ✅ Luau | ❌ | C |
| 过滤器输出作为普通通道 | ⚠️ 派生序列 | ✅ datastore topic | ❌ | A |
| 配方进布局、可移植 | ✅ XML | ✅ 内嵌源码 | ❌ | A |
| FFT | ✅ 插件 | ✅ 插件 | ❌ | C |

## 5. 绘图与交互

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| 多页签 / 切分 | ✅ | ✅ | ✅ | — |
| 页签“+”新增 | ✅ | ✅ | ❌ | A |
| 页签复制 | ❌ | ✅ | ❌ | A |
| 单画布全屏 | ✅ | ✅ | ❌ | A |
| 曲线拖到画布 | ✅ | ✅ | ❌ | A |
| 时间游标 + 各曲线值 | ✅ | ✅ | ⚠️ 只有 t/y，不是各曲线值 | A |
| 数据树显示游标处数值 | ✅ | ✅ | ❌ | A |
| XY 图 | ✅ | ✅ | ❌ | B |
| plot 级样式/线宽 | ⚠️ 曲线级为主 | ✅ plot 级 | ⚠️ 曲线级 | B |
| GPU 画布 | ⚠️ OpenGL | ✅ QRhi | ❌ | C |
| 百万点/千序列 | ✅（宣传） | ✅（基准） | ❌ 未验证 | C |

## 6. 数据树与数据集管理

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| 搜索 | ✅ | ✅ | ✅ | — |
| 曲线/2D/3D 类型开关 | ⚠️ | ✅ | ❌ | B |
| 数据集 Info | ✅ | ✅ | ❌ | A |
| Reload / Replace | ✅ | ✅ | ❌ | A |
| Remove / Remove all | ✅ | ✅ | ⚠️ 单源删除，需验收 | A |
| Custom Topics 分区 | ❌ | ✅ | ❌ | C |
| 同名通道前缀区分 | ⚠️ | ✅ | ⚠️ 别名图例 | A |

## 7. 布局、导出、扩展

| 能力 | PJ3 | PJ4 | PlotScope | 阶段 |
|---|---|---|---|---|
| 布局保存/恢复 | ✅ | ✅ | ✅ | — |
| 保存多源 offset/顺序 | ❌ | ✅ | ❌ | A |
| 保存变换配方 | ✅ | ✅ | ❌ | A |
| CSV 数据导出 | ❌ 核心 | ✅ 插件 | ❌ 占位 | A |
| Parquet 导出 | ❌ | ✅ 插件 | ❌ | B |
| PNG 导出 | ✅ 全部页签 | ⚠️ CLI/MCP | ✅ 单画布 | — |
| 状态视图（字符串/枚举/布尔） | ❌ | ✅ | ❌ | B |
| 2D 图像/视频/深度 | ✅ 视频插件 | ✅ 核心 | ❌ | C |
| 3D / TF / 点云 | ❌ | ✅ | ❌ | C |
| 插件 SDK | ✅ C++ | ✅ C ABI | ❌ | C |
| 插件市场 | ❌ | ✅ | ❌ | C |
| MCP / AI 控制 | ❌ | ✅ | ❌ | C |
| PJ3 生态 GPS/罗盘/直方图/仪表 | ⚠️ 第三方 | ⚠️ 仅 widget 仪表 | ❌ | 按场景 |

## 8. 路线

### A. 分析闭环（当前优先，必须清零）
1. 内核派生通道 + 12 过滤器 + 四元数 4→3 + Filter Editor。
2. 多源时间偏移、对齐、前缀、同名通道对比。
3. 游标各曲线值 + 数据树游标列。
4. 绘图页签“+”、复制、全屏、曲线拖入。
5. 数据集 Info/Reload/Replace/Remove all。
6. CSV 导出、布局保存变换配方与 offset。

### B. 格式与基础视图
MCAP、Parquet、MF4、BLF/candump 的 DBC 解码、JSON/Protobuf 解析、XY 图、状态视图、plot 级样式、Parquet 导出。

### C. 平台能力
MQTT/ZMQ/UDP/LSL/ROS2/Foxglove/PJ Bridge/WebRTC、MCAP 录制、Luau/Python、插件 SDK 与市场、2D/3D、GPU/列式存储、MCP。

### D. 超越 PJ 的部分
- 飞控语义：PX4 ULog 参数/消息/飞行模式/估计器新息的结构化视图；后续 ArduPilot。
- 两次仿真/试飞对比：同名通道配对 + 时间对齐 + 参数差异 + 指标差异，一步出结论。
- 批量与自动化：无界面 CLI、批量截图/报告、回归对比。
- 本地化与离线：中文界面、离线安装、轻量部署。

## 9. 验收口径

每个能力项必须满足：
1. 同一输入文件能打开，数值与 PJ 一致（golden case）。
2. 同一工作流能在 UI 完成，不缺步骤。
3. 布局保存后重启可恢复。
4. 回归用例进 `ctest` 或脚本化 GUI 测试。

“对齐”完成的标志不是 README 打勾，而是矩阵每行有可复现的验收用例。

# 支持的数据格式

## 文件格式

| 格式 | 典型来源 | 读取方式 | 注意事项 |
|---|---|---|---|
| PX4 ULog `.ulg` | PX4 飞控日志 | 按 topic/field 展开通道，可查看参数表 | 需要日志里有对应 topic；缺失 topic 不会凭空生成通道 |
| ArduPilot DataFlash `.bin` | ArduPilot 飞控日志 | 按 `ATT.Roll` 等消息/字段展开，读取 FMTU 单位和 PARM 参数 | 单位以日志内 FMTU 为准 |
| Vector CAN `.asc` | CAN 总线录制 | 无 DBC 时输出报文字节原始值与周期 | 不猜物理量；需要物理量时先提供协议/DBC 或做协议映射 |
| Vector CAN `.blf` | CAN 总线录制 | 同上 | 同上 |
| CSV / TSV / TXT | 通用表格/导出数据 | 自动探测分隔符、表头和时间列 | 时间列识别错误时可在导入对话框里手动指定 |
| MCAP `.mcap` | 录制文件/回放 | JSON channel；支持未压缩、zstd、lz4 chunk | 只读；writer 只写最小结构 |
| MDF4 `.mf4` / `.mdf4` | 车辆/台架数据 | 读取通道组与主时间轴；支持原始块、zlib 压缩块、线性/有理/多项式转换 | 复杂转换或未支持的压缩块可能降级 |
| NMEA | GNSS 接收机 | 按语句展开通道，时间归零到首样本 | 语句类型取决于录制内容 |
| UBX | u-blox 接收机 | 按消息展开通道 | 同上 |

加载器由 schema 驱动。新增格式在扩展层注册，不需要在 app 里单独写对话框；开发方式见 [`../develop/extensions.md`](../develop/extensions.md)。

## CSV 的时间列

CSV 没有统一的时间规范。PlotScope 会按表头猜时间列，猜不中时在导入对话框中手动选：

- 行号作为时间：没有时间列时使用。
- 指定某一列作为时间：适用于 `time_ms`、`timestamp`、`t` 等列。
- 日期列 + 时间列合并：适用于分开记录日期和时刻的表格。
- 单位为秒/毫秒/微秒时，按列名或对话框选项换算。

同一份 CSV 里不同源的时间基准可能不同；多源对比前先确认时间列和时间单位。

## 实时源

| 源 | 接入方式 | 用途 |
|---|---|---|
| MAVLink | UDP，绑定地址/端口/sysid | 飞控实时数据 |
| NovAtel OEM7 | TCP 或串口 | GNSS/惯导调试 |
| 内置仿真 | 应用内启动 | 无硬件时验证绘图和脚本 |
| 原始流回放 | PSRAW1 或旧裸字节 | 回放录制流 |

实时源由独立 `LiveSession` 持有环形缓冲和 UI 刷新；录制由独立 `StreamRecorder` 写盘，不阻塞数据输入。

录制格式：

- **MCAP**：写解析后的通道，适合后续直接回放。
- **RAW**：写 PSRAW1 原始字节，保留原始协议内容。
- **CSV**：写解码后的通道，适合外部工具。

有界队列加后台写线程；磁盘慢时丢弃批次并计数，日志里能看到丢弃数量。

## 已知限制

- MAVLink tlog 原生解析尚未接入。
- Parquet、MQTT/ZMQ/ROS2/WebSocket/LSL 尚未接入。
- MCAP writer 无 chunk/summary/索引；大文件索引性能未验证。
- OEM7 宿主 RAW 录制尚未把 database schema_blob 写进文件头，回放需在配置里指定 database 路径。
- CAN 无 DBC 时只给原始字节，不做物理量猜测。

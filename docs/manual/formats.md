# 支持的数据格式

以 `plotscope --list-parser` 输出为准，按领域分组：

| 格式 | 领域 | 典型来源 | 注意事项 |
|---|---|---|---|
| CSV / TSV / TXT（`csv`） | 通用 | 表格与导出数据 | 自动探测分隔符、表头和时间列；猜不中可在导入对话框手动指定 |
| 自定义顺序协议（`custom-protocol`） | 通用 | 协议编辑器定义的顺序帧 | schema/表达式见 [psproto 规范](../../src/libs/protocol/SPEC.md) |
| PX4 ULog（`.ulg`/`.ulog`） | 飞控 | PX4 飞控日志 | 按 topic/field 展开通道，可查看参数表；缺失 topic 不会凭空生成通道 |
| ArduPilot DataFlash（`.bin`/`.log`） | 飞控 | ArduPilot 日志 | 按 `ATT.Roll` 等消息/字段展开，读取 FMTU 单位与 PARM 参数 |
| MDF4（`.mf4`/`.mdf4`/`.mdf`） | 汽车 | 车辆/台架测量数据 | 读取通道组与主时间轴；支持原始块、zlib 压缩块、线性/有理/多项式转换 |
| Vector BLF（`.blf`） | 汽车 | CAN 总线录制 | 无 DBC 时输出报文字节原始值与周期，不猜物理量 |
| ROS2 bag（`.db3`/`.db2`） | 机器人 | ROS2 rosbag2 SQLite 记录 | 按 topic 展开通道 |

NMEA / UBX（GNSS 语句与 u-blox 消息）由 gnss 工具箱包提供解析，按语句/消息展开通道，时间归零到首样本。

## CSV 的时间列

CSV 没有统一的时间规范。PlotScope 按表头猜时间列，猜不中时在导入对话框手动选：

- 行号作为时间（没有时间列时）。
- 指定某一列作为时间，适用于 `time_ms`、`timestamp`、`t` 等列。
- 日期列 + 时间列合并。
- 单位为秒/毫秒/微秒时按列名或对话框选项换算。

同一份 CSV 里不同源的时间基准可能不同；多源对比前先确认时间列和时间单位。

## 实时源

| 源 | 接入方式 | 用途 |
|---|---|---|
| MAVLink | UDP，绑定地址/端口/sysid | 飞控实时数据 |
| NovAtel OEM7 | TCP 或串口 | GNSS/惯导调试 |
| 内置仿真 | 应用内启动 | 无硬件时验证绘图和脚本 |

录制格式：MCAP（写解析后通道，可直接回放）、RAW（PSRAW1 原始字节，保留协议原文）、CSV（解码后通道，给外部工具）。录制走有界队列加后台写线程；磁盘慢时丢弃批次并计数，日志里能看到丢弃数量。

## 已知限制

- CAN 无 DBC 时只给原始字节，不做物理量猜测（DBC 支持在 M3 工具箱规划中）。
- MAVLink tlog 原生解析、Parquet、MQTT/ZMQ/WebSocket/LSL 尚未接入。
- MCAP writer 只写最小结构（无 chunk/summary/索引）；大文件索引性能未验证。
- OEM7 宿主 RAW 录制尚未把 database schema_blob 写进文件头，回放需在配置里指定 database 路径。

新增格式的开发方式见开发文档；格式驱动以工具箱包形式注册，`plotscope --list-parser` 可随时核对当前实际支持列表。

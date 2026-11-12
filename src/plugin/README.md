# src/plugin — 官方 Loader

一个家族一个目录，目录内自带 `manifest.json`、`CMakeLists.txt` 与样例文件（≤2 MB）。

- 解析器用 Rust 写（`src/kernel` 里复用 ps-ndf / ps-parse），也可以 C++，语言不限，只要按 D9 的 C ABI 吐数据。
- 插件不碰 Qt，也不 emit 信号：数据通过 `append_record` / `append_bound_record` / `append_arrow_stream` 三种粒度交给宿主。
- 计划家族：PX4 ULog、ArduPilot DFLog、Vector BLF/ASC、CAN（DBC + raw）、GNSS（NMEA/OEM7/UBX/RTCM）、SEGGER RTT/SystemView、CSV。

M0 只有目录与约定，第一个 Loader 在 M1 落地。

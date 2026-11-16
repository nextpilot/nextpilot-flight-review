# 设置层实施记录

- 状态：已实施（2026-11-16）
- 开发指南：[`../develop/settings.md`](../develop/settings.md)
- 相关代码：`src/application/shell/SettingsManager.*`、`src/application/shell/SettingsDialog.*`

## 解决的问题

旧 `SettingsManager` 只是 `QSettings` 的薄包装：

- 写入后没有进程内通知，设置页改了主题、布局恢复、扩展启停，其他界面不能统一刷新。
- key 全是裸字符串，拼写和类型错误只能运行时暴露。
- 默认值散落在调用点，无法形成一份可枚举的 key 清单。
- 外部进程修改配置后，当前进程只能手动 `sync()`。

## 关键决策

- `SettingsManager` 改为 `QObject`，增加 `changed(key, oldValue, newValue)` 和按 key 订阅的 `onChanged()`。
- 应用 key 集中到 `ps::settings::SettingsKey<T>`，业务代码使用 typed `get/set/remove`。
- 原始 `value/setValue/remove` 保留给扩展兼容和导出，但任何写路径仍然经过 `SettingsManager`。
- 兼容旧配置：bool 接受 `"true"` / `"false"` 字符串，key 名不变。
- 文件后端使用 `QFileSystemWatcher` + 250ms 防抖；Windows 注册表等非文件后端使用 3 秒轮询 + 快照比较。
- 自身写入先更新快照，避免监听回环造成重复 `changed`。
- 移除 `TransformDialog` 里直接构造 `QSettings` 的旁路，统一走设置服务。

## 验证

- `cmake --build build -j4` 通过。
- `ctest` 20/20 通过，其中 `settings_manager` 覆盖默认值、类型回退、变更信号、同值去重、remove、外部变更和导出失败。
- 离屏启动应用无 settings 告警。
- Windows 注册表轮询分支编译通过；当前 Linux 环境无法实机验证注册表变更。

## 已知边界

- 跨进程通知有延迟：文件后端约 250ms，注册表轮询约 3s。
- key 清单是编译期 `inline const` 列表，没有运行时反射或自动生成设置页。
- 设置页仍是批量 Apply/OK，连续修改多个 key 会逐 key 发信号；订阅方需要保证刷新幂等。
- 不用于密钥、令牌等敏感信息。

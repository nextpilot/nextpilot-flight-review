# 设置层：强类型 key 与变更通知

- 状态：规划中，未实施
- 目标版本：下一轮设置专题
- 相关代码：`src/application/shell/SettingsManager.*`、`src/application/shell/SettingsDialog.*`、各模块的 `*Settings.*`

## 现状

当前 `SettingsManager` 是应用设置的唯一入口，内部持有一个 `QSettings`：

```cpp
QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
void setValue(const QString &key, const QVariant &value);
void remove(const QString &key);
void sync();
```

这层封装解决了“不要到处直接建 `QSettings`”，但还没有解决两个问题。

### 1. 没有变更通知

`QSettings` 本身不是可观察对象，写入后不会发信号。当前 `SettingsManager` 也没有 `changed` 事件，因此：

- 设置页改了主题、布局恢复、扩展启停等值，已经打开的 Dock、Ribbon、画布不会自动刷新，只能在下一次读取时碰运气。
- 模块如果缓存了设置值，必须自己重新 `value()` 或手动 `sync()`，没有统一的失效入口。
- 进程内其他 `QSettings` 实例、测试代码或第二个 PlotScope 进程写入后，当前进程不会感知；`sync()` 只能手动调用。
- Windows 下 `QSettings` 默认走注册表，不能简单用 `QFileSystemWatcher` 监听；跨进程变更需要单独定义策略。

### 2. key 是裸字符串，没有类型约束

所有 key 都是 `QStringLiteral("...")` 或普通字符串：

- 拼写错误在编译期发现不了，运行时只会拿到默认值。
- `QVariant` 返回值没有类型约束，`toBool()`、`toInt()` 用错类型时可能静默得到 0 / false。
- 同一个 key 的默认值可能散落在多个模块里，容易出现“这里默认 true、那里默认 false”。
- 设置页、导出、测试各自手写 key，缺少一份可枚举的清单。

目前只有 `restoreLayout()` 做了 bool 包装，其他 key 仍是裸字符串。

## 目标

把设置层从“QSettings 的薄包装”升级为**可观察、强类型、可枚举的应用设置服务**，同时保持现有 key 名和已存配置兼容。

### P1：进程内变更通知

- `SettingsManager` 改为 `QObject`，增加信号：

```cpp
void changed(const QString &key, const QVariant &oldValue, const QVariant &newValue);
```

- 所有写入仍只经过 `SettingsManager`，在 `setValue/remove` 后发出 `changed`。
- 提供按 key 订阅的便捷接口，模块不需要自己比较旧值：

```cpp
SettingsManager::instance().onChanged(QStringLiteral("theme"), this, [](const QVariant &v) {
    // 刷新主题
});
```

- 同一 key 连续写入相同值时不发信号，避免无意义刷新。
- 写入失败或类型不匹配时记录日志，不静默吞掉。

### P2：强类型 key

- 在 shell 提供 `SettingsKey<T>`（或等价的轻量模板），key 只定义一次：

```cpp
namespace ps::settings {
inline constexpr SettingsKey<bool> kRestoreLayout{QStringLiteral("restoreLayout"), false};
inline constexpr SettingsKey<QString> kTheme{QStringLiteral("theme"), QStringLiteral("dark")};
}
```

- 读取/写入走模板接口：

```cpp
bool restore = settings.get(kRestoreLayout);
settings.set(kTheme, QStringLiteral("light"));
```

- 读取时校验 `QVariant` 能否转换为目标类型；不能转换时使用 key 的默认值并记录警告，不把错误值继续传给业务。
- 设置页、导出、测试统一从 key 清单取名字和默认值，不再手写字符串。
- 保持现有 key 名不变，已有配置不需要迁移；新增 key 必须先进清单。

### P3：跨实例/跨进程同步（可选）

- 进程内：以 `SettingsManager` 单例为唯一写入口，其他实例不再直接持有 `QSettings`。
- Linux/macOS 文件后端：用 `QFileSystemWatcher` 监听 `QSettings::fileName()`，防抖后 `sync()`，对比缓存快照，只对变化的 key 发 `changed`。
- Windows 注册表后端：不使用文件监听，改为低频轮询 `sync()` + 快照比较，或明确只支持进程内通知。
- 自身写入要避免触发监听回环：写入时设置抑制标记，或比较新旧值后再决定是否发信号。

## 验收

- 设置页修改 `theme` 后，主题、Ribbon、已打开画布立即更新，不重启。
- 修改 `restoreLayout` 后，关闭流程立即使用新值，不需要重启。
- 扩展启停写入后，重启按钮能读到最新配置（配合显式 `sync()`）。
- 拼写错误的 key 不会静默生效：使用 key 清单时编译失败，或运行时明确警告。
- 类型不匹配的值不会传给业务，使用默认值并记录。
- 连续写入相同值不触发重复刷新。
- 外部修改配置文件（文件后端）后，进程能收到 `changed`；不支持的后端有明确说明。
- `ctest` 覆盖：默认值、类型校验、变更信号、相同值去重、remove、外部变更、写入失败。

## 非目标

- 不把 `QSettings` 换成数据库或远程配置中心。
- 不做每模块独立配置文件；应用设置仍走一个命名空间。
- 不存储密钥、令牌等敏感信息；这类内容走专门的凭据存储。
- 不做设置项热插拔的 UI 自动生成；设置页仍由模块贡献，key 清单只提供名称、类型、默认值。

## 风险

- `QSettings` 的 Windows 注册表后端与文件后端行为不同，跨进程通知不能按同一套假设实现。
- 现有 key 已经写进用户配置，强类型化时不能改 key 名，否则旧配置丢失。
- 设置变更可能触发 UI 重建或画布重绘，需要防止递归写入和重复刷新。
- 模块目前可能缓存设置值，接入 `changed` 时要逐个确认旧缓存是否失效。
- 设置页的“应用/确定”是批量提交，信号可能在中间状态连续触发，需要批量写入或合并通知。

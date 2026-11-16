# 设置服务

应用设置统一走 `SettingsManager`。业务代码不直接创建 `QSettings`，key 不散写字符串。

## 入口

```cpp
#include "SettingsManager.h"

auto &st = SettingsManager::instance();
QString theme = st.get(ps::settings::kTheme);
st.set(ps::settings::kTheme, QStringLiteral("light"));
```

- 命名空间：`NextPilot / PlotScope`
- 配置文件由 `QSettings` 决定；路径统一由 Qt 管理，业务代码不要自己算。
- `SettingsManager` 是进程内唯一写入口，扩展和模块都通过它读写。

## key 清单

key 和默认值只定义在 `src/application/shell/SettingsManager.h` 的 `ps::settings` 中。

| key | 类型 | 默认值 |
|---|---|---|
| `theme` | `QString` | `dark` |
| `restoreLayout` | `bool` | `false` |
| `lastLayout` | `QString` | 空 |
| `recentFiles` | `QStringList` | 空 |
| `windowGeometry` | `QByteArray` | 空 |
| `windowState` | `QByteArray` | 空 |
| `extensions.disabled` | `QStringList` | 空 |
| `derivedMaxPoints` | `int` | `2000000` |
| `exportSampleLimit` | `int` | `0` |
| `gridDefault` | `bool` | `true` |
| `legendDefault` | `bool` | `true` |
| `timeFormat` | `QString` | `relative` |

新增设置时：

1. 在 `SettingsManager.h` 增加 `inline const SettingsKey<T>`，key 名和默认值只写一次。
2. 业务代码用 `get/set/remove`，不要写字符串。
3. 只有扩展兼容或导出场景才使用原始 `value/setValue/remove`。
4. 旧 key 不改名；需要改名时先做迁移，再删除旧 key。

## 读取、写入、删除

```cpp
bool restore = st.get(ps::settings::kRestoreLayout);
st.set(ps::settings::kRestoreLayout, true);
st.remove(ps::settings::kRestoreLayout);
```

类型校验：

- `bool`：接受 bool，也兼容旧配置里的 `"true"` / `"false"` 字符串。
- `int`：接受整数家族，转换失败回退默认值。
- `QString` / `QStringList` / `QByteArray`：精确类型，不做宽松转换。
- 其他类型：要求 `QMetaType` 精确匹配。

读取失败会 `qWarning` 并返回 key 默认值，不把错误值传给业务。

## 变更通知

```cpp
st.onChanged(ps::settings::kTheme, this, [](const QString &theme) {
    // 刷新主题
});

st.onChanged(QStringLiteral("theme"), this, [](const QVariant &oldValue, const QVariant &newValue) {
    // 需要同时看旧值和新值时用原始接口
});
```

语义：

- 写入新值后发 `changed(key, oldValue, newValue)`。
- 连续写入相同值不发信号。
- `remove` 只在 key 存在时发一次信号，`newValue` 是无效 `QVariant`。
- 回调 context 用 `this`；对象销毁后连接自动断开。
- 回调里不要再次写入同一个 key，避免递归刷新。

## 跨进程与外部修改

- 文件后端：`QFileSystemWatcher` 监听配置文件和目录，250ms 防抖后 `sync()`，与内存快照比较，只对变化的 key 发信号。
- Windows 注册表或其他非文件后端：3 秒轮询 `sync()` + 快照比较。
- 自身写入会先更新快照，监听事件不会造成重复信号。
- 需要确定性读取外部修改时，显式调用 `st.sync()`。
- 跨进程通知有延迟，不适用于实时协同或安全凭据。

## 测试

```bash
LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" ctest --test-dir build -R settings_manager --output-on-failure
```

覆盖内容：默认值、类型回退、变更信号、同值去重、remove、外部修改、导出失败。

新增或修改设置后，至少补一条 typed `get/set` 测试；涉及通知时补 `onChanged` 测试。

## 边界

- 不把 `QSettings` 换成数据库或远程配置中心。
- 不做每模块独立配置文件。
- 不存密钥、令牌等敏感信息。
- 设置页仍由模块通过 `SettingsContribution` 贡献，不由 key 清单自动生成 UI。

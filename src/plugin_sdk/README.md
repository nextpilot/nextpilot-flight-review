# ps_plugin_sdk

插件与宿主之间唯一的公共契约，纯 C，不依赖 Qt，也不 include `src/app` 或内核内部类型。

- `ps_plugin_api.h`：`PsPluginDescriptor`、`PsLoaderVtable`、ABI 版本。
- 插件入口：`extern "C" const PsPluginDescriptor *ps_plugin_entry_<id>(void)`；外部 `.so` 后续统一为 `ps_plugin_entry`。
- 数据面回调：`schema_json / probe_json / default_options / load`，全部用 JSON 字符串跨边界。
- UI 面：暂以 `descriptor->ui` 预留；后续只允许 `.ui` + JSON 协议，禁止返回 `QWidget*`。
- 插件只通过 `ps_kernel.h` 的 C ABI 写数据集，不链 `ps-datastore` 内部实现。

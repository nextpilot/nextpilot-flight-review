# ps_plugin_sdk

插件与宿主之间唯一的公共契约，纯 C，不依赖 Qt，也不 include `src/app` 或内核内部类型。

- `ps_plugin_api.h`：`PsPluginDescriptor`、`PsLoaderVtable`、ABI 版本。
- 插件入口：`extern "C" const PsPluginDescriptor *ps_plugin_entry_<id>(void)`；外部 `.so` 后续统一为 `ps_plugin_entry`。
- 数据面回调：`schema_json / probe_json / default_options / params_json / load`，全部用 JSON 字符串跨边界。
- `params_json` 由插件负责解析 P/Q、PARM 等参数表，app 不再按格式 ID 写分支。
- UI 面：`PsUiVtable` 只给 `.ui` 路径和 JSON 回调（`get_data / on_event / on_tick`），禁止返回 `QWidget*`；由 app 的 `DialogEngine` 加载。
- 外部插件：`.so` 必须导出 `ps_plugin_entry()`，包目录放 `manifest.json`、`.ui` 与资源；`ps-runtime` 负责扫描、预检、`dlopen`、ABI 校验。
- 插件只通过 `ps_kernel.h` 的 C ABI 写数据集，不链 `ps-datastore` 内部实现。

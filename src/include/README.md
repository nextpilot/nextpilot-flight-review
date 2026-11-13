# ps_plugin_api

插件与宿主之间唯一的公共契约，纯 C，不依赖 Qt，也不 include `src/app` 或内核内部类型。

- `ps_plugin_api.h`：`PsPluginDescriptor`、`PsLoaderVtable`、`PsUiVtable`、family/capabilities、ABI 版本。
- 插件入口：`extern "C" const PsPluginDescriptor *ps_plugin_entry_<family>_<id>(void)`；外部 `.so` 统一为 `ps_plugin_entry`。
- `family` 是入口第一层分类：`data_loader / data_source / parser / toolbox / panel`；`capabilities` 声明 file/probe/params/ui/cache/stream/record。
- 数据面回调：`schema_json / probe_json / default_options / params_json / load`，全部用 JSON 字符串跨边界。
- `schema_json` 同时承载插件自己的选项 UI 描述：`fields` 为表单，`ui.preview` 声明探测列/预览行/摘要，`initial_from_probe` 声明探测结果如何回填字段；app 只按这份描述渲染，不认识 `delimiter / has_header` 等格式键。
- `extensions` 由插件声明，app 不再按格式 ID 写扩展名分支。
- `params_json` 由插件负责解析 P/Q、PARM 等参数表。
- UI 面：`PsUiVtable` 只给 `.ui` 路径和 JSON 回调（`get_data / on_event / on_tick`），禁止返回 `QWidget*`；由 app 的 `DialogEngine` 加载。
- 外部插件：`.so` 必须导出 `ps_plugin_entry()`，包目录放 `manifest.json`、`.ui` 与资源；`ps-runtime` 负责扫描、预检、`dlopen`、ABI 校验、重复 id 去重。
- 插件只通过 `ps_kernel.h` 的通用 C ABI 写数据集（`ps_dataset_new / ps_dataset_add_channel`），不链 `ps-datastore` 内部实现，也不要求内核认识自己的文件格式。
- probe / parse / schema / params 由插件实现；内核只保留数据模型、filter、quat、LOD 等通用算子。

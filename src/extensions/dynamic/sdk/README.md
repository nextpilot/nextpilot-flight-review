# extensions/dynamic/sdk — 外部动态扩展 SDK

`src/extensions/dynamic` 现在只保留 SDK；官方和通用格式加载器都在 `src/extensions/builtin`。

```text
src/extensions/dynamic/
  sdk/
    cpp/    C++ 扩展 SDK（C 布局镜像，不依赖 kernel）
    rust/   Rust 扩展 SDK
```

数据加载器位置：

- 通用 loader 与 CSV/JSON/binary：`builtin/common`
- CAN/DBC：`builtin/automotive/datasources/can`
- 飞控：`builtin/autopilot/datasources`
- 汽车：`builtin/automotive/datasources`
- 机器人：`builtin/robotics/datasources`
- 卫导：`builtin/gnss`
- Rust 样板：`builtin/common/datasources/rustcsv`

## 扩展契约

- 描述符必须声明 `family` 与 `capabilities`；`DataSourceRegistry` 只收 `PS_FAMILY_DATA_LOADER / LIVE_SOURCE / DATA_SOURCE`。
- 数据面只走 `src/application/runtime/host/include/ps_extension_api.h` 的 C 契约：`schema_json / probe_json / default_options / params_json / load`。
- 扩展不得直接链接 `ps_kernel`。`load/open` 会收到 `const PsHostApi*`，只能通过 `dataset_new / dataset_add_table_typed / dataset_add_table_column_typed / dataset_append_frame / dataset_free` 写 typed 数据集；跨 Windows/Linux 同一套宿主表语义。
- 格式解析属于扩展：每个 loader 自带 `*Parser.{h,cpp}`，只通过宿主函数表写内核。
- 扩展不 include `src/application/shell`，也不持有宿主 UI。简单选项页放在 `schema_json.fields + ui.preview`，由 base 的 `DataSourceDialog` 按 schema 渲染；需要复杂自定义界面时，`.ui` 随扩展目录分发，由 `UiDialogEngine` 加载。
- base 不出现 `delimiter / has_header / suggested_time` 这类格式键；只认识 `fields / source / initial_from_probe / ui.preview` 这些通用 UI 描述。
- data_source 的 `schema.transport` 只声明 `supported` 与 `default`；`file/udp/tcp/serial|uart/internal` 的具体字段由 base 固定渲染。
- 外部 `.so` + `manifest.json` 的发现、预检、`dlopen`、ABI 校验由 `application/runtime/host/ExtensionRuntime` 负责；通过校验后经 `ExtensionRegistry::addExtension()` 注册为 `DynamicExtension`，重复 id 只保留第一个。
- 有 `PS_CAP_CACHE` 的文件加载器，解析结果由 `application/modules/datasource/services/cache/DatasetCache` 调 `ps_cache_save_dataset` 落盘；源文件 size/mtime 变化或扩展版本变化自动失效。
- `sdk/rust` 只镜像 C 布局；`builtin/common/datasources/rustcsv` 是真实 Rust I/O 样板（`.pscsv`），构建后安装到 `extensions/data_loader_rustcsv/`。

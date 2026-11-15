# extensions/dynamic — 外部动态扩展

按 family 分目录，再按扩展 id 分目录；目录名就是分类，入口名、target 名、manifest id 保持一致。

```text
src/extensions/dynamic/
  api/            ps_extension_api.h：扩展唯一契约（ABI + PsHostApi）
  sdk/            C++ / Rust 扩展 SDK（C 布局镜像，不依赖 kernel）
  data_loader/
    rustcsv/ -> ps_data_loader_rustcsv  ps_extension_entry（Rust cdylib 样板）
    csv/     -> ps_data_loader_csv     ps_extension_entry_data_loader_csv
    ulog/    -> ps_data_loader_ulog    ps_extension_entry_data_loader_ulog
    apbin/   -> ps_data_loader_apbin   ps_extension_entry_data_loader_apbin
    asc/     -> ps_data_loader_asc     ps_extension_entry_data_loader_asc
    blf/     -> ps_data_loader_blf     ps_extension_entry_data_loader_blf
    common/  -> ps_loader_common       # 通用 dataset 构建 / CAN 帧 → 通道
  data_source/   # 实时源：MAVLink / 仿真
```

- 描述符必须声明 `family` 与 `capabilities`；`DataInputRegistry` 只收 `PS_FAMILY_DATA_LOADER / DATA_SOURCE / DATA_INPUT`。
- 数据面只走 `src/extensions/dynamic/api/ps_extension_api.h` 的 C 契约：`schema_json / probe_json / default_options / params_json / load`。
- 扩展不得直接链接 `ps_kernel`。`load/open` 会收到 `const PsHostApi*`，只能通过 `dataset_new / dataset_add_channel / dataset_free` 写数据集；跨 Windows/Linux 同一套宿主表语义。
- **格式解析属于扩展**：每个 loader 自带 `*Parser.{h,cpp}`，只通过宿主函数表写内核。
- `common/` 只放跨 loader 复用的机械层（`psBuildDataset`、文件/JSON/错误 helper、CAN 帧模型），不放任何单格式解析。
- `extensions` 由扩展声明，base 不再按格式写 `if (suffix == ...)`。
- 扩展不 include `src/base/modules/shell/ui`，也不持有宿主 UI。简单选项页放在 `schema_json.fields + ui.preview`，由 base 的 `DataInputDialog` 按 schema 渲染；需要复杂自定义界面时，`.ui` 随扩展目录分发，由 `DialogEngine` 加载。
- base 不出现 `delimiter / has_header / suggested_time` 这类格式键；只认识 `fields / source / initial_from_probe / ui.preview` 这些通用 UI 描述。
- data_source 的 `schema.transport` 只声明 `supported` 与 `default`；`file/udp/tcp/serial|uart/internal` 的具体字段由 base 固定渲染。
- 外部 `.so` + `manifest.json` 的发现、预检、`dlopen`、ABI 校验由 `base/modules/runtime/ExtensionRuntime` 负责；通过校验后经 `ExtensionRegistry::addExtension()` 注册为 `DynamicExtension`，重复 id 只保留第一个。
- 有 `PS_CAP_CACHE` 的文件加载器，解析结果由 `base/modules/data_source/services/cache/DatasetCache` 调 `ps_cache_save_dataset` 落盘；源文件 size/mtime 变化或扩展版本变化自动失效。
- `sdk/rust` 只镜像 C 布局；`data_loader/rustcsv` 是真实 Rust I/O 样板（`.pscsv`），构建后安装到 `extensions/data_loader_rustcsv/`。

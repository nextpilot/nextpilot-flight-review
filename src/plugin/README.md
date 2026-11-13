# src/plugin — 官方插件

按 family 分目录，再按插件 id 分目录；目录名就是分类，入口名、target 名、manifest id 保持一致。

```text
src/plugin/
  include/  ps_plugin_api.h：插件唯一契约（ABI 4 + PsHostApi）
  sdk/rust/ ps-plugin-sdk：Rust 侧 C 布局镜像，不依赖 kernel
  data_loader/
    rustcsv/ -> ps_data_loader_rustcsv  ps_plugin_entry（Rust cdylib 样板）
    csv/     -> ps_data_loader_csv     ps_plugin_entry_data_loader_csv
    ulog/    -> ps_data_loader_ulog    ps_plugin_entry_data_loader_ulog
    apbin/   -> ps_data_loader_apbin   ps_plugin_entry_data_loader_apbin
    asc/     -> ps_data_loader_asc     ps_plugin_entry_data_loader_asc
    blf/     -> ps_data_loader_blf     ps_plugin_entry_data_loader_blf
    common/  -> ps_loader_common       # 通用 dataset 构建 / CAN 帧 → 通道
  data_source/   # 实时源：MAVLink / ROS / OEM7
  parser/        # 消息解析：DBC / MAVLink / RTCM
  toolbox/       # 算子 / 分析工具
  panel/         # 面板 / 可视化扩展
```

- 描述符必须声明 `family` 与 `capabilities`；`LoaderRegistry` 只收 `PS_FAMILY_DATA_LOADER`，其他 family 由各自 catalog 管理。
- 数据面只走 `src/plugin/include/ps_plugin_api.h` 的 C 契约：`schema_json / probe_json / default_options / params_json / load`。
- 插件不得直接链接 `ps_kernel`。`load/open` 会收到 `const PsHostApi*`，只能通过 `dataset_new / dataset_add_channel / dataset_free` 写数据集；跨 Windows/Linux 同一套宿主表语义。
- **格式解析属于插件**：每个 loader 自带 `*Parser.{h,cpp}`，只通过宿主函数表的 `dataset_new / dataset_add_channel` 写内核；`ps_kernel.h` 不再提供 `ps_csv_load / ps_ulog_load / ...` 这类格式 API，内核也不依赖 zlib。
- `common/` 只放跨 loader 复用的机械层（`psBuildDataset`、文件/JSON/错误 helper、CAN 帧模型），不放任何单格式解析。
- `extensions` 由插件声明，app 不再按格式写 `if (suffix == ...)`。
- 插件不 include `src/app`，也不持有宿主 UI。简单选项页放在 `schema_json.fields + ui.preview`，由 app 的 `DataInputDialog` 按 schema 渲染；需要复杂自定义界面时，`.ui` 随插件目录分发，由 `DialogEngine` 加载。
- app 不出现 `delimiter / has_header / suggested_time` 这类格式键；只认识 `fields / source / initial_from_probe / ui.preview` 这些通用 UI 描述。
- data_source 的 `schema.transport` 只声明 `supported` 与 `default`（`default` 可以是 `udp://127.0.0.1:1223` 这样的 URI）；`file/udp/tcp/serial|uart/internal` 的具体字段由 app 固定渲染，插件不再提供 transport 字段。解析参数由插件 `schema.parser.fields`（或 `schema.fields`）提供；`DataInputDialog` 支持多选 parse，每个 parse 展示自己的 schema 配置。
- 外部 `.so` + `manifest.json` 的发现、预检、`dlopen`、ABI 校验由 `src/app/plugin_host` 负责；重复 id 只保留第一个。
- 有 `PS_CAP_CACHE` 的文件加载器，解析结果由 `src/app/cache/DatasetCache` 调 `ps_cache_save_dataset` 落盘；源文件 size/mtime 变化或插件版本变化自动失效。
- `sdk/rust` 只镜像 C 布局；`data_loader/rustcsv` 是真实 Rust I/O 样板（`.pscsv`），构建后安装到 app 的 `plugins/data_loader_rustcsv/`，证明插件可用 Rust 解析/读写文件而不复制第二份 kernel。

# src/plugins — 官方插件

按 family 分目录，再按插件 id 分目录；目录名就是分类，入口名、target 名、manifest id 保持一致。

```text
src/plugins/
  data_loader/
    csv/     -> ps_data_loader_csv     ps_plugin_entry_data_loader_csv
    ulog/    -> ps_data_loader_ulog    ps_plugin_entry_data_loader_ulog
    apbin/   -> ps_data_loader_apbin   ps_plugin_entry_data_loader_apbin
    asc/     -> ps_data_loader_asc     ps_plugin_entry_data_loader_asc
    blf/     -> ps_data_loader_blf     ps_plugin_entry_data_loader_blf
  data_source/   # 实时源：MAVLink / ROS / OEM7
  parser/        # 消息解析：DBC / MAVLink / RTCM
  toolbox/       # 算子 / 分析工具
  panel/         # 面板 / 可视化扩展
```

- 描述符必须声明 `family` 与 `capabilities`；`LoaderRegistry` 只收 `PS_FAMILY_DATA_LOADER`，其他 family 由各自 catalog 管理。
- 数据面只走 `src/include/ps_plugin_api.h` 的 C 契约：`schema_json / probe_json / default_options / params_json / load`。
- `extensions` 由插件声明，app 不再按格式写 `if (suffix == ...)`。
- 插件不 include `src/app`，也不持有宿主 UI；格式专属 `.ui` 随插件目录分发，由 app 的 `DialogEngine` 加载。
- 外部 `.so` + `manifest.json` 的发现、预检、`dlopen`、ABI 校验由 `src/runtime` 负责；重复 id 只保留第一个。
- 有 `PS_CAP_CACHE` 的文件加载器，解析结果由 `src/datastore/DatasetCache` 落盘；源文件 size/mtime 变化或插件版本变化自动失效。

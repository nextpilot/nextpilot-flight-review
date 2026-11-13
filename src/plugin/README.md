# src/plugin — 官方 Loader 插件

一个插件一个目录，目录内自带 `manifest`（编译期内嵌）、`CMakeLists.txt` 与样例。

- 数据面只走 `src/plugin_sdk/ps_plugin_api.h` 的 C 契约：`schema_json / probe_json / default_options / params_json / load`。
- 插件不 include `src/app`，也不持有宿主 UI；格式专属的 `.ui` 随插件目录分发，由 app 的 `DialogEngine` 加载。
- 已落地插件：`csv/`、`ulog/`、`apbin/`、`asc/`、`blf/`。app 只通过 `LoaderRegistry::registerDescriptor()` 注册，不再包含任何格式专属代码。
- 后续：OEM7 作为 device 插件，额外带连接向导与状态面板；实时流插件带记录能力。
- 外部 `.so` + `manifest.json` 的发现、子进程预检、`dlopen`、ABI 校验由 `src/runtime` 负责；插件包另放 `.ui`，由 app 的 `DialogEngine` 加载。

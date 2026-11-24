# tools — 开发与发布工具

- `ps-plugin-check/`：Qt-free 子进程 `dlopen` 插件做准入检查（坏 .so 崩在子进程里）
- `ps-bench/`：吞吐 / 延迟基准（10M 点 × 20 通道 × 60 fps）
- `ps-pack/`：安装包 + 三方许可清单

- `bootstrap-deps.sh`：下载 Qt 到 `~/.ps-dps/`
- `build-local.sh`：只用 `~/.ps-dps/` 的 Qt 构建并跑 ctest
- `setup-dev-env.sh`：一键体检/安装编译环境（gcc、cmake、ninja、Qt 6.2.4、ccache）并写入 shell 变量；`--check` 只体检，`--install` 补齐
- `setup-build-env.sh`：把 ccache 装到 `~/.ps-deps/` 并写入 shell 启动文件，使 cmake 的 `find_program(ccache)` 免配置命中（`--check` / `--uninstall`）
- `nmea-metadata.py`：从 pynmea2 生成 `nmea_database.json`
- `ubx-metadata.py`：从 pyubx2 字段字典生成 `ubx_database.json`
- `mavlink-metadata.py`：从 MAVLink 头文件生成 `mavlink_metadata.json`

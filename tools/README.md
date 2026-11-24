# tools — 开发与发布工具

- `ps-plugin-check/`：Qt-free 子进程 `dlopen` 插件做准入检查（坏 .so 崩在子进程里）
- `ps-bench/`：吞吐 / 延迟基准（10M 点 × 20 通道 × 60 fps）
- `ps-pack/`：安装包 + 三方许可清单

- `ubuntu-setup.sh`：编译环境统一入口（Ubuntu/Debian）。体检并安装工具链、Qt、Rust、Python venv，把路径与缓存变量写进 `~/.bashrc`、`~/.profile`。`--check` 只体检（默认）、`--install` 补齐、`--print-env` 供 CI、`--uninstall` 撤块
- `requirements.txt`：Python 依赖版本，清单路径由 `deps-version.json` 的 `toolchains.python.configuration` 指向，装进 `~/.ps-deps/toolchain/python/venv`，不污染系统 site-packages
- `mavlink-gen.sh`：从 MAVLink 定义生成代码
- `nmea-metadata.py` / `ubx-metadata.py` / `mavlink-metadata.py`：从协议库/头文件生成 `*_database.json`
- `spikes/`：一次性实验代码，不参与构建

# 测试

## 运行全量测试

```bash
LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" \
  ctest --test-dir build --output-on-failure
```

只跑单个测试：

```bash
LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" \
  ctest --test-dir build -R layout_restore --output-on-failure
```

## 测试分层

| 类型 | 位置 | 覆盖 |
|---|---|---|
| 单元测试 | `tests/unit/` | 命令管理、会话、布局恢复、数据源、格式解析、Lua 脚本、视频同步 |
| 格式夹具 | `tests/fixture/` | CSV、ULog、BLF、ASC、OEM7、MCAP、MDF4 等 |
| Rust 内核测试 | `src/application/kernel` | `cargo test`，由 CTest 的 `kernel_cargo_test` 调用 |
| GUI 冒烟 | 手工/Xvfb | 启动、加载 CSV、Ribbon/Dock、页签、截图 |

## 布局恢复测试

`test_layout_restore` 直接构造 `MainWindow`，执行 `layout.import`，验证：

- 页签是否重建。
- 恢复页标题是否正确。
- `SessionManager::layoutPath` 是否更新。

夹具 `tests/fixture/layout_one_page.psl`：

```json
{"app":"PlotScope","version":1,"current":0,"pages":[{"type":"canvas"}],"titles":["恢复页"]}
```

## GUI 冒烟

```bash
xvfb-run -a -s '-screen 0 1600x1000x24' bash -c '
  XDG_CONFIG_HOME=/tmp/ps_smoke \
  LD_LIBRARY_PATH="$PWD/deps/Qt/6.2.4/gcc_64/lib" QT_QPA_PLATFORM=xcb \
  ./build/src/application/plotscope_app tests/fixture/demo_flight.csv
'
```

检查：

- 6 通道正常加载。
- 数据源、属性、日志 Dock 可见。
- Ribbon 页签和图标没有空白方块。
- 布局恢复时没有重复画布或重复视图。

## 新增测试的约定

- 测试目标加到 `tests/CMakeLists.txt`，按需要链接 `ps_application_shell`、`ps_application_plotting`、`ps_application_datasource`、`ps_host`。
- 涉及 Qt Widgets 的测试需要 `QApplication`，不要只建 `QCoreApplication`。
- 布局/窗口测试用临时配置目录，避免污染开发者本机 `~/.config/NextPilot/PlotScope`。
- 格式测试尽量放最小夹具，不提交大日志。

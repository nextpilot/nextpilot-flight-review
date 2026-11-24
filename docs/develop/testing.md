# 测试

首次搭建环境（工具链体检 + Qt + ccache 全部就位）：

```bash
tools/setup-dev-env.sh --check     # 只体检
tools/setup-dev-env.sh --install   # 补齐并写入 shell 变量
```

跑完新开终端即可；没跑脚本的机器仍按下文手工带 `LD_LIBRARY_PATH`。

## 运行全量测试

```bash
LD_LIBRARY_PATH="$HOME/.ps-dps/Qt/6.2.4/gcc_64/lib" \
  ctest --test-dir build --output-on-failure
```

只跑单个测试：

```bash
LD_LIBRARY_PATH="$HOME/.ps-dps/Qt/6.2.4/gcc_64/lib" \
  ctest --test-dir build -R ps_test_gui --output-on-failure
```

## 快速迭代构建

测试目标有 37 个可执行文件，改一行跨层头就要全编一遍。日常迭代关掉它们，验收前再开回来：

```bash
# 日常：只编主程序与 GUI，测试目标不生成
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPS_BUILD_TESTS=OFF
cmake --build build -j2

# 需要跑测试前，改回 ON 重新 configure（增量，不会全量重编）
cmake -S . -B build -DPS_BUILD_TESTS=ON
cmake --build build -j2 && ctest --test-dir build --output-on-failure
```

关闭测试后 `ctest` 无任何用例，报告 `0 tests` 是预期行为，不是失败。

**编译缓存**。先跑一次环境脚本，把 ccache 装到持久目录 `~/.ps-deps/` 并写进 `~/.bashrc`、`~/.profile`：

```bash
tools/setup-build-env.sh            # 安装/更新，幂等
tools/setup-build-env.sh --check    # 只看状态
tools/setup-build-env.sh --uninstall
```

之后新开终端，cmake configure 会打印 `[ccache] 已启用`。configure 时自动探测 ccache 并写入 `CMAKE_CXX_COMPILER_LAUNCHER`，探测不到则静默退化为直连编译器。

**路径不在仓库里写死**：`CMakeLists.txt` 只用裸 `find_program(ccache)`，搜索范围由 shell 的 `PATH` 决定。换机器、换 HOME、多套工具链都不要改仓库，重跑一次 `tools/setup-build-env.sh` 即可，CMake 侧零改动。

缓存容量与命中率：

```bash
ccache -s              # 命中率与占用；缓存目录 <~/.ps-deps/ccache-cache>，上限 5G 已由脚本设好
ccache -z              # 清零统计，用来测一轮改动的真实命中率
```

只有编到同内容的源文件才命中：切分支、清理重建、多个 target 重复编同一文件（如 `App.cpp` 被 11 个测试目标各编一遍）收益最大；改着头文件后的首次构建仍需真编。想强制不用缓存时加 `-DPS_USE_CCACHE=OFF`。

## 测试分层

| 类型 | 位置 | 说明 |
|---|---|---|
| 契约/单元测试 | `tests/test_*.cpp` → `ps_test_*` | 数据、绘图、脚本、协议、过滤器、注册表、意图、MCP 桥等，headless 可跑 |
| GUI 契约测试 | `tests/test_gui.cpp` → `ps_test_gui` | offscreen 下验证 ribbon 绑定、停靠、画布取数、数据呈现链路 |
| Rust 内核测试 | `src/kernel` | `cargo test`，由 CTest 的 `kernel_cargo_test` 调用 |
| 格式夹具 | `tests/fixture/` | CSV、ULog、APBin、BLF、ASC、MDF4、rosbag2、UBX、MCAP 等 |
| UI 自动化驱动 | `tests/ui_driver.cpp` → `ps_ui_driver` | 工具（非 ctest 用例），截图 + 事件注入 + 文档产图，见下节 |

## GUI 契约测试

`ps_test_gui` 在 offscreen 平台下构造完整 GUI（App + MainWindow），验证：

- ribbon 按插槽数据渲染：9 个页签、分组、动作数量与意图绑定；
- 模块在 GUI 环境下能申请到部件（面板工厂注册链路）；
- 停靠区就位（dock 面板存在、标题正确）；
- `plot.channel` 意图返回**像素级有界**数据（点数 ≤ 像素宽，与数据行数无关）；
- 数据面板能列出数据集/表/列；
- 模块静态自注册后全部激活。

## UI 自动化驱动（ps_ui_driver）

offscreen 截图 + Qt 事件注入的可复用工具，用于人工目检、场景复现与界面回归。不是 ctest 用例，带 JSON 步骤文件运行：

```bash
./build/tests/ps_ui_driver --script steps.json --out-dir /tmp/shots
```

脚本为 JSON，`steps` 顺序执行，任一步失败计入失败数，按失败数退出：

```json
{
  "steps": [
    { "op": "list-pages" },
    { "op": "load", "path": "tests/fixture/demo_flight.csv", "format": "csv" },
    { "op": "page", "index": 2 },
    { "op": "settle", "ms": 150 },
    { "op": "shot", "out": "page2.png" },
    { "op": "click", "widget": "objectName" },
    { "op": "key", "widget": "objectName", "keys": "Ctrl+S" },
    { "op": "type", "widget": "objectName", "text": "hello" },
    { "op": "action", "action": "act.objectName" },
    { "op": "dump", "out": "tree.txt" }
  ]
}
```

| op | 参数 | 说明 |
|---|---|---|
| `list-pages` | — | 打印全部 ribbon 页（索引 + 标题） |
| `page` | `index` 或 `name` | 切 ribbon 页（按索引或页名） |
| `settle` | `ms` | 等待渲染/异步完成 |
| `shot` | `out` | 整窗截图保存为 PNG |
| `load` | `path`、`format` | 走真实 `data.load` 意图加载数据文件 |
| `plot-all-channels` | `dataset`（可选） | 数据集全部数值通道上图（等价于逐列双击） |
| `intent` | `intent`、`params` | 执行任意意图；params 支持 `$lastSource`/`$lastDataset` 引用前序产出 |
| `view-action` | `action`、`params` | 走 `dispatchViewAction`（与 ribbon 点击同路径） |
| `panel` | `slot`、`panel` | 打开面板（`requestPanel`） |
| `raise-panel` | `area`、`class` | 把 dock 区某类面板置顶（类名按后缀匹配） |
| `cursor` | `t` | 游标读数 + A/B 区间 + 书签（画布真实 API） |
| `hover-toolbar` | — | 浮出活动格工具栏 |
| `demo-live` | `name`、`paused` | 注册演示 live 源（呈现 live 卡片，不真连接） |
| `click` / `dblclick` | `widget`（`x`/`y` 可选） | 在控件中心（或局部坐标）注入鼠标点击 |
| `key` | `widget`、`keys` 或 `text` | 组合键（如 `Ctrl+S`）或文本输入 |
| `type` | `widget`、`text` | 文本输入 |
| `action` | `action` | 触发 QAction（objectName / 文本 / data 三重匹配） |
| `dialog` | `name`、`shot`、… | 打开对话框配置后截图：`import` / `protocol` / `transform` / `filter` / `settings` |
| `dump` | `out` | 打印控件树（找 objectName 定位控件用） |

`widget` 用 objectName 匹配（`findChild` 递归查找），先用 `dump` 拿控件树再写步骤。其他参数：`--width`/`--height` 设窗口尺寸（默认 1440x900）。拖拽（QDrag 程序化投递）暂未纳入，见 `ps_test_gui` 对 `dropChannel` 的覆盖。

## 文档截图再生成

`docs/screenshots/` 的界面图由 `ps_ui_driver` 生成，场景脚本版本化在 `docs/screenshots/scripts/`（在仓库根运行，产出路径以 `--out-dir` 指定）：

```bash
./build/tests/ps_ui_driver --script docs/screenshots/scripts/overview.json --out-dir /tmp/shots
```

改了界面后重新生成对应 PNG 并覆盖 `docs/screenshots/` 即可；新增界面图先在 scripts/ 加场景脚本。原 `plotscope-gui-shot` 已合并进本工具并删除（issue IKX638）。

## 命令面板与快捷键

`ps_ui_driver` 可直接验证命令层：

```json
{
  "steps": [
    { "op": "key", "widget": "ps.shellFrame", "keys": "Ctrl+Shift+P" },
    { "op": "type", "widget": "commandPalette.input", "text": "palette.show" },
    { "op": "shot", "out": "command-palette.png" }
  ]
}
```

覆盖口径：

- `Ctrl+Shift+P` 打开命令面板；列表显示命令、快捷键、说明，输入按标题/id/分类模糊过滤。
- `Ctrl+P` 打开快速切换；只列编辑器、面板、Activity，不进入文件搜索。
- `Ctrl+B` 切左栏，`Ctrl+Alt+B` 切右侧辅助栏，`Ctrl+J` 切底部面板，`Ctrl+1..9` 按 Activity Bar 顺序切活动。
- `Ctrl+Tab` 循环中央页签；脚本编辑器有多个脚本页签时先循环脚本页签。`Ctrl+W` 关闭当前中央页签，不可关闭时状态栏给原因。
- 未选中曲线时“删除曲线”灰显并显示条件原因；快捷键冲突在面板中标“(冲突)”。
- 快捷键覆盖写入 `ui.keybindings`；留空恢复默认，冲突回滚并提示。

文档截图：

```bash
./build/tests/ps_ui_driver --script docs/screenshots/scripts/command-palette.json --out-dir docs/screenshots
```

## 视图停靠与 Activity Bar 拖放

`ps_ui_driver` 可验证视图跨槽移动和 Activity Bar 接收 dock：

```json
{
  "steps": [
    { "op": "load", "path": "tests/fixture/demo_flight.csv", "format": "csv" },
    { "op": "plot-all-channels" },
    { "op": "drag-view-to-activity", "title": "终端", "activity": "data" },
    { "op": "shot", "out": "activity-drop.png" }
  ]
}
```

覆盖口径：

- 数据源、脚本文件各自是独立 dock，标题只有视图名，不再出现“数据”套“数据源”。
- 视图可在 Primary Side Bar、Secondary Side Bar、Panel 之间拖动并浮动；不能停入 Editor Area。
- 把视图拖到 Activity Bar 已有图标：并入该 Activity；拖到空白：新建图标。内容落回 Primary Side Bar。
- Activity 点击在视图当前所在区或浮动窗口聚焦，不强制搬回左栏。
- 布局版本 4；旧 `ps.dock.sidebar` 快照跳过，回退默认布局。


## GUI 冒烟（手工，Xvfb）

真实显示服务器路径的冒烟检查（可选，需本机装 xvfb）：

```bash
xvfb-run -a -s '-screen 0 1600x1000x24' bash -c '
  LD_LIBRARY_PATH="$HOME/.ps-dps/Qt/6.2.4/gcc_64/lib" QT_QPA_PLATFORM=xcb \
  ./build/src/application/gui/plotscope-gui tests/fixture/demo_flight.csv
'
```

检查：通道正常加载、数据源/属性/日志 Dock 可见、ribbon 页签和图标没有空白方块。

日常界面检查优先用 `ps_ui_driver`（不依赖 xvfb，可直接截图留档）。

## 新增测试的约定

- 测试目标加到 `tests/CMakeLists.txt`，按需要链接 `ps_gui`、`ps_host`、`ps_context` 等机制库（参照 `ps_test_gui` 的链接清单）；headless 测试故意不链领域 GUI 库，守住 CLI 纯净。
- 涉及 Qt Widgets 的测试需要 `QApplication`，不要只建 `QCoreApplication`。
- 格式测试尽量放最小夹具，不提交大日志。

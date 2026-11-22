# 测试

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

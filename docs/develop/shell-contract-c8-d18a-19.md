# 裸壳契约 D18a / 19a / 19b 落账

本文把 `IKXGYH`「裸壳契约与彻底域化（B）」里三条**只差文档**的裁定补上，并给出可机械复核的证据。
写的是当前代码事实，不是计划；未达标的就记欠账，不写成"已解决"。

对应契约条目：§11 链接关（D18）、§12 产品身份（§19/§20）、§9 D19 `item.*` 键语义。

> 2026-11-25 收尾更新：D18a 的"界面层欠账"已清（`!111` `1c607371`）、19a 的关于页用途文案已归 `ProductSpec::aboutText`（`f4782619`）、
> 验收 7 加载进度已接真实摄取字节（`7c2e0b85`）、语义色库层证据已补（`!115` `65dba73e`）。下文正文保留首版记录，欠账部分按"已清"标注。

---

## D18a：协议 UI 残留核实（`ps_gui` 里的协议域界面）

**结论：链接层与界面层均已干净（界面层欠账由 `!111` `1c607371` 清偿）。**

### 已达标的部分（链接关）

`ps_gui` 的 PUBLIC 依赖里已不含任何 `ps_module_*` / `ps_toolbox_*`：

```cmake
target_link_libraries(ps_gui PUBLIC ps_host ps_context ps_gui_theme ps_gui_transform
                                    ps_command ps_protocol fastplot Qt6::...)
```

`ps_shell_bare` 能单独出产物、`--smoke` 退出码 0、符号表里 `DataModule|PlotModule|AnalysisModule` 全为 0，
说明"壳不链业务"这条成立（验收 6 三项双向检查的第三项）。

### 残留（未达标）

协议域的**两个对话框仍编在 `ps_gui` 里**，且由壳直接调用：

| 文件                                                        | 归属应为                                      | 现状                                    |
| ----------------------------------------------------------- | --------------------------------------------- | --------------------------------------- |
| `src/application/gui/shell/ProtocolInspectorDialog.{h,cpp}` | 协议模块的 GUI 侧（`ps_module_protocol_gui`） | 编进 `ps_gui`（`CMakeLists.txt:79-82`） |
| `src/application/gui/shell/ProtocolEditorDialog.{h,cpp}`    | 同上                                          | 同上                                    |

壳里的调用点：

```cpp
// src/application/gui/shell/MainWindow.cpp
#include "ProtocolInspectorDialog.h"
#include "ProtocolEditorDialog.h"
ProtocolInspectorDialog::run(m_appCtx.toolbox(), this);   // :942
ProtocolEditorDialog::run(this, &m_appCtx, m_host);       // :950
```

这就是 D18 说的"壳等于壳 + 领域界面混合物"的最后一块：**裸壳编进了协议报文查看/编辑界面**，别的产品复用时白白多带两个对话框。

**处置**：不在本轮硬搬（搬动要连带 `m_appCtx.toolbox()` 的协议面板工厂注册与命令入口，属 c9 之后的独立项）。记欠账，判据写死：

```bash
grep -rn "ProtocolInspectorDialog\|ProtocolEditorDialog" src/application/gui → 0
```

达标后这两条只剩 `ps_module_protocol_gui` 里的实现与自注册。

### 界面层欠账已清（`!111` `1c607371`）

协议两个对话框已从 `gui/shell` 迁到模块呈现层 `src/application/modules/protocol/gui/`
（`ProtocolInspectorDialog.*`、`ProtocolEditorDialog.*`，target `ps_module_protocol_gui`，OBJECT 库）。
壳不再 `new` 任何业务对话框，只经 `DialogLauncherHub` 按**动作 id** 触发；模块侧用
`PS_REGISTER_DIALOG_LAUNCHER` 自注册（`ProtocolEditorDialog.cpp:3086` 起注册 4 个 id）。

机械判据：

```bash
grep -rn "ProtocolInspectorDialog\|ProtocolEditorDialog" src/application/gui/shell → 0
```

---

## 19a：产品身份文案（必须来自 `ProductSpec`）

**结论：已达标。** 壳不得知道"这个产品叫什么"，身份文案全部由组合根注入的 `ProductSpec` 提供。

| 身份文案              | 取值来源                                     | 代码位置                                        |
| --------------------- | -------------------------------------------- | ----------------------------------------------- |
| 窗口标题              | `m_spec.displayName`                         | `MainWindow.cpp:98` 前后                        |
| 关于对话框产品名/版本 | `m_spec.displayName` / `m_spec.version`      | 关于入口                                        |
| 关于页用途说明        | `m_spec.aboutText`，空则整段不显示           | `MainWindow.cpp:601`，字段在 `ProductSpec.h:47` |
| 报告标题（导出）      | `m_spec.reportTitle`，空则回落 `displayName` | `MainWindow.cpp:368/523`                        |
| 应用名/设置路径       | `m_spec.id`                                  | 组合根 `setApplicationName`                     |
| 默认面板布局          | `m_spec.defaultPanels`                       | `MainWindow.cpp:457`                            |
| 模块清单              | `m_spec.moduleIds`                           | 装配层                                          |

机械判据（验收 19）：`grep -rn "PlotScope" src/application/gui/shell` → 0（只允许命名空间声明行）。
`ps_shell_bare` 用另一份 spec 起第二个产品，实测窗口标题/关于/报告标题随之全变，说明没有"产品常量分支"。

---

## 19b：业务文案（必须来自模块/贡献点，不得来自 `ProductSpec`）

**结论：已达标，并把边界写死在这里，防止以后有人往 `ProductSpec` 里塞业务文案。**

| 文案类别                 | 归属                                               | 例子                      |
| ------------------------ | -------------------------------------------------- | ------------------------- |
| 面板标题                 | 面板自注册的 `PanelSpec.title`（包内 `gui/` 注册） | `dataset-tree` → "数据源" |
| 命令标签 / Ribbon 分组名 | 模块 `contributes().actions` / `.ribbonGroups`     | "绘图"、"导出"            |
| 状态栏项文案             | 模块 `contributes().statuses` + `updateStatus`     | "游标: 12.40 s"           |
| Activity 标签            | 模块 `contributes().activities`                    | "脚本"                    |
| 空态/错误提示            | 模块或壳的通用诊断（不含业务名词）                 | "未装载任何数据源"        |

**规则**：`ProductSpec` 只承载**身份 + 装配**（id / displayName / version / reportTitle / aboutText / defaultPanels / moduleIds）。
`aboutText` 是"这个产品是干什么用的"身份描述，属 19a；`ps_shell_bare` 留空即不带出，实测成立。
任何"某个面板叫什么、某个按钮叫什么"的字符串都不许进 `ProductSpec`——否则第二个产品想改文案就得改代码，
而这正是 D18 去掉业务依赖后要避免的事。

一条边界用例说明理由：`defaultPanels[].title` 允许覆盖面板标题，但它只在**产品默认布局**这一处生效，
且为空时回落面板自注册标题。它是"装配偏好"，不是"业务文案来源"。

---

## 复核方式（可机械执行）

```bash
# D18a（达标应为 0）
grep -rn "ProtocolInspectorDialog\|ProtocolEditorDialog" src/application/gui
# 验收 19（达标应为 0）
grep -rn "PlotScope" src/application/gui/shell
# 验收 6 第三项：ps_gui PUBLIC 依赖不含 ps_module_* / ps_toolbox_*
grep -A3 "target_link_libraries(ps_gui PUBLIC" src/application/gui/CMakeLists.txt
```

前两条目前**一条达标、一条未达标**（`Protocol*Dialog` 未搬），本文按实测记录，不粉饰。

---

## 补充：验收 7 加载进度（`7c2e0b85`）

原先 `data.loading.progress` 只有「加载中…」→「空闲」两态，验收 7 要的是真实进度。现在：

- `IngestPipeline::progressChanged(sourceId, done, total)`（`IngestPipeline.h:116`）由 `onProgress` 钩子在解析回调里转发（`IngestPipeline.cpp:330`）；
- `DataModule` 订阅它（`DataModule.cpp:206`）：`total>0` 显示百分比，`total=0`（CSV 等流式 parser 不知总量）显示已读字节数；
- 收尾仍由 `dataLoadFinished(bool ok)` 回「空闲 / 加载失败」，进度订阅不负责收尾。

机械判据：

```bash
grep -rn "progressChanged" src/libs/context src/application/modules/data → 两处信号定义 + 一处订阅
```

# AGENTS.md · app/（L6 组合根）

> 目录归属权威见 [`../LAYERS.md`](../LAYERS.md)。

## 本目录是什么

**组合根 + 进程入口**：App（构造 AppContext 与 RuntimeHost、把自注册的模块/扩展装进宿主、驱动启动）、cli_main（CLI 入口）、mcp_bridge（MCP stdio 桥）、diagnostics。GUI 与 CLI 共用同一个 App。

### 四个子目录各管一件事

| 子目录 | 内容 | 边界 |
|---|---|---|
| `core/` | `App.h/.cpp`——组合根本体：构造世界、接线、启动顺序 | 只能装配，不得含业务 |
| `cli/` | `cli_main.cpp`——一次性批处理命令行入口 | 只做参数解析与命令分发 |
| `mcp/` | `mcp_bridge.h/.cpp`——MCP stdio 桥（JSON-RPC） | 协议层，纯函数；传输在 `cli/` |
| `diag/` | `diagnostics.h/.cpp`——诊断快照（注册树/意图统计） | 只读快照，不得改状态 |

> **命名消歧**：`app/`（L6，进程级组合根）与 `gui/shell/`（L5，窗口外壳装配）**不同名不同义**。全库不再有 `application/shell/`。

## 允许放

- 构造与装配：new 出世界 + 接线（构造 AppContext、registerModule、registerBuiltinPacks）
- 启动顺序与命令行解析（cli）
- 面向外部进程的驱动入口（mcp——把 ai.* 意图暴露为 MCP 工具，与 CLI 同构）

## 禁止放

- **任何业务逻辑**——数据怎么解析、图怎么画、意图怎么执行，一概不进 app
- 具体格式分支、具体 UI 构建（UI 归 gui，经 headless 标志自然分离）
- 新的状态持有——App 不持业务状态，运行态全在 AppContext（状态分叉 = 违规）
- 对 gui/ 的直接依赖（GUI 模式由 gui 层的入口构造 App，不是 App 反向 include gui）

## 跨子目录 include 约定

子目录之间**只用相对路径**（`../core/App.h`），不用 include 搜索路径：

```cpp
#include "../core/App.h"      // ✅
#include "App.h"              // ❌ 跨子目录裸 include
```

`core/`、`mcp/`、`diag/` 各自已通过 `target_include_directories(... PUBLIC <自己的目录>)` 暴露自己的头，**同目录内**可以裸 include。

## 动手前自查

1. App.cpp 超过 ~100 行了吗？超了先怀疑有业务逻辑混进来
2. 新需求是"多装一个东西"还是"做一件事"？前者改装配（通常 = 新增模块自带自注册，app 零改动）；后者 app 大概率不是它该待的地方
3. 想在 app 里加 helper 函数？先问：哪个下层库不能装它？
4. 新代码该进 `core/`、`cli/`、`mcp/`、`diag/` 哪一个？说不清说明它不属于 app

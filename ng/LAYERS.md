# ng 层契约

本文件是 `ng/` 各目录定位与依赖的唯一权威。新增代码先在这里判归属，再动手。与任何文档冲突时以本文件为准。

## 依赖方向（严格向下，禁止逆向）

```
L0  foundation              Result / 事件 / 常量（INTERFACE，无状态）
     ↑
L1  dataset  registry  intent  uischema  protocol  fastplot    机制能力库
     ↑
L2  core                   静态机制暂存区（B7 消亡中，内容下沉 L1）
     ↑
L3  host                   运行时编排：模块生命周期 + 意图路由 + 交互总线
     ↑
L4  modules  extensions    器官（机制提供者）· 知识包（领域业务）
     ↑
L5  gui                    GUI 外壳 + 功能视图
     ↑
L6  app                    组合根：创建并装配 AppContext + RuntimeHost + 模块
```

箭头向上表示"可以被谁依赖"。依赖只能从上往下（上层依赖下层），**禁止下层依赖上层、禁止同层横向依赖**。

`libs/` 是一个物理目录，内含 L0+L1 多个机制库；`core` 是过渡层，B7 后目录删除、内容归入 L1 与 L3。

---

## 各层定位卡

### L0 `libs/foundation`

| 项 | 内容 |
|---|---|
| 定位 | 最底层的纯契约：值类型、Result、事件、公共常量 |
| 放什么 | 无状态的值语义类型、模板、别名 |
| 禁放什么 | 任何有状态对象、Qt Widgets、业务逻辑 |
| 可依赖 | Qt::Core、系统库 |
| 禁依赖 | 除 Qt/系统外的一切（含其他 L1 库） |

### L1 `libs/{dataset,registry,intent,uischema,protocol,fastplot}`

| 项 | 内容 |
|---|---|
| 定位 | 可复用的机制能力库——纯函数/类型/常量定义，谁用谁在上层实例化 |
| 放什么 | 数据结构与算法、契约接口定义、纯 metadata、静态自注册对象（OBJECT 库） |
| 禁放什么 | 全局活实例、单例、服务定位器、跨调用存活对象、对上层的依赖 |
| 可依赖 | L0、Qt、系统库、同层内明确申明的库（见依赖矩阵） |
| 禁依赖 | L2+ 任何层 |
| 铁律 | 遵守 `ng/AGENTS.md` §5.1 libs 铁律；namespace 用所属库名，不得用 `core` |

各子库定位：

| 库 | 定位 |
|---|---|
| `dataset` | 数据集与仓库句柄（数据本体在 Rust kernel） |
| `registry` | 最小 CRUD 注册表核 + 自注册助手 |
| `intent` | 意向类型与执行契约、录制器 |
| `uischema` | 字段契约树 + SchemaEngine + 唯一 Qt 渲染器（`uischema_widgets` 是渲染子库） |
| `protocol` | psproto 文档模型、编译、编解码、脚本后端 |
| `fastplot` | 通用绘图能力库（将来独立为 Qwt-like 库） |

### L2 `core`

| 项 | 内容 |
|---|---|
| 定位 | **过渡层**：尚未迁走的静态机制暂存区，B7 后目录整体删除 |
| 放什么 | 只允许 B1–B6 尚未迁完的存量代码；**不新增任何概念** |
| 禁放什么 | 新功能、UI 关注点、register* 回调总线（→ L3 host，见 roadmap §7.9）、`QWidget` |
| 可依赖 | L0、L1、Qt::Core |
| 禁依赖 | L3+ 任何层 |
| 终态 | 内容按 §7.6 映射表分流：纯数据 → L1 `context`，路由行为 → L3 `host` |

`AppContext` 在 B7 迁出后定义在 L1 `context`：唯一实例在 L6 `app`，其余各层**构造注入**拿引用，禁止全局取用。

### L3 `host`

| 项 | 内容 |
|---|---|
| 定位 | 运行时编排层——模块生命周期、意图路由、交互扩展点 |
| 放什么 | 模块装载/激活/卸载、意图执行器、系统级命令、交互注册表（`InteractionRegistry`：viewAction/property/transform 的注册与派发） |
| 禁放什么 | 具体业务格式、具体画布/面板、任何 `QWidget*` 的跨层返回 |
| 可依赖 | L0、L1、L2、Qt::Core |
| 禁依赖 | L4+ 任何层 |
| 判定 | 协调"谁来做"属于这里；"做什么业务"属于 L4 |

### L4 `modules` / `extensions`

| 项 | 内容 |
|---|---|
| 定位 | 器官与知识包——机制提供者与领域业务 |
| `modules` 放什么 | 可装卸的能力器官：data / plot / script / protocol / analysis / settings |
| `extensions` 放什么 | 领域知识包：common / autopilot / automotive / robotics / gnss / registry |
| 禁放什么 | 彼此的实现细节、对方的私有头、具体格式分支跨包渗透 |
| 可依赖 | L0–L3、Qt（GUI 模块可依赖 Widgets）、本模块内的子目录 |
| 禁依赖 | **其他 module / 其他 extension**（同层横向，硬线）；L5 gui；L6 app |
| 隔离 | 模块/扩展之间只经 L3 host 的注册与意图协作，不直接 include |

### L5 `gui`

| 项 | 内容 |
|---|---|
| 定位 | GUI 外壳 + 功能视图——唯一的 Widgets 装配层 |
| 放什么 | MainWindow、Ribbon/Dock 容器、主题、参数表单渲染、视图工厂、各功能视图 |
| 禁放什么 | 业务格式解析、数据源生命周期、领域判断逻辑 |
| 可依赖 | L0–L4、Qt Widgets/Gui/Core |
| 禁依赖 | L6 app |
| 结构 | `shell/` 外壳装配 · `views/` 绘图视图 · 其余为共享 UI 基础设施 |

### L6 `app`

| 项 | 内容 |
|---|---|
| 定位 | 组合根——创建唯一实例、注册模块与扩展、装配启动 |
| 放什么 | `App`（持有 AppContext + RuntimeHost）、main 入口、启动顺序、命令行解析 |
| 禁放什么 | 业务逻辑、格式分支、具体 UI 构建 |
| 可依赖 | 全部下层 |
| 禁依赖 | 无（最顶层） |

---

## 支撑目录

| 目录 | 定位 | 约束 |
|---|---|---|
| `cmake/` | 构建期守卫脚本 | 只做静态可判定的边界检查；规则只加严不放宽 |
| `tests/` | 测试 target | 可依赖任意层做验证；不被任何生产 target 依赖 |
| `recording/` | 录制子系统 | 当前挂 L3 侧；实现归 `recording`，不承载领域语义 |

---

## 依赖矩阵（可依赖 = ✓，禁止 = ✗）

| from \ to | L0 | L1 | L2 core | L3 host | L4 mod/ext | L5 gui | L6 app |
|---|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| L1 libs | ✓ | 同层受限 | ✗ | ✗ | ✗ | ✗ | ✗ |
| L2 core | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ |
| L3 host | ✓ | ✓ | ✓ | ✓ | ✗ | ✗ | ✗ |
| L4 mod/ext | ✓ | ✓ | ✓ | ✓ | ✗ 横向 | ✗ | ✗ |
| L5 gui | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ |
| L6 app | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |

L1 同层依赖按库定位单向：uischema_widgets → uischema、dataset → foundation 等，不允许反向。

---

## 新增代码判定顺序

1. 是纯值语义/契约、无状态？→ L0 `foundation`
2. 是可复用机制、调用方实例化即可？→ L1 对应库
3. 是 B1–B6 尚未迁完的存量？→ L2 `core`（**不新增概念**）
4. 是协调"谁来做"/生命周期/路由？→ L3 `host`
5. 是可装卸能力或领域知识？→ L4 `modules` / `extensions`
6. 是界面装配或视图？→ L5 `gui`
7. 是创建实例、装配启动？→ L6 `app`

判不清时按"依赖更低层能完成任务就放低层"处理。禁止因为"放哪都行"塞进 `core`。

---

## 强制机制

- **构建期守卫** `cmake/LayerContractGuard.cmake` 在 configure 期扫描 target 链接，跨层倒挂直接 `FATAL_ERROR`，编译未开始即红。
- 正则不可靠的项（服务定位器取用、namespace 语义、业务逻辑渗透）靠 code review + 架构测试 `ng_test_architecture`。
- 守卫规则**只许加严，不许为绕过加豁免**；确需跨界的，先改本契约再改守卫。

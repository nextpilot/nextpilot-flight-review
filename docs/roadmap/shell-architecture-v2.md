# Shell 架构解耦方案 v2

> 状态：Phase A 已实施；Phase B 及后续由 [PJ4 架构迁移方案](pj4-architecture-migration.md) 接替（2026-11-17）
>
> 范围：Phase A 先整理 `src/application/shell` 内部依赖；Phase B 起拆出受控 `Core`，`MainWindow` 只做 Shell。不改业务模块、不迁移扩展、不改命令 ID 和现有 UI 行为。
>
> 目标：`MainWindow` 不再同时扮演“窗口壳 + 服务中心”；进程级服务进入 `Core`，窗口级 UI 留在 Shell，文档级状态进入 `Workspace`。Manager 之间不反向依赖具体类型。
>
> 本文件保留 Phase A 的断环记录和 v2 讨论，不再作为后续实施目标。原 Phase B 的 `Core / Workspace / ProjectContext` 不采用；后续按 `ps_runtime + ModuleContext + ServiceRegistry` 落地，见新方案。
>
> 本文件是实施方案，不直接改变仓库硬约束。实施完成后再同步更新 `AGENTS.md` 和 `docs/develop/architecture.md`。

---

## 1. 现状问题

当前所有权树是清楚的：`MainWindow` 持有全部 Manager。问题不在 ownership，而在 call graph：

| 问题 | 现状 | 影响 |
|---|---|---|
| `PropertyManager` 反向取 Dock | `PropertyManager.cpp` 通过 `m_mw->docks().byId("dock.prop"/"dock.tree")` 控制面板可见性 | `PropertyManager → MainWindow → DockManager` 形成环，属性面板无法脱离主窗口单测 |
| `SettingsManager` 依赖窗口 | `openDialog(MainWindow*)`、`registerCommand(..., MainWindow*)` | `SettingsManager → MainWindow` 形成环，设置存储和设置 UI 混在一起 |
| `SessionManager` 依赖命令 | `SessionManager::bindCommandState(CommandManager&, ...)` | `SessionManager → CommandManager`，命令 handler 又可能回指 Session，形成运行期环 |
| `LogManager` 依赖对象名 | `MainWindow` 用 `findChild<QPlainTextEdit*>("dock.log.view")` 取日志控件 | 字符串魔法耦合，Dock 改名或换实现就断 |
| 命令 Handler 捕获整个窗口 | `ShellCommands.cpp` 大量 `[&win]` | 命令层表面无依赖，实际通过闭包回指任意 Manager |
| 扩展窗口钩子拿 `MainWindow*` | `windowHooks` 直接传入 `MainWindow` | 模块可以绕过 Registry 直接操作任意 Manager |

结论：

```text
ownership 基本是 MainWindow 单根，没有 Manager 之间的所有权环；
但 call graph 是 hub-and-spoke，多个 Manager 通过 MainWindow 反向取其他 Manager。
```

真正的问题不是“有中心”，而是 `MainWindow` 同时承担了窗口壳、UI 容器和服务门面三种角色。  
Qt 大型项目通常会保留一个受控中心，但把它放在 `Core`，而不是 `MainWindow`；`MainWindow` 只负责 Shell。

---

## 2. 目标架构

### 2.1 长期分层

```text
Core / Application（进程级组合根）
├─ AppServices
│   ├─ SettingsService
│   ├─ LogService
│   └─ ThemeService
├─ PluginManager
│   ├─ ExtensionHost / Registry
│   └─ 依赖解析与生命周期
└─ DocumentManager（后续多工程时启用）

MainWindow / Shell（窗口级组合根）
├─ CommandManager（窗口上下文；多窗口时再上移 ActionManager）
├─ DockHost
├─ RibbonHost
├─ PageHost
├─ PropertyHost
└─ SessionStore

Workspace / ProjectContext（文档级组合根，后续多工程时启用）
├─ 数据源
├─ 分析模型
├─ 绘图模型
└─ 选择/撤销状态
```

本次实施顺序：**先做 Phase A（Shell 内部断环），再做 Phase B（Core / Shell 分层）**。  
`DocumentManager` / `Workspace` 属于 Phase D，不在第一批强行引入。

### 2.2 Phase A 目标图（本次改动范围）

```text
MainWindow（Phase A：仍持有全部 Manager，但只做装配）
├─ CommandManager        无 Manager 依赖
├─ ExtensionManager      ExtensionHost + SettingsManager
├─ SessionManager        SettingsManager + DockLayoutPort
├─ LogManager            Qt 控件 + Sink
├─ DockManager           Registry + CommandManager + ThemeManager
├─ PropertyManager       DockShowPort + ThemeManager
└─ RibbonManager         CommandManager + Registry + ThemeManager
```

关键变化：

```text
PropertyManager  →  DockShowPort        （不再经 MainWindow 找 Dock）
SettingsManager  →  QSettings           （不再拿 MainWindow 开对话框）
SessionManager   →  Settings + LayoutPort（不再 include CommandManager）
LogManager       →  保持独立             （不再靠 findChild 魔法字符串）
MainWindow       →  所有 Manager         （Phase A 只负责装配）
```

Phase B 完成后，`SettingsManager / LogManager / ThemeManager / ExtensionManager` 从 `MainWindow` 移到 `Core`；`MainWindow` 只保留窗口级 Shell。

### 2.3 受控 Core 与 MainWindow 的边界

允许有 `Core` / `AppServices` 服务门面：

```cpp
struct AppServices
{
    SettingsService &settings;
    LogService &log;
    ThemeService &theme;
    PluginManager &plugins;
};
```

`Core` 是**受控服务门面**，不是第二个 MainWindow。它只放：

- 进程级、跨模块、生命周期稳定的服务；
- 插件管理、文档管理这类应用级能力；
- 只暴露稳定接口，不暴露具体窗口控件。

不允许把 `DockManager`、`RibbonManager`、`PropertyManager`、`SessionManager` 这些窗口 UI 对象塞进 `Core`：

```cpp
// 不推荐：把窗口 UI 对象也塞进 Core，Core 变成第二个 MainWindow
struct Core {
    DockManager* docks;
    RibbonManager* ribbon;
    PropertyManager* props;
    SessionManager* session;
    ...
};
```

也不允许业务模块通过 `Core` 反向拿窗口控件。  
`Core` 只给模块 `ModuleContext` / `HostContext`，模块不应该看到 `MainWindow` 或 `DockManager`。

### 2.4 参考模式：Qt Creator

Qt Creator 是 Qt 官方大型项目的代表，采用：

```text
Core Plugin
├─ ActionManager
├─ EditorManager / DocumentManager
├─ ModeManager
└─ 基础服务

PluginManager
├─ 读取插件 manifest
├─ 解析插件依赖
├─ 按依赖顺序加载
└─ Object Pool 注册/检索对象
```

参考：

- [Core Namespace](https://doc.qt.io/qtcreator-extending/core.html)
- [The Plugin Manager, the Object Pool, and Registered Objects](https://doc.qt.io/qtcreator-extending/pluginmanager.html)
- [Plugin Life Cycle](https://doc.qt.io/qtcreator-extending/plugin-lifecycle.html)

可借鉴的不是“照抄 ICore”，而是三条：

1. 进程级服务集中到 Core，窗口只做 Shell；
2. 插件依赖显式声明，PluginManager 决定加载顺序；
3. 扩展通过 Object Pool / 扩展点注册，不直接拿另一个插件的内部对象。

Qt Creator 的 `ICore` 本身就是一个受控 Service Locator。它成立的前提是：Core 只暴露稳定接口、不承载具体业务、插件依赖可解析。  
这正是本方案要立住的边界：**可以有中心，但中心不能是 `MainWindow`。**

---

## 3. 硬规则

实施完成后，`src/application/shell` 遵守以下规则：

1. `*Manager.h` / `*Manager.cpp` 不允许 `#include "MainWindow.h"`。
2. Manager 不允许 include 兄弟 Manager 的具体头；以下基础服务除外：
   - `CommandManager`
   - `ThemeManager`
   - `ps::components::Registry`
3. 跨 Manager 的查询/动作，通过构造函数注入回调或端口。
4. `MainWindow` 只负责创建窗口级 Shell、接线、生命周期和启动顺序，不承载业务转发；进程级服务由 `Core` 提供。
5. 允许 `Core` / `AppServices` 作为受控服务门面，但只放进程级服务、插件管理和文档管理；禁止把 Dock/Ribbon/Property/Session 等窗口 UI 对象放进 Core。
6. `ShellCommands`、`ShellComponent`、`SettingsCommands` 属于组合层，允许认识 `MainWindow`；Manager 不允许。
7. 模块只通过 `HostContext` / `Registry` 接入，不直接持有 `MainWindow*`，也不通过 `Core` 反向拿窗口控件。
8. `QObject` parent 只表示生命周期，不表示依赖；禁止在 Manager 里 `qobject_cast<MainWindow*>(parent())`。
9. 跨层长期持有的 Widget 指针使用 `QPointer`，不裸存可能被 Qt 销毁的控件。
10. 端口只用在跨边界、跨生命周期或需要替换实现的地方；Shell 内部基础服务不要为端口而端口化。

---

## 4. 接口设计

### 4.1 Dock 显示端口

新增 `src/application/shell/ports/DockPorts.h`：

```cpp
#pragma once

#include <QString>
#include <functional>

namespace ps::shell
{

// 显示 Dock；activate=true 时同时设为当前标签页。
using DockShow = std::function<void(const QString& id, bool activate)>;

// 保存/恢复 Dock 布局；SessionManager 只认这两个函数，不认 DockManager。
using DockSave = std::function<QByteArray()>;
using DockRestore = std::function<bool(const QByteArray&)>;

} // namespace ps::shell
```

`DockManager` 增加：

```cpp
void showDock(const QString& id, bool activate = false);
QWidget* dockWidget(const QString& id) const;
```

`PropertyManager` 改为：

```cpp
explicit PropertyManager(QObject* parent, ps::shell::DockShow showDock);
```

`MainWindow` 接线：

```cpp
m_props(this, [this](const QString& id, bool activate) {
    m_docks.showDock(id, activate);
})
```

`PropertyManager::ensureVisible()` 改为：

```cpp
m_showDock(QStringLiteral("dock.prop"), true);
m_showDock(QStringLiteral("dock.tree"), false);
```

行为保持：属性面板显示并设为当前页，对象树只显示不抢当前页。

### 4.2 Settings 去窗口化

`SettingsManager` 只保留设置存储：

```cpp
class SettingsManager : public QObject
{
public:
    static SettingsManager& instance();
    // 删除 openDialog / registerCommand
};
```

`SettingsDialog` 改为：

```cpp
SettingsDialog(QWidget* parent,
               ExtensionManager& extensions,
               std::function<void()> applyTheme);
```

内部改动：

```cpp
// 旧：m_mw->extensions().registry().settings()
// 新：m_extensions.registry().settings()

// 旧：m_mw->applyTheme()
// 新：m_applyTheme()
```

`SettingsCommands.cpp` 改成组合层函数：

```cpp
namespace shell
{

void registerSettingsCommand(ps::commands::CommandManager& commands,
                             QWidget* parent,
                             ExtensionManager& extensions,
                             std::function<void()> applyTheme)
{
    commands.registerCommand(
        ps::commands::CommandSpec{QStringLiteral("file.settings"), QStringLiteral("设置"),
                                  QStringLiteral("settings"), QStringLiteral("Ctrl+,"),
                                  false, {}, {}, false},
        [parent, &extensions, applyTheme](const ps::commands::CommandContext&) {
            SettingsDialog dialog(parent, extensions, applyTheme);
            dialog.exec();
        });
}

} // namespace shell
```

`MainWindow` 调用：

```cpp
shell::registerSettingsCommand(m_commands, this, m_extensions,
                               [this] { applyTheme(); });
```

### 4.3 Session 去 CommandManager

从 `SessionManager` 删除：

```cpp
void bindCommandState(ps::commands::CommandManager&, std::function<void()>);
```

新增组合层 helper，放在 `src/application/shell/SessionCommands.h/.cpp`：

```cpp
namespace shell
{

void bindSessionCommandState(SessionManager& session,
                             ps::commands::CommandManager& commands,
                             std::function<void()> refreshTitle);

} // namespace shell
```

实现保持原逻辑：

```cpp
commands.setInvokeHook([&session, &commands, refreshTitle = std::move(refreshTitle)](
                           const QString& id) {
    if (id == QLatin1String("layout.save") || id == QLatin1String("layout.import")) {
        session.markClean();
        if (refreshTitle)
            refreshTitle();
        return;
    }
    const auto* spec = commands.spec(id);
    if (spec && spec->sessionDirty) {
        session.markDirty();
        if (refreshTitle)
            refreshTitle();
    }
});
```

`MainWindow` 调用：

```cpp
shell::bindSessionCommandState(m_session, m_commands, [this] { refreshTitle(); });
```

### 4.4 Log 视图去 findChild

`MainWindow` 改为：

```cpp
m_logs.setLogView(qobject_cast<QPlainTextEdit*>(
    m_docks.dockWidget(QStringLiteral("dock.log"))));
```

`LogManager` 不改接口，继续保持“只认识控件和 Sink”。

### 4.5 命令 Handler 收窄（第二批）

`ShellCommands.cpp` 从“一个函数捕获整个 MainWindow”拆成按命令域注册：

```cpp
void registerFileCommands(CommandManager&, LogManager&, QTabWidget* pages,
                          ExtensionManager&);
void registerLayoutCommands(CommandManager&, SessionManager&, DockManager&);
void registerHelpCommands(CommandManager&, ExtensionManager&);
```

每个函数只捕获自己需要的服务。`ShellCommands` 仍是组合层，允许知道 `MainWindow` 来取这些服务，但 lambda 不再 `[&win]` 一把抓。

---

## 5. 实施阶段

### Phase A：断开最明显的环（本次必做）

#### A1. PropertyManager 去 MainWindow

- [ ] 新增 `src/application/shell/ports/DockPorts.h`。
- [ ] `DockManager` 增加 `showDock(id, activate)`、`dockWidget(id)`。
- [ ] `PropertyManager` 删除 `class MainWindow;` 和 `MainWindow* m_mw`。
- [ ] `PropertyManager` 构造函数改为 `(QObject* parent, DockShow showDock)`。
- [ ] `ensureVisible()` 改调 `m_showDock`。
- [ ] `MainWindow` 构造 `m_props` 时注入 lambda。
- [ ] 确认成员声明顺序：`m_docks` 在 `m_props` 之前，lambda 捕获安全。

验收：

- 属性节点注册后，`dock.prop` 自动显示并成为当前页。
- `dock.tree` 自动显示，但不抢当前页。
- 无属性节点时，两个 Dock 仍在窗口显示前收起。
- `PropertyManager.cpp` 不再出现 `MainWindow` 或 `docks()`。

#### A2. SettingsManager 去窗口

- [ ] `SettingsManager.h` 删除 `class MainWindow;`、`openDialog`、`registerCommand`。
- [ ] `SettingsManager.cpp` / `SettingsCommands.cpp` 拆分：`SettingsManager` 只保留 QSettings。
- [ ] `SettingsDialog` 改为 `(QWidget*, ExtensionManager&, std::function<void()>)`。
- [ ] `SettingsDialog` 用 `extensions.registry().settings()` 取扩展设置页。
- [ ] `SettingsDialog` 用 `applyTheme` 回调替换 `m_mw->applyTheme()`。
- [ ] `MainWindow` 调用 `shell::registerSettingsCommand(...)`。

验收：

- `Ctrl+,` 打开设置对话框，父窗口正确。
- 通用页主题切换立即生效。
- 扩展设置页仍能创建、应用。
- `SettingsManager.h/.cpp` 不再出现 `MainWindow`。

#### A3. Log 视图去对象名

- [ ] `MainWindow` 用 `m_docks.dockWidget("dock.log")` 取日志视图。
- [ ] 删除 `findChild<QPlainTextEdit*>("dock.log.view")`。
- [ ] `dockWidget()` 对不存在/非 QWidget 内容返回 `nullptr`。
- [ ] `setLogView(nullptr)` 安全，不崩溃。

验收：

- 日志 Dock 正常显示日志。
- 关闭日志 Dock 再打开，日志仍追加。
- `dock.log` 未注册时应用不崩溃。

#### A4. Session 去 CommandManager

- [ ] 从 `SessionManager.h/.cpp` 删除 `bindCommandState`。
- [ ] 新增 `SessionCommands.h/.cpp` 的 `bindSessionCommandState`。
- [ ] `MainWindow` 改用 `shell::bindSessionCommandState`。
- [ ] 更新 `tests/unit/test_session_manager.cpp`：要么直接测 helper，要么保留 Session 纯行为测试，另加 command binding 测试。
- [ ] `tests/CMakeLists.txt` 加入 `SessionCommands.cpp`（如测试直接链接 helper）。

验收：

- `sessionDirty` 命令执行后标题出现未保存标记。
- `layout.save` / `layout.import` 后标记清除。
- `SessionManager` 不再 include `CommandManager.h`。

#### A5. 回归

- [ ] `cmake --build build -j4` 通过。
- [ ] `ctest --test-dir build --output-on-failure` 通过。
- [ ] `xvfb-run` GUI 冒烟：启动、打开文件、属性面板、日志、设置、保存布局、恢复布局。
- [ ] 运行 `rg` 边界检查（见 §7）。

### Phase B：Core / Shell 分层（主线，A 完成后立即做）

目标：把进程级服务从 `MainWindow` 移到 `Core`，`MainWindow` 只保留窗口级 Shell。

- [ ] 新建 `src/application/core/Core.h/.cpp` 或等价 `Application` 组合根。
- [ ] `Core` 持有 `SettingsService`、`LogService`、`ThemeService`、`PluginManager`。
- [ ] `ExtensionManager` 改为 `PluginManager` 的窗口无关实现，`MainWindow` 只消费 `Registry`。
- [ ] `MainWindow` 构造函数改为 `MainWindow(Core&, QWidget*)`。
- [ ] `MainWindow` 中删除 `SettingsManager`、`LogManager`、`ThemeManager`、`ExtensionManager` 成员。
- [ ] `ShellCommands` / `SettingsCommands` 从 `Core` 取设置、日志、主题、扩展。
- [ ] 启动顺序改为 `Core` 先建，`PluginManager` 扫描/激活，再建 `MainWindow`。

验收：

- 删除任意一个业务扩展，`MainWindow` 仍能编译、启动。
- `MainWindow` 构造函数不再需要 `SettingsManager::instance()`。
- `rg 'SettingsManager|ExtensionManager|ThemeManager' src/application/shell/MainWindow.*` 为空。
- 命令行文件打开、设置、主题、日志、扩展设置页行为不变。

### Phase C：命令与扩展边界（第二批）

- [ ] `ShellCommands.cpp` 按文件/布局/帮助拆注册函数，去掉 `[&win]` 全捕获。
- [ ] `windowHooks` 从 `MainWindow*` 改为窄上下文，至少不暴露 `docks()` / `properties()`。
- [ ] 为 `HostContext` 增加只读能力接口，模块不能通过 `window()` 反向 cast 成 `MainWindow`。
- [ ] 命令 Handler 新增代码禁止捕获整个 `MainWindow`。
- [ ] 插件 manifest 增加 `requires` / `provides`，`PluginManager` 按依赖拓扑加载。

### Phase D：Document / Workspace（多工程时再做）

- [ ] 引入 `DocumentManager`，管理当前工程/文档。
- [ ] 数据源、分析模型、绘图模型、选择/撤销状态进入 `ProjectContext`。
- [ ] `MainWindow` 只负责切页、布局和窗口状态，不持有业务状态。
- [ ] 多窗口/多工程需求出现前不提前抽象；单工程阶段可以先用 `Workspace` 空壳。

---

## 6. 行为保持清单

本次重构不允许改变以下行为：

| 行为 | 必须保持 |
|---|---|
| 属性面板自动显示 | 有节点时显示 `dock.prop`，并设为当前页 |
| 对象树自动显示 | 有节点时显示 `dock.tree`，但不抢当前页 |
| 无属性节点 | 启动时收起两个 Dock |
| 设置对话框 | `Ctrl+,` 可打开，主题切换立即生效 |
| 扩展设置页 | 仍按 Registry 顺序展示，应用逻辑不变 |
| 日志视图 | 日志 Dock 正常追加，状态栏同步 |
| 会话状态 | `sessionDirty` 命令标脏，布局保存/导入清脏 |
| 布局恢复 | 仍走 DockManager save/restore |
| 命令 ID | 不改 `file.*` / `layout.*` / `view.*` / `help.*` |

---

## 7. 验收命令

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

边界检查：

```bash
# Manager 不得 include MainWindow
rg -n '#include "MainWindow.h"' src/application/shell/*Manager.*

# PropertyManager 不得再出现 MainWindow / docks()
rg -n 'MainWindow|m_mw|docks\(\)' src/application/shell/PropertyManager.*

# SettingsManager 不得再出现 MainWindow / openDialog / registerCommand
rg -n 'MainWindow|openDialog|registerCommand' src/application/shell/SettingsManager.*

# SessionManager 不得再 include CommandManager
rg -n 'CommandManager' src/application/shell/SessionManager.*

# 不再用对象名找日志视图
rg -n 'dock\.log\.view|findChild<QPlainTextEdit' src/application/shell
```

GUI 冒烟：

```bash
xvfb-run -a ./build/plotscope_app
```

---

## 8. 风险与回滚

| 风险 | 处理 |
|---|---|
| `PropertyManager` 注入 lambda 捕获 `this`，构造顺序错误 | `m_docks` 必须在 `m_props` 之前声明；当前顺序已满足，补静态断言/注释 |
| `dock.prop` 显示后没有成为当前页 | `showDock(id, activate)` 保留 `setAsCurrentTab()`，A1 验收覆盖 |
| `SettingsDialog` 父窗口变化导致模态行为变化 | 仍以 `MainWindow` 作为 `QWidget*` parent 传入；只改类型，不改实例 |
| 扩展设置页的 parent 从 `MainWindow*` 变成 `QWidget*` | `SettingsContribution::create` 只要求 `QWidget*`，行为不变；补 GUI 冒烟 |
| `SessionManager` 测试依赖 `bindCommandState` | 测试改为测 `SessionCommands` helper；Session 自身测试保持纯状态 |
| `dock.log` 不存在时取到空指针 | `dockWidget()` + `qobject_cast` + null check；`setLogView(nullptr)` 安全 |
| 旧代码仍有 Manager 互相 include | 用 §7 的 `rg` 检查作为 CI 门禁；新代码评审按 §3 规则 |

回滚策略：

- A1~A4 每步独立可回滚；不一次性改 `MainWindow` 构造顺序。
- 如设置对话框行为异常，先回滚 A2，不影响 A1/A3/A4。
- 如属性面板行为异常，先回滚 A1，不影响其他步骤。

---

## 9. 不做什么

- 不把 `DockManager`、`RibbonManager`、`PropertyManager`、`SessionManager` 等窗口 UI 对象塞进 `Core`，避免 Core 变成第二个 MainWindow。
- 不把 `Core` 作为业务模块的万能 Service Locator；模块只拿 `ModuleContext` / `HostContext` 的能力接口。
- 不把 `CommandManager` 改成信号总线。
- 不迁移 `modules/datasource`、`modules/plotting` 或扩展代码。
- 不修改命令 ID、Ribbon 结构、Dock 布局和用户可见文案。
- Phase A 不在本次引入多窗口/多工程抽象；`Workspace` 只作为 Phase D 方向记录。

---

## 10. 完成后的文档动作

- [ ] 更新 `AGENTS.md` §4：增加 Manager include 边界、Core 与 Shell 分工、组合层例外。
- [ ] 更新 `docs/develop/architecture.md`：补充“Core 提供进程级服务，MainWindow 只做 Shell”的装配说明。
- [ ] 将本文件状态改为“已实施”，记录实际日期和偏差。
- [ ] 若 Phase D 暂不实施，在 README 的当前优先级里保留为后续项。

---

## 11. 一句话验收标准

```text
Phase A：MainWindow 仍持有窗口级 Manager，但没有任何 Manager 通过 MainWindow 去取另一个 Manager。
Phase B：进程级服务进入 Core，MainWindow 只保留 Dock/Ribbon/Page/Property/Session 等窗口级 Shell。
```

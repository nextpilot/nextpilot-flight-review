# 组件与扩展模型

> 当前实现指南。硬边界见 [`../../AGENTS.md`](../../AGENTS.md)；旧组件模型与决策记录在 [`../roadmap/extension-model-v1.md`](../roadmap/extension-model-v1.md) 和 [`../roadmap/extension-points.md`](../roadmap/extension-points.md)。

## 两种加载方式

| 类型 | 入口 | 运行方式 | UI 能力 |
|---|---|---|---|
| 内置扩展 | `ps::components::IComponent` | 随 app 编译，静态注册到 `ExtensionRegistry` | 可创建 Qt 控件，但只拿 `HostContext` |
| 动态扩展 | `PsExtensionDescriptor` + 公开 ABI | 运行时扫描动态库，`ExtensionRuntime` 校验后登记 | 不返回 `QWidget*`；声明 `.ui`/JSON/白名单控件，由宿主创建 |

两者投递贡献的方式相同：都往 `ps::components::Registry` 里注册，宿主统一消费。

## 内置扩展

```cpp
class GnssComponent : public ps::components::IComponent
{
public:
    ComponentManifest manifest() const override
    {
        return {QStringLiteral("gnss.core"), QStringLiteral("GNSS"),
                QStringLiteral("1.0"), {QStringLiteral("data_source"),
                                        QStringLiteral("dock")}};
    }

    void contribute(Registry &registry, HostContext &host) override
    {
        registry.addDataSource(/* ... */);
        registry.addDock(DockContribution{
            QStringLiteral("dock.gnss.sky"),
            QStringLiteral("卫星星空图"),
            Qt::RightDockWidgetArea, true, 10,
            QStringLiteral(":/icons/sat.svg"),
            [host](QWidget *parent) {
                auto *view = new SkyPlotWidget(parent);
                // host.log / host.window / host.configDir
                return view;
            }});
    }
};
```

约束：

- 不 include `shell/MainWindow.h`，不把 `MainWindow*` 传进组件。
- 不直接改 `MainWindow` 的 Ribbon/Dock；用 `Registry` 贡献。
- 只依赖 modules 的公开头；跨业务访问走 `ModuleBridge`。
- 一个扩展目录一个 target；`ps_builtin_components` 聚合，删掉后 app 仍可构建。

## 领域工具箱

内置扩展按业务领域拆成四个工具箱，各自注册本领域的数据源和协议 parser：

| 工具箱 | 组件 id | 注册内容 |
|---|---|---|
| `autopilot` | `autopilot.core` | `data_loader_ulog`、`data_loader_apbin`、`data_source_mavlink`、`data_source_sim`、`autopilot.mavlink` |
| `gnss` | `gnss.core` | `data_loader_nmea`、`data_loader_ubx`、`data_source_oem7`、`gnss.nmea`、`gnss.ubx`、`gnss.oem7` |
| `automotive` | `automotive.core` | `data_loader_asc`、`data_loader_blf`、`data_loader_mdf4`，CAN/DBC 共享 `ps_can_common` |
| `robotics` | `robotics.core` | `data_loader_rosbag2`、`data_load_mcap` |
| `common` | `data.builtin` | 通用 loader、`data_loader_csv`、`data_loader_json`、`data_loader_binary` |

`builtin/common` 只保留 CSV、JSON、binary 这类通用格式的实现与注册，不再登记领域格式。新增领域格式时，放到对应工具箱组件里注册；不要把 `if (suffix == ...)` 写回通用数据源。

工具箱之间不互相 include 私有头；需要共享加载器、数据集构建等机械能力时走 `builtin/common`。工具箱只注册 parser，不自己维护第二份 `MessageParserRegistry`。

## 动态扩展

动态库只导出 `PsExtensionDescriptor`，不导出 C++ 类或 Qt 对象：

```cpp
extern "C" PS_EXPORT const PsExtensionDescriptor *ps_extension_entry();
```

约束：

- manifest 必须过 schema；`abiVersion`、`ps_abi_selftest()` 必须匹配。
- 数据面走 `PsLoaderVtable` / `PsDataSourceVtable`，UI 面走 `PsUiVtable` 的 `.ui` 路径 + JSON 回调。
- 不跨 C ABI 返回 `QWidget*`；不 include shell/modules 私有头。
- 不加载任意 `.so` 路径；不执行 manifest 中的脚本；不信任未校验的 widget/action 名称。
- 动态扩展崩溃不能带崩宿主；宿主负责 UI 创建和销毁。

`UiDialogEngine` 负责加载扩展 `.ui`，但只接受宿主传入的描述，不持有扩展对象。

## Registry 贡献点

当前 `Registry` 暴露：

| 方法 | 用途 |
|---|---|
| `addDataSource` | 文件/串口/TCP/UDP 数据源或加载器 |
| `addProtocol` | 协议 kind、schema、校验、编辑器 |
| `addRibbon` | Ribbon 页、面板、条目 |
| `addDock` | Dock 面板 |
| `addDialog` | 通用业务对话框 |
| `addSettings` | 设置页 |
| `addAction` | 组件自有 action |
| `addScriptRunner` | 脚本执行器 |
| `addStatusChip` | Ribbon 右侧只读状态芯片 |
| `addWindowHook` | 宿主窗口建立后的模块初始化 |

贡献项由创建者拥有；宿主只遍历和创建容器，不认识具体业务类型。

## HostContext

`HostContext` 是宿主给组件的窄接口，当前提供：

```cpp
struct HostContext
{
    std::function<QWidget *()> window;
    std::function<void(const QString &)> log;
    std::function<QString()> configDir;
    std::function<void(double)> setPlayhead;   // 通用时间游标
    std::function<bool(const QString &, const QString &)> invoke; // 通用命令调用
};
```

约束：

- 不把 `MainWindow*`、`QApplication*`、内部容器传进组件。
- 不在 `HostContext` 里堆业务命令；业务能力走模块公开 API 或 `Registry`。
- `invoke` 只转发通用命令 id；组件自己的业务 action 不让宿主实现。
- 模块专属的 UI 绑定（例如脚本面板）不作为宿主服务提供，由该模块自己的贡献创建。

## 生命周期

1. 启动：`ExtensionManager::scanStandardLocations()` 扫描动态库，`ExtensionRegistry` 收集内置和动态入口。
2. `ExtensionManager::activate()` 读取禁用列表，按顺序调用每个组件的 `contribute()`，填充 `Registry`。
3. 宿主执行 `windowHooks`，再创建 Ribbon/Dock/Settings。
4. 启停：设置写配置，提示重启；当前没有热卸载/热重载，不要依赖 `ExtensionManager::deactivate()` 做运行时释放。
5. 关闭：宿主处理窗口/布局；业务状态由模块的保存逻辑处理。

## UI 归属

- 内置组件可以创建自己的 QWidget，但只挂在宿主提供的容器下。
- 动态组件只能声明 UI，宿主按 `.ui`、schema、白名单控件创建。
- 模块贡献 Dock/Ribbon/Settings，不直接操作 `MainWindow` 的私有成员。
- 通用 `.ui` 对话框由 `UiDialogEngine` 加载；扩展只提供路径和取数回调。

## 新增扩展检查

- 新增目录是否只依赖公开头？有没有 include `MainWindow.h`？
- 是否通过 `Registry` 贡献，而不是直接 `new QAction` / `addDockWidget`？
- 动态扩展是否只走 C ABI？有没有跨边界传 Qt 对象？
- manifest/ABI 校验、禁用列表、重启生效是否覆盖？
- 格式扩展是否有 `tests/fixture` 和解析断言？
- 删除该扩展后，app 是否仍可构建运行？

# PlotScope 组件与扩展扩展模型 v1

## 一句话

- **内部组件：宿主给 C++ API，组件直接调用，自己造控件。**
- **外部扩展：扩展给 JSON manifest，app 解析，app 造控件。**

内部组件没有 JSON manifest；`ComponentManifest` 只是 C++ 结构体，用于展示和依赖检查，不参与解析。

## 两层模型

### 第一层：贡献注册

内部与外部都向 `Registry` 注册贡献：

- `addLoader`：数据文件 loader descriptor
- `addDataSource`：完整数据来源 descriptor（文件 / 串口 / TCP / UDP 统一）
- `addProtocol`：协议 vtable
- `addRibbon`：Ribbon 页 / 面板 / 条目
- `addDock`：Dock 面板
- `addAction`：组件自有 action

内部组件写法：

```cpp
void contribute(Registry &registry, const HostServices &host) override
{
    registry.addLoader(&gnssLoaderDescriptor);
    registry.addDock(DockContribution{
        "dock.gnss.skyplot", "卫星星空图", Qt::RightDockWidgetArea, true, 10,
        [host](QWidget *parent) {
            auto *w = new GnssSkyPlotWidget(parent);
            // 使用 host 服务
            return w;
        }
    });
}
```

外部扩展写法：

```json
{
  "id": "vendor.gnss",
  "capabilities": ["loader", "dock"],
  "dock": {
    "id": "vendor.gnss.status",
    "title": "厂商 GNSS",
    "area": "right",
    "widget": "table"
  }
}
```

### DataSource 统一模型

PJ4 中 `data_loader` 与 `data_streamer` 共用 `DataSourceExtensionBase`，文件与实时流由 capabilities / manifest 声明，不拆两个基类。PlotScope 对齐这一模型：

```cpp
struct DataSourceSpec
{
    QString id;                          // gnss.nmea
    QString label;                       // NMEA
    QVector<TransportKind> transports;   // File / Serial / Tcp / Udp
    MessageParserSpec parser;            // 解析器
    QStringList extensions;              // 文件过滤
};
```

宿主只保留一个注册表：

```cpp
void addDataSource(const DataSourceSpec &source);
const QVector<DataSourceSpec> &dataSources() const;
```

- 文件打开与设备连接只是按 `transports` 过滤，不再走 loader/source 两套入口。
- `MessageParser` 与 `DataSource` 解耦，NMEA parser 可被文件源、串口源、TCP 源复用。
- 运行实例统一叫 `DataSourceSession`：文件是一次有限会话，串口是持续会话。
- 旧 ABI 的 `PS_FAMILY_DATA_LOADER` / `PS_FAMILY_DATA_SOURCE` 仅作兼容，app 层不再暴露。

当前落地：

- `Registry` 只有 `addDataSource / dataSources()`，`addLoader` 已删除；
- `DataSourceRegistry` 构造函数不再写死任何 `ps_extension_entry_*`；
- 过渡组件 `BuiltinDataSourcesComponent` 集中登记现有内建入口，后续由 GNSS / FlightLog 组件接管；
- `MainWindow` 只遍历 `m_components.dataSources()` 注册。

## 第二层：控件创建

| 类型 | 内部组件 | 外部扩展 |
|---|---|---|
| 菜单结构 | 声明 | 声明 |
| 复杂控件 | 组件自己 `new` | app 按白名单创建 |
| 宿主指针 | 不传裸 `MainWindow*`，传 `HostServices` | 不传宿主对象 |
| 生命周期 | Qt 父子对象 | 宿主包装、销毁 |

内部组件不能直接传 `MainWindow*`，原因：

- 组件可删，不能反向绑死宿主；
- 不暴露 MainWindow 私有成员；
- 便于测试时传假宿主；
- 以后拆分宿主不影响组件。

## HostServices

窄接口，只暴露组件需要的宿主能力：

```cpp
struct HostServices
{
    std::function<QWidget *()> window;
    std::function<void(const QString &)> log;
    std::function<QString()> configDir;
    std::function<bool(const QString &, const QString &)> runCommand;
    std::function<QDockWidget *(const QString &)> dock;
};
```

不传 `MainWindow*`，不传 `QApplication*`，不传内部容器对象。

## Dock 贡献

```cpp
struct DockContribution
{
    QString id;
    QString title;
    Qt::DockWidgetArea area;
    bool visible;
    int order;
    std::function<QWidget *(QWidget *parent)> create;
};
```

- `create` 非空：内部组件自己返回 `QDockWidget*`，宿主统一 `addDockWidget`、设置 `objectName`、可见性。
- `create` 为空：宿主按 `id` 构建内置面板，如 `dock.src`、`dock.prop`、`dock.tree`、`dock.video`、`dock.script`、`dock.log`。
- 外部扩展不填 `create`，只声明 `widget` 类型。

内置 Dock 当前由 `ShellComponent` 声明：

| id | 标题 | 区域 | 默认 |
|---|---|---|---|
| `dock.src` | 数据源 | 左 | 显示 |
| `dock.prop` | 属性面板 | 右 | 显示 |
| `dock.tree` | 对象树 | 右 | 显示 |
| `dock.video` | 视频同步 | 右 | 隐藏 |
| `dock.script` | 脚本 · Lua 5.4 | 右 | 显示 |
| `dock.log` | 日志 | 底部 | 显示 |

`MainWindow::createDocks()` 只做：

1. 按 `order` 遍历 `m_components.docks()`；
2. `create` 或 `buildDockWidget(id)` 得到 `QDockWidget*`；
3. 统一 `setObjectName / setWindowTitle / addDockWidget / setVisible`；
4. 对 `dock.prop + dock.tree` 做纵向 split；
5. 对 `dock.prop + dock.script`、`dock.log` 做 resize。

布局关系后续可扩展为 `splitWith` / `sizeHint` 字段，不写死在宿主。

## Ribbon 贡献

```cpp
struct RibbonContribution
{
    QString page;
    QString panel;
    QVector<RibbonItem> items;
    QVector<RibbonPanel> panels;
    int order;
};

struct RibbonItem
{
    QString action;
    QString label;
    QString arg;
    bool large;
};
```

- 简单按钮走 `items`，宿主创建 `QAction` 并查 `CommandRegistry`。
- 复杂控件走 `RibbonPanel.widget`，宿主按 ID 构建，如 `sourceGallery / plotGallery / layoutPicker / viewToggles`。
- 内部组件需要自定义复杂控件时，可后续扩展 `widgetFactory`，由组件返回 `QWidget*`。
- 外部扩展只允许声明白名单 `widget` 类型。

## 安全与性能

### 性能

- 贡献声明只在启动时处理一次，几十个按钮是微秒级。
- 真正耗时的是 Qt 控件创建，Ribbon/Dock 构建本身不是瓶颈。
- 点击只查命令表，不做动态解析，不影响绘图刷新。

### 安全

内部组件：

- 同一次编译、同一个 Qt、同一个 ABI；
- 有编译期类型检查；
- 字符串命令 ID 需要启动时校验，拼错写日志。

外部扩展：

- JSON 必须过 schema；
- `widget` 必须白名单；
- `action` 必须已在 `CommandRegistry` 注册；
- 不执行 manifest 中的脚本、不加载任意类名、不允许任意 `.so` 路径；
- 扩展崩溃不能带崩 app，UI 创建和销毁由宿主兜底。

## 后续组件

### GNSS 组件（已落地首版）

`src/extensions/builtin/gnss` 现在是单一 target `ps_gnss`，已迁入 NMEA/UBX/OEM7，删除目录后 app 仍可构建。

```text
src/extensions/builtin/gnss/
  loaders/nmea
  loaders/ubx
  loaders/novatel
  codec/nmea
  codec/ubx
  codec/novatel
  debug/        串口/TCP 会话、指令控制台、原始报文
  ui/           卫星星空图、SNR、定位质量
```

当前贡献 `data_source`（NMEA/UBX/OEM7）；后续补 protocol / ribbon / dock。一个目录、一个 target `ps_gnss`。

### 飞控日志组件

```text
src/extensions/builtin/flightlog/
  loaders/ulog
  loaders/apbin
  sources/mavlink
  analysis/     飞行阶段、消息速率、健康状态
  ui/
```

贡献 data source / ribbon / dock。一个目录、一个 target `ps_flightlog`。

CSV、MCAP、ROS bag、MDF4 不并入，仍留在通用 loader。

## 验收

- 删除 `src/extensions/builtin/gnss` 或 `src/extensions/builtin/flightlog` 后，app 仍可构建运行，只缺对应功能。
- 内部组件不出现 `MainWindow*` 参数。
- 外部扩展 manifest 缺字段、未知命令、未知 widget 时，启动日志报错且不影响其他页面。
- 内置 Ribbon 六页、六个 Dock 均由贡献生成，`MainWindow` 不再直接 `new QAction` / `new QDockWidget` 业务面板。

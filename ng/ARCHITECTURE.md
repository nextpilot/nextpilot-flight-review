# PlotScope NG — 新架构设计

> 这是一套从零设计的新骨架，独立于仓库现有代码（`src/`）。目标不是给旧代码打补丁，
> 而是验证一套更干净的数据绘图分析软件架构：数据模型统一、模块可独立装卸、
> 脚本与 headless 是一等公民。

---

## 1. 本质与总原则

数据绘图分析软件 = **三条主干 + 一个地基**。

- 地基：**统一数据模型**。所有代码围绕 `Dataset / Series` 转。
- 主干一（数据）：把各种来源变成 `Dataset`。
- 主干二（视图）：把 `Series` 渲染成图、支持交互。
- 主干三（自动化）：把人的操作变成可复现的意图序列。

四条硬原则：

1. **一个模型走天下**：没有第二套数据抽象，没有格式名渗透进绘图代码。
2. **谁创建谁拥有，谁拥有谁撤销**：用 Qt 父子所有权消解"卸载"难题。
3. **通信只有三种**：能力注入、typed 事件、Qt 信号槽（context object 自动断连）。
4. **意图是公共货币**：GUI 点击和脚本调用产出同一种意图，走同一个执行器。

## 2. 分层

```
app/            CLI / GUI 入口（只做装配与参数解析）
  ↓
host/           装载循环、依赖拓扑、意图执行器、生命周期驱动
  ↓
modules/        器官：data / plot / script（编译进 app，可配置关闭）
  ↓
core/           地基：Dataset/Series、DataRepository、事件、意图定义
  ↓
(可选) kernel   重计算：LOD/滤波/对齐，纯计算，无 UI 无插件概念
```

依赖方向严格向下。**core 不依赖任何上层；module 之间零直接依赖。**

## 3. 统一数据模型（core）

数据模型对齐仓库 kernel（`ps-ndf/src/typed.rs`）的 typed 表模型——**Dataset 是多张二维表的库，不是扁平 Series 列表**：

```cpp
enum class ValueType { F64, F32, I64, U64, I32, U32, I16, U16, I8, U8, Bool, Text };

struct Column {              // 一列（列式存储）
    QString name, unit;
    ValueType type;          // 12 种 typed 值，对齐 PsValueType
    std::vector<QVariant> values;
    std::vector<bool> nulls; // 每列独立 null 位图
};

struct Table {               // 一张表 = 一个 topic
    QString name;
    QVector<qint64> timeUs;  // 表级共享时间轴，UTC 微秒（不是列里的 double 秒）
    QVector<quint64> seq;    // 表内序号，微秒相同时排序
    QVector<Column> columns; // 表内所有列共享 timeUs
};

struct ChannelView {         // 绘图视图：把 (table,column) 投影成 (t_sec,value) f64
    QString table, column, name, unit;
    ValueType type;
    QVector<double> time, value;
};

class Dataset {              // 库：装多张表，表名在库内唯一
    int addTable(name, QVector<qint64> timeUs);
    bool addColumn(tableName, Column);
    const Table* table(QString name) const;
    std::optional<ChannelView> channel("table.column") const;
};

class DataRepository {       // 数据的唯一所有者
    DatasetId add(Dataset);
    void remove(DatasetId);
    const Dataset* get(DatasetId) const;
};
```

关键约束（与 kernel 一致）：
- **时间轴是表级的、UTC 微秒 i64**，表内所有列共享；绘图时才投影成秒。
- **列是 typed 的**，带独立 null 位图；不是清一色 f64。
- **Dataset 归 DataRepository 所有**，不归任何模块。模块卸载不删数据。
- 不可变快照：派生/变换产生新 Dataset，不改原对象。
- ng 用纯 C++ 值类型表达这套语义，独立于 Rust kernel；未来可平滑接入真实 `PsDataset`。

- **Dataset 归 DataRepository 所有**，不归任何模块。模块卸载不删数据。
- 数据快照不可变：派生/变换产生新 Dataset，避免多处修改同一份数据。
- 时间统一 UTC 秒，起点对齐作为视图属性，不复制底层数组。

## 4. 模块与扩展

### 4.1 统一接口

器官（module）和知识包（extension）实现同一个接口，宿主一视同仁：

```cpp
class IModule : public QObject {
    virtual ModuleInfo info() const = 0;              // id/version/requires/provides/kind
    virtual void initialize(ModuleContext&) = 0;      // 声明能力、订阅事件，不碰 UI
    virtual void onAttach(UiContext&) {}              // 建 UI；headless 时宿主不调用
};
```

- `ModuleKind::Builtin`：编译进 app，默认启用，可配置关闭。
- `ModuleKind::Extension`：独立动态库，运行时扫描、ABI 校验后加载。

### 4.2 依赖前置解析

`ModuleInfo::requires` 声明依赖的能力。宿主启动时：

1. 收集所有模块的 `provides/requires`。
2. 拓扑排序；循环依赖启动即拒绝并打印环路径。
3. 缺依赖的模块不加载（记日志），其余继续——失败隔离，不连坐。

### 4.3 独立装卸的两个机制

**所有权即撤销**：模块自己创建 dock/action，`parent` 给自己；宿主只放置、不接管。
卸载时 `delete module` → Qt 父子树自动回收全部 UI、自动断开全部信号槽。
没有 token、没有撤销协议，这是 Qt 原生语义。

**依赖前置**：见 4.2，运行时不碰运气、不层层 bridge。

## 5. 统一意图层（automation 的灵魂）

> GUI 的每次交互和脚本的每行代码，最终都变成同一种意图对象，交给同一个执行器。

```cpp
struct Intent {
    QString op;             // "data.load" / "plot.addSeries" / "plot.render" ...
    QVariantMap params;
};
```

- GUI 按钮点击 → 生成 Intent → 执行器。
- 脚本 `ps.plot.add(...)` → 生成同样的 Intent → 执行器。
- **意图可序列化**：一串 Intent 就是 Job（JSON），可录制、可回放、可版本管理。

执行器是唯一知道"如何执行意图"的地方。它把意图分派给提供对应能力的模块。

## 6. 自动化与 headless

- 脚本引擎是 core 服务，脚本只拿到门面 `ps`，永远碰不到 QWidget 和模块内部对象。
- **headless = 装载模块但不调用 `onAttach()`**：没有第二套装配代码。
- GUI 与 CLI 共用同一个装载循环和同一个意图执行器；区别只在是否提供 `UiContext`。
- CLI：`--list-modules`、`--info <file>`、`--run <job.json>`、`--export`。
- CI：headless 跑 Job + 导出 PNG/CSV 做基线对比。

## 7. 我会明确砍掉的复杂度

- `ServiceRegistry` 那种 `type_index→void*` 服务定位器 → 构造函数注入。
- 全局单例 `instance()` → 能力从 Context 注入。
- 遍布全局的命令 id 字符串总线 → QAction 信号 + 意图对象。
- GUI/CLI 两套装配 → 共用装载循环。
- 运行时热重载 UI → 明确"重启生效"。

## 8. 目录结构

```
ng/
├── ARCHITECTURE.md       本文档
├── CMakeLists.txt        独立构建，复用仓库 deps/Qt
├── core/                 数据模型/仓库/事件/意图（无 QWidget）
├── host/                 IModule/装载循环/意图执行器/App
├── modules/              器官：data / plot
├── extensions/           知识包：csv
├── app/                  CLI 入口
└── tests/                架构契约测试
```

## 9. 落地顺序

1. core：数据模型 + 意图定义。
2. host：装载循环 + 依赖排序 + 执行器。
3. 一条竖切：CSV 扩展 + 数据模块 + plot 意图，端到端跑通。
4. 再铺开其余格式与功能。
5. 脚本引擎与 Job 录制（前三步完成后近乎免费）。

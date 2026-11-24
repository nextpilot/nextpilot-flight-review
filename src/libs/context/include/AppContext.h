#pragma once

#include "DatasetHub.h"
#include "SourceHub.h"
#include "ToolboxHub.h"
#include "RegistryTree.h"
#include "IngestPipeline.h"
#include "InteractionHub.h"
#include "StatusHub.h"
#include "MessageLog.h"
#include "PersistentStateHub.h"
#include "DerivedSeriesHub.h"
#include "Result.h"
#include "Selection.h"
#include "SettingsStore.h"
#include "Transform.h"

#include <QHash>
#include <QVariant>
#include <functional>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace nextpilot::plotscope::context
{

using nextpilot::plotscope::uischema::TransformHub;

using nextpilot::plotscope::foundation::Result;
using nextpilot::plotscope::dataset::DatasetHub;
using nextpilot::plotscope::dataset::SourceHub;
using nextpilot::plotscope::registry::ToolboxHub;
using nextpilot::plotscope::registry::RegistryTree;
using nextpilot::plotscope::uischema::Selection;


// 应用上下文：**进程级运行态的唯一持有者**。
//
// 为什么需要它（而不是让模块各自去找）：
//   在此之前，app 级状态分散在三处——
//     App 持 repo/recorder、RuntimeHost 持 settings/toolbox、注册表是全局静态。
//   于是模块取数据要走 ctx.data()，取设置要走 ctx.host().settings()，
//   两条路径、两种持有方式。新增能力时不知道该挂哪儿——这是"上帝类"的温床。
//
// AppContext 把**运行态**收敛到一处，模块只认一个入口：
//   ctx.app().data() / .settings() / .toolbox() / .sources() / .ingest()
//
// 铁律（必须守住）：
//  1. **绝不包含任何 QWidget 指针或 GUI 专属类型**。
//     CLI（headless）与 GUI 共用同一份 AppContext；若这里引入 QWidget，
//     CLI 的依赖树会被污染，"纯 Core"承诺失效。
//  2. **不含会话级/窗口级状态**（当前页签、视口缩放、面板显隐）——那些是 GUI 展示态，
//     不可序列化、CLI 不需要。可序列化的用户偏好走 SettingsStore 的作用域。
//  3. **不含编译期资产目录**（ParserHub / RecordingFormat / RecordingBackend /
//     BuiltinPack）——它们靠静态初始化自注册，与进程同生共死，收进来反而
//     造成"多 App 实例时重复初始化"。
//
// 依赖方向：core → 无上层依赖；host 与 app 都只向下取用。
// 交互注册（viewAction / property / transform）已剥离到 InteractionHub，
// 经 interactions() 取用——它们是运行时路由行为，不是运行态数据。
class AppContext : public QObject
{
    Q_OBJECT
public:
    // settingsPath 为空时设置存内存（测试/headless）；否则读写该 JSON 文件。
    explicit AppContext(const QString &settingsPath = QString(), QObject *parent = nullptr);
    ~AppContext() override;

    // === 运行态访问器（模块唯一入口）===

    // 数据集仓库（数据本体由 Rust 拥有，这里只是句柄仓库）。
    DatasetHub &data() { return m_repo; }
    const DatasetHub &data() const { return m_repo; }

    // 统一设置（三作用域）。
    SettingsStore &settings() { return m_settings; }
    const SettingsStore &settings() const { return m_settings; }

    // 工具箱（扩展包贡献登记处）。
    ToolboxHub &toolbox() { return m_toolbox; }
    const ToolboxHub &toolbox() const { return m_toolbox; }

    // 数据源实例登记（多源并存）。
    SourceHub &sources() { return m_sources; }
    const SourceHub &sources() const { return m_sources; }

    // 摄取管道（单前端 → 多解析器扇出 → 多数据集）。
    IngestPipeline &ingest() { return m_ingest; }

    // 交互注册表（viewAction / property / transform 的注册与派发）。
    // 这是运行时路由行为，与上面的静态运行态数据分开：AppContext 只 owns
    // 这个成员对象、不实现任何 register*/dispatch* 方法；逻辑归 RuntimeHost 编排。
    InteractionHub &interactions() { return m_interactions; }
    // 状态栏登记处（验收 7 / D8 推模式）：模块只推文案，壳订阅渲染。
    StatusHub &status() { return m_status; }
    const StatusHub &status() const { return m_status; }
    const InteractionHub &interactions() const { return m_interactions; }

    // 全系统注册树：RegistryNode 继承即上树，(kind,name) 定位。
    // 组合根唯一持有——不做静态单例，测试间树完全隔离。
    RegistryTree &registryTree() { return m_registryTree; }
    const RegistryTree &registryTree() const { return m_registryTree; }

    // 派生序列注册表：独立派生字段（不写回 dataset，原始数据零污染）。
    // data.derive 登记结果，data.series/plot.channel 按派生名穿透读数，
    // 数据树经 data.derive.list 展示。
    TransformHub &transforms() { return m_transforms; }
    const TransformHub &transforms() const { return m_transforms; }
    DerivedSeriesHub &derivedHub() { return m_derivedHub; }
    const DerivedSeriesHub &derivedHub() const { return m_derivedHub; }

    // 进程级消息流（终端 / 输出 / 问题 / 调试控制台的共同数据源）。
    // 只存纯数据：级别 + 来源 + 正文 + 定位，无 QWidget、无第三方位图。
    // appendMessage 走这里而不是直接 MessageLog::append，是为了统一发信号——
    // 面板只订阅 messageAppended，不轮询、不互相引用。
    MessageLog &messages() { return m_messages; }
    const MessageLog &messages() const { return m_messages; }

    // 结构化状态的登记/收集（会话快照与工作区预设的内容来源）。
    PersistentStateHub &states() { return m_states; }
    const PersistentStateHub &states() const { return m_states; }
    void appendMessage(const QString &level, const QString &source, const QString &text,
                       const QString &locator = QString());

    // === 命令接缝（外壳注入；headless 不注入即为空）===
    //
    // 模块的呈现层要执行"命令"（右键、标题栏、设置页的按钮），但命令层
    // 属于外壳（快捷键、Ribbon、命令面板共用一份注册表）。模块不能反向
    // 依赖 ps_gui，于是外壳把两个能力注入到这里：
    //   invoke：按命令 id 执行（与按钮/快捷键同一条路径）；
    //   when ：按表达式求值，extra 是被求值点的局部键（如 item.* 的 View 作用域）。
    // 二者为空时模块应把声明当作不可用，而不是自己重造一套解析。
    void setCommandHooks(std::function<bool(const QString &, QString *)> invoke,
                         std::function<bool(const QString &, const QHash<QString, QVariant> &)> when);
    bool invokeCommand(const QString &commandId, QString *error = nullptr) const;
    // extra 里给出的键**只对本次求值可见**：item.* 之类按 View 作用域临时合入，
    // 不写进全局上下文，切视图自然失效（D19）。
    bool evaluateWhen(const QString &when,
                      const QHash<QString, QVariant> &extra = {}) const;

    // === 当前活动视图（指针，不是副本）===
    //
    // "用户正在看哪个源、哪条通道"是跨前端共享的运行态：
    // GUI 点一下通道，脚本随后 query 应当得到同一答案。
    // 所以它属于进程级上下文，而**不是** GUI 内部状态。
    // 但存的是 id 指针而非数据副本——数据始终在仓库里，这里不复制一份。
    //
    // **两套 id 空间，不可混用**：
    //   · activeSourceId —— **数据源** id（SourceHub 的键）。
    //     谁重载/导出/关闭，以它为准。
    //   · activeDatasetId —— **数据集** id（DatasetHub 的键）。活动通道
    //     落在哪个数据集上。
    // 一个源可扇出多个解析器、产出多个数据集，故二者不是一回事。
    // 曾把 datasetId 写进 activeSourceId，于是 data.reload 拿数据集 id 去查源表，
    // 必然找不到 —— 表现为"重载"按钮永远报"没有可重载的数据源"。
    int activeSourceId() const { return m_activeSourceId; }
    void setActiveSourceId(int id);

    int activeDatasetId() const { return m_activeDatasetId; }
    QString activeTable() const { return m_activeTable; }
    QString activeColumn() const { return m_activeColumn; }

    // 设置当前活动通道（数据集 + 表 + 列）。
    //
    // 这是**视图之间唯一的协作通道**：数据集树被双击 → 记下活动通道 →
    // 绘图视图订阅到变化 → 去取数并渲染。
    //
    // 为什么走上下文而不是让两个视图互相引用：
    //   视图之间若直接依赖，就成了网状结构，加一个视图要改多个文件。
    //   走共享状态则是星形——新增视图只订阅信号，不影响已有视图。
    //
    // 注意：它只改活动**数据集**，不动 activeSourceId —— 源 id 由调用方
    // 显式给出（数据集 → 源的反查用 SourceHub::sourceOfDataset）。
    void setActiveChannel(int datasetId, const QString &table, const QString &column);

    void clearActiveChannel();

    // === 统一选中对象（跨前端共享）===
    //
    // 与活动通道分开：活动通道只表达"正在画哪一列"，而选中对象是属性面板的
    // 输入——它可以是源/表/列/曲线/轴/协议字段中的任意一个。二者会互相印证，
    // 但不是一回事（例如点画布上的一条曲线选中 Series，活动通道不变）。
    // 纯数据（Selection），故 headless/脚本可读，视图之间不互相引用。
    const Selection &selection() const { return m_selection; }
    void setSelection(const Selection &selection);
    void clearSelection();

    // === 源级时间 offset（第二梯队 #204：多源时间对齐）===
    //
    // 
    // 画布施加"曲线 x + 源 offset"，一键 Align start/center/end 按各源时间范围
    // 计算使多源对齐。offset 是**源级配方**（宿主态），不改数据、不入视图状态；
    // 画布通过 sourceTimeOffsetChanged 刷新重取显示。
    double sourceTimeOffset(int sourceId) const { return m_sourceTimeOffsets.value(sourceId, 0.0); }
    void setSourceTimeOffset(int sourceId, double seconds);
    // 已设置非零 offset 的源 id 列表（plot.offset.clear 全清用）。
    QVector<int> sourceTimeOffsetIds() const { return m_sourceTimeOffsets.keys(); }

Q_SIGNALS:
    // 当前活动源变化（GUI 树、脚本、CLI 都可订阅）。
    void activeSourceChanged(int sourceId);
    // 源级时间 offset 变化（画布订阅后刷新；Align/脚本/GUI 共用一条路径）。
    void sourceTimeOffsetChanged(int sourceId, double seconds);
    // 当前活动通道变化（画布、状态栏、脚本都订阅它）。
    void activeChannelChanged(int datasetId, const QString &table, const QString &column);
    // 统一选中对象变化：属性面板订阅它刷新。参数是选中对象的 JSON（跨线程安全）。
    void selectionChanged(const QJsonObject &selection);

    // 新消息入库（终端 / 输出 / 问题 / 调试控制台订阅）。参数全为可序列化字符串，
    // 面板据此增量追加一行，无需重读整条日志。
    void messageAppended(const QString &level, const QString &source, const QString &text,
                         const QString &locator);

    // 派生序列已算出（data.derive 的结果）。这是**事件**而非状态，参数是可序列化的
    // 数值数组（非 GUI 专属类型），故不违反"上下文不含 QWidget"的铁律。
    // 数据集本身零改动：画布订阅后作为 generated series 叠加显示，与活动通道体系并列。
    void derivedSeriesReady(const QString &name, const QVector<double> &time,
                            const QVector<double> &value);

    // 数据加载生命周期（data.load 真实摄取入口驱动，非定时器刷值）：
    // dataLoadStarted 在进入摄取管道前发；dataLoadFinished 在摄取返回后发，
    // ok 表示成功。状态栏/面板订阅后推常驻文案；headless 下无订阅者亦无副作用。
    void dataLoadStarted();
    void dataLoadFinished(bool ok);

private:
    // 注册树必须先声明（成员逆序析构：后声明的节点先亡、向仍活着的树摘除自身）。
    RegistryTree m_registryTree;
    DatasetHub m_repo;
    SettingsStore m_settings;
    ToolboxHub m_toolbox;
    SourceHub m_sources;
    IngestPipeline m_ingest; // 依赖 m_repo，故必须在其后构造

    InteractionHub m_interactions;
    StatusHub m_status; // 状态栏登记处：模块推文案，壳订阅渲染（不含 QWidget）
    MessageLog m_messages;        // 进程级消息流（纯数据，面板订阅后增量渲染）
    PersistentStateHub m_states;  // 结构化状态的登记表（收集/恢复由外壳驱动）
    TransformHub m_transforms;
    DerivedSeriesHub m_derivedHub; // 派生序列注册表：独立派生字段
    std::function<bool(const QString &, QString *)> m_invokeCommand;
    std::function<bool(const QString &, const QHash<QString, QVariant> &)> m_evalWhen;
    Selection m_selection;       // 当前统一选中对象

    int m_activeSourceId = -1;   // 数据源 id（SourceHub 键）
    QHash<int, double> m_sourceTimeOffsets; // 源级显示时间 offset（秒，缺省 0）
    int m_activeDatasetId = -1;  // 活动通道所在的数据集 id（DatasetHub 键）
    QString m_activeTable;
    QString m_activeColumn;
};

} // namespace nextpilot::plotscope::context

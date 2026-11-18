#pragma once

#include "DataRepository.h"
#include "DataSourceRepository.h"
#include "ExtensionRegistry.h"
#include "IngestPipeline.h"
#include "SettingsStore.h"

#include <QObject>
#include <QString>

namespace ng::core
{

// 应用上下文：**进程级运行态的唯一持有者**。
//
// 为什么需要它（而不是让器官各自去找）：
//   在此之前，app 级状态分散在三处——
//     App 持 repo/recorder、ModuleHost 持 settings/toolbox、注册表是全局静态。
//   于是器官取数据要走 ctx.data()，取设置要走 ctx.host().settings()，
//   两条路径、两种持有方式。新增能力时不知道该挂哪儿——这是"上帝类"的温床。
//
// AppContext 把**运行态**收敛到一处，器官只认一个入口：
//   ctx.app().data() / .settings() / .toolbox() / .sources() / .ingest()
//
// 铁律（必须守住）：
//  1. **绝不包含任何 QWidget 指针或 GUI 专属类型**。
//     CLI（headless）与 GUI 共用同一份 AppContext；若这里引入 QWidget，
//     CLI 的依赖树会被污染，"纯 Core"承诺失效。
//  2. **不含会话级/窗口级状态**（当前页签、视口缩放、面板显隐）——那些是 GUI 展示态，
//     不可序列化、CLI 不需要。可序列化的用户偏好走 SettingsStore 的作用域。
//  3. **不含编译期资产目录**（ParserRegistry / RecordingFormat / RecordingBackend /
//     BuiltinPack）——它们靠静态初始化自注册，与进程同生共死，收进来反而
//     造成"多 App 实例时重复初始化"。
//
// 依赖方向：core → 无上层依赖；host 与 app 都只向下取用。
class AppContext : public QObject
{
    Q_OBJECT
public:
    // settingsPath 为空时设置存内存（测试/headless）；否则读写该 JSON 文件。
    explicit AppContext(const QString &settingsPath = QString(), QObject *parent = nullptr);
    ~AppContext() override;

    // === 运行态访问器（器官唯一入口）===

    // 数据集仓库（数据本体由 Rust 拥有，这里只是句柄仓库）。
    DataRepository &data() { return m_repo; }
    const DataRepository &data() const { return m_repo; }

    // 统一设置（三作用域）。
    SettingsStore &settings() { return m_settings; }
    const SettingsStore &settings() const { return m_settings; }

    // 工具箱（知识包贡献登记处）。
    ExtensionRegistry &toolbox() { return m_toolbox; }
    const ExtensionRegistry &toolbox() const { return m_toolbox; }

    // 数据源实例登记（多源并存）。
    DataSourceRepository &sources() { return m_sources; }
    const DataSourceRepository &sources() const { return m_sources; }

    // 摄取管道（单前端 → 多解析器扇出 → 多数据集）。
    IngestPipeline &ingest() { return m_ingest; }

    // === 当前活动视图（指针，不是副本）===
    //
    // "用户正在看哪个源、哪条通道"是跨前端共享的运行态：
    // GUI 点一下通道，脚本随后 query 应当得到同一答案。
    // 所以它属于进程级上下文，而**不是** GUI 内部状态。
    // 但存的是 id 指针而非数据副本——数据始终在仓库里，这里不复制一份。
    int activeSourceId() const { return m_activeSourceId; }
    void setActiveSourceId(int id);

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
    void setActiveChannel(int datasetId, const QString &table, const QString &column);

    void clearActiveChannel();

Q_SIGNALS:
    // 当前活动源变化（GUI 树、脚本、CLI 都可订阅）。
    void activeSourceChanged(int sourceId);
    // 当前活动通道变化（画布、状态栏、脚本都订阅它）。
    void activeChannelChanged(int datasetId, const QString &table, const QString &column);

private:
    DataRepository m_repo;
    SettingsStore m_settings;
    ExtensionRegistry m_toolbox;
    DataSourceRepository m_sources;
    IngestPipeline m_ingest; // 依赖 m_repo，故必须在其后构造

    int m_activeSourceId = -1;
    QString m_activeTable;
    QString m_activeColumn;
};

} // namespace ng::core

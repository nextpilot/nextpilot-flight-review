#pragma once

// M2.8 Registry 机制整改（R1）：注册节点基类。
//
// 范式定稿（docs/roadmap/M2.8-Registry整改.md §2.1-2.2）：借鉴 RT-Thread rt_object——
// 注册不是某个管理器的功能，是**对象自带的属性**。继承 RegistryNode 即自动获得
// 注册/卸载能力，全体注册对象挂一棵 RegistryTree，(kind, name) 即 rt_object 的
// (type, name) 二元组——name 给人看，kind 给机判。
//
// 硬约束（rt_object 照搬会翻车的那条）：值条目（DataSourceInfo/UiSlot/贡献 struct）
// 按值存于 QHash/QList，扩容即搬家，不能做成节点。因此分两层：
//   RegistryTree（一棵树，组合根持有）
//   └── RegistryNode 节点（活对象：注册表/模块——构造自动上树，析构自动摘除）
//         └── 条目 = 节点内的值数据（不上树），经节点暴露 count/keysAsStrings
//
// kind 是身份不是行为，构造时定死，子类零样板。
// kind 集合 = 核心封闭集（Dataset/Source/DerivedSeries/Interaction/UiSlot/Extension，
// M3 扩展包贡献条目不新增）+ 存量静态表节点集（PanelFactory/BuiltinModule/Parser/
// RecordingFormat/RecordingBackend——M2.8 遗留 4 收口：全量上树无盲区后扩容，
// 之后只减不增，M3 插件贡献经既有 kind 表达）。

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace nextpilot::plotscope::registry
{

class RegistryTree;

// 注册表类型标签。diag JSON 与 (kind,name) 定位共用。
enum class RegistryKind : quint8
{
    Dataset,
    Source,
    DerivedSeries,
    Interaction,
    UiSlot,
    Extension,
    PanelFactory,
    BuiltinModule,
    Parser,
    RecordingFormat,
    RecordingBackend
    // A8b：contributes 是 Extension 的属性面（ToolboxHub 内），不另立 kind——
    // "贡献"与"扩展包"是同一个注册表，diag.registries 保持 11 类。
};

// kind → 诊断字符串（kebab：dataset / builtin-module ...）。
QString kindName(RegistryKind kind);

// 注册节点基类【范式拍板】：继承即自动注册/卸载，全体节点挂一棵树。
// tree 由组合根传入（禁止全局单例，M2.8 §2.6 拍板 4）。
class RegistryNode : public QObject
{
    Q_OBJECT
public:
    // 身份构造时定死；构造即上树。
    explicit RegistryNode(RegistryTree &tree, RegistryKind kind, const QString &name,
                          QObject *parent = nullptr);
    // 析构自动从树摘除（树先亡时 RegistryTree 析构会摘走本指针，见 RegistryTree）。
    ~RegistryNode() override;

    // ---- 身份（非虚）----
    RegistryKind kind() const { return m_kind; }
    QString name() const { return m_name; }

    // ---- 可观测（子类实现；值条目经节点间接可枚举，不上树）----
    virtual int count() const = 0;
    virtual QStringList keysAsStrings() const = 0;

Q_SIGNALS:
    // 统一信号【拍板：带 key】——消费方可精准响应"某条"（画布联动铺路）。
    // 各子类旧具名信号（datasetAdded 等）保留，与基类信号并存（测试零改动）。
    void objectAdded(QVariant key);
    void objectRemoved(QVariant key);

protected:
    friend class RegistryTree;

    RegistryKind m_kind;
    QString m_name;
    RegistryTree *m_tree = nullptr; // 反向指针：析构自动摘除用；树先亡时被清空
};

} // namespace nextpilot::plotscope::registry

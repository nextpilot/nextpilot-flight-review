#pragma once

// 服务实例化与发现（对标 VSCode platform/instantiation）。
//
// 语义（IKXUTD A1b 施工卡 §1.1，冻结，不得发挥）：
//   - registerSingleton(id, instance, owner, version, typeTag)：同 id 二次注册 →
//     失败 + 诊断，绝不覆盖前者；只存裸指针，不拥有、不析构。
//   - unregisterOwner(owner)：按 owner 整批撤；依赖倒序（后注册的先撤）。
//   - getServiceUnchecked(id)：装配层专用，未命中返回 nullptr。
//   - getService<T>(id)：typeTag 不符 → nullptr + 诊断，命中返回强类型指针。
//   - ids()：当前已登记的全部 id（升序）。
//
// 纯 Qt Core，无 QObject、无上层依赖，headless 与 GUI 共用。instance 的生命周期
// 由注册方（装配层）拥有，本服务只持有指针——避免了跨 DLL 的析构所有权问题。

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <typeinfo>

namespace nextpilot::plotscope::instantiation
{

// 服务类型 → 规范 typeTag。默认用类型的 RTTI 名；各服务接口可在自己的头文件里
// 做全特化给出稳定可读 tag（如 "vscode.workspace"）。注册与取用必须用同一个
// ServiceTypeTag<T>::value()，故二者恒匹配；手填不一致的 typeTag 会被 getService<T> 挡下。
template <class T>
struct ServiceTypeTag
{
    static QString value() { return QString::fromLatin1(typeid(T).name()); }
};

class InstantiationService
{
public:
    struct Entry
    {
        void *instance = nullptr;
        QString owner;
        int version = 0;
        QString typeTag;
        int seq = 0; // 注册序号，用于 unregisterOwner 倒序撤
    };

    // 同 id 重复注册返回 false，并把诊断追加到 diagnostics()。不覆盖原条目。
    void registerSingleton(const QString &id, void *instance, const QString &owner,
                           int version, const QString &typeTag);

    // 按 owner 整批撤；依赖倒序（注册序号大的先撤）。每次撤的 id 记入 removedLog()。
    void unregisterOwner(const QString &owner);

    // 单条撤（装配层重配时用）。命中返回 true。
    bool unregister(const QString &id);

    // 装配层专用：未命中返回 nullptr。不做 typeTag 校验。
    void *getServiceUnchecked(const QString &id) const;

    // 类型安全取用：未命中或 typeTag 不符返回 nullptr 并写诊断。
    template <class T>
    T *getService(const QString &id) const
    {
        const Entry *e = findEntry(id);
        if (!e)
        {
            appendDiagnostic(QStringLiteral("getService: missing id '%1'").arg(id));
            return nullptr;
        }
        const QString expected = ServiceTypeTag<T>::value();
        if (e->typeTag != expected)
        {
            appendDiagnostic(QStringLiteral("getService: typeTag mismatch for '%1' (got '%2', "
                                            "expected '%3')")
                                 .arg(id, e->typeTag, expected));
            return nullptr;
        }
        return static_cast<T *>(e->instance);
    }

    QStringList ids() const;
    QStringList idsByOwner(const QString &owner) const;

    // 注册失败 / 取用失败的诊断记录（含重复 id 拒绝、typeTag 不符）。
    QStringList diagnostics() const;
    // 按撤序记录的 id（供诊断/测试验证"依赖倒序"）。
    QStringList removedLog() const { return m_removedLog; }

    int size() const { return m_entries.size(); }
    bool isEmpty() const { return m_entries.isEmpty(); }
    void clear();

private:
    const Entry *findEntry(const QString &id) const;
    void appendDiagnostic(const QString &msg) const; // 可变成员，const 可取用

    QMap<QString, Entry> m_entries;
    int m_seq = 0;
    mutable QStringList m_diagnostics;
    QStringList m_removedLog;
};

} // namespace nextpilot::plotscope::instantiation

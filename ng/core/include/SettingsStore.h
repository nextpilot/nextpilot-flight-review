#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace ng::core
{

// 键的作用域。三种性质不同的东西，但走同一个存储。
//
//   App        用户偏好，跨工作区（主题、语言、默认目录、启用的扩展）
//   Workspace  会话状态，可恢复（窗口几何、dock 排列、最近打开的源）
//   Document   具体工作区，可分享（plot 布局、曲线样式、时间窗）
//
// 分域不是为了隔离数据，而是为了让"何时存、何时恢复、要不要跟随用户"
// 这件事有明确归属。
enum class SettingsScope
{
    App,
    Workspace,
    Document
};

// 统一设置存储：**app 唯一的持久化出口**。
//
// 设计要点：
//   - 分层键空间：完整键 = "<scope前缀>/<相对路径>"，如 "app/theme"、"ws/window/geometry"；
//   - 类型化读写：QVariant 承载，但只有 JSON 可序列化的类型会落盘；
//   - 变更信号：任何写入都发 changed(scope, key)，上层可订阅；
//   - 原子落盘：先写临时文件再改名，避免半截文件；
//   - **不存 QWidget**：序列化由各贡献方自己负责（core 只存 JSON 文本）——
//     于是 core 保持无 Widgets 依赖，headless 依然可用；
//   - **不知道"恢复"是什么**：它只存取；恢复是上层的编排（见 ARCHITECTURE.md §10）。
//
// 与老代码的差别：老代码是 SettingsStore(QSettings 薄封装) + SettingsManager(强类型 key
// + 变更通知 + 导出) 两层，且 key 散落各处；ng 合成一层 + 显式作用域。
class SettingsStore : public QObject
{
    Q_OBJECT
public:
    // filePath 为空时使用内存存储（测试与 headless 用）。
    explicit SettingsStore(const QString &filePath = QString(), QObject *parent = nullptr);

    // --- 原始读写 ---
    QVariant value(const QString &key, const QVariant &defaultValue = {}) const;
    void setValue(const QString &key, const QVariant &value);
    void remove(const QString &key);
    bool contains(const QString &key) const;
    QStringList keys() const;
    QStringList keysIn(SettingsScope scope) const;

    // --- 作用域便捷读写（自动拼前缀）---
    QString scopedKey(SettingsScope scope, const QString &relative) const;
    QVariant scopedValue(SettingsScope scope, const QString &relative,
                         const QVariant &defaultValue = {}) const;
    void setScopedValue(SettingsScope scope, const QString &relative, const QVariant &value);
    void removeScoped(SettingsScope scope, const QString &relative);

    // --- JSON 块（复杂结构走这里：布局、曲线属性）---
    bool setJson(SettingsScope scope, const QString &relative, const QJsonObject &object);
    QJsonObject json(SettingsScope scope, const QString &relative) const;

    // --- 落盘 ---
    bool flush(QString *err = nullptr);   // 原子写盘
    void load();                          // 从磁盘读入
    QString filePath() const { return m_filePath; }
    bool isPersistent() const { return !m_filePath.isEmpty(); }

Q_SIGNALS:
    // 任一键变化（scope 为该键所属作用域；跨作用域时取首个匹配）。
    void changed(int scope, const QString &key);
    void flushed();

private:
    void loadFromDisk();
    bool saveToDisk(QString *err) const;

    QString m_filePath;
    QHash<QString, QVariant> m_values;
};

} // namespace ng::core

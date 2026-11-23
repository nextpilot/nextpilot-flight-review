#pragma once

// 上下文键的三层作用域。
//
// VS Code 的 ContextKeyService 是 scoped 树：子作用域覆盖父作用域。这里简化为固定三层，
// 求值一律从最内层往外查，先命中者胜：
//   Shell  —— 壳级键（activity / region / document）。
//   Panel  —— 当前面板的键（view.active、本域状态）。
//   View   —— 当前视图实例的键（选区、游标、临时态），最容易变、也最该被内层覆盖。
//
// 规则：键名唯一表见契约文档，模块只能写自己域内声明的键；未定义键在 when 中求值为假。

#include <QHash>
#include <QString>
#include <QVariant>

namespace nextpilot::plotscope::command
{

enum class ContextScope
{
    Shell = 0,
    Panel = 1,
    View = 2,
};

class ContextStack
{
public:
    void set(ContextScope scope, const QString &key, const QVariant &value);
    void clear(ContextScope scope);
    void clearAll();

    // 从最内层（View）到最外层（Shell）查找；未定义返回无效 QVariant。
    QVariant value(const QString &key) const;
    bool contains(const QString &key) const;

    // 合并视图（内层覆盖外层），给"一次性拿到全部键"的调用方（诊断/测试/序列化）用。
    QHash<QString, QVariant> merged() const;

    QHash<QString, QVariant> layer(ContextScope scope) const { return m_layers[int(scope)]; }

private:
    QHash<QString, QVariant> m_layers[3];
};

} // namespace nextpilot::plotscope::command

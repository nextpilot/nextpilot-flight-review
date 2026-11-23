#pragma once

// when 表达式：语法与语义抄 VS Code 的 contextkey（子集），不引入函数/算术/赋值。
//
// 语法白名单（按优先级从紧到松）：
//   !           一元非，绑定最紧
//   == != < <= > >=         比较（两侧同为数值则按数值比，否则按字符串比）
//   in / not in             左侧是右侧集合的成员（左侧为列表时任一元素命中即真）
//   =~ / not =~             正则（QRegularExpression，非法正则视为不匹配）
//   &&          逻辑与
//   ||          逻辑或
//   ( )         布尔分组；在 in 的右侧 ( "a", "b" ) 是值集合
//   "xxx"       带引号字符串；true / false；整数与小数
//   裸标识符     当作上下文键，按真值求值（bool true / 非零数 / 非空串 为真）
//
// 语义约定：
//   - 未定义的键求值为假（与 VS Code 一致），不报错。
//   - 字符串比较忽略大小写；数值比较在两侧都能转成数字时启用。
//   - 解析失败不抛异常：得到一个恒假表达式 + error 文本，调用方负责进诊断。
//
// 性能约定：compile 只在装载期做一次（缓存由 CommandRegistry 持有），
// 运行期 evaluate 只走已经建好的节点树，不再解析字符串。

#include <QString>
#include <QVariant>
#include <memory>

namespace nextpilot::plotscope::command
{

class ContextStack;
struct WhenNode;

class WhenExpression
{
public:
    WhenExpression() = default;   // 空表达式 = 恒真

    // 解析失败时返回恒假表达式，并把原因写进 error（可为空）。
    static WhenExpression compile(const QString &source, QString *error = nullptr);

    bool evaluate(const ContextStack &context) const;

    bool isTrue() const { return empty_; }
    bool isEmpty() const { return empty_; }
    bool isValid() const { return valid_; }
    QString source() const { return m_source; }

private:
    std::shared_ptr<const WhenNode> m_root;
    QString m_source;
    bool empty_ = true;   // 源串为空：恒真
    bool valid_ = true;   // 解析成功
};

} // namespace nextpilot::plotscope::command

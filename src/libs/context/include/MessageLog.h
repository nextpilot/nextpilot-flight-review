#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// 进程级消息流：底部面板条（终端 / 输出 / 问题 / 调试控制台）的唯一数据源。
//
// 为什么需要它：在它之前，脚本 print/app.log 只写进控制台自己的 QPlainTextEdit，
// 状态栏失败提示只走 statusBar，二者互不可见——于是"输出""问题"这类面板即便
// 建出来也是空壳，只能各自去偷别的视图的私有 widget。这里把消息收敛成纯数据
// （字符串 + 级别 + 时间戳），headless 与 GUI 共用、可订阅、可序列化。
//
// 铁律：**纯数据，不含 QWidget**（与 AppContext 同一约束）；实例归 AppContext，
// 不做全局单例，测试之间互不污染。环形缓冲上限见 kMaxMessages。
namespace nextpilot::plotscope::context
{

struct Message
{
    QString level;   // info / warn / error / debug（视图按级别过滤与着色）
    QString source;  // 产生方自报：脚本 / 外壳 / 调试 ...
    QString text;    // 正文（可能多行）
    QString locator; // 可点击定位：脚本为 "name:line"，其它留空
    qint64 ts = 0;   // epoch 毫秒
};

class MessageLog
{
public:
    // 上限 2000 条：足够覆盖一次长跑的脚本输出，又不至于让内存随运行时长无界增长。
    // 溢出时丢最旧的一条（环形缓冲），不做丢弃计数提示——面板另有截断提示。
    static constexpr int kMaxMessages = 2000;

    void append(const Message &message);
    void append(const QString &level, const QString &source, const QString &text,
                const QString &locator = QString());

    const QVector<Message> &all() const { return m_messages; }

    // 按级别过滤（空列表 = 全部）。问题面板取 {"warn","error"}。
    QVector<Message> filtered(const QStringList &levels) const;

    void clear() { m_messages.clear(); }
    int size() const { return m_messages.size(); }

private:
    QVector<Message> m_messages;
};

} // namespace nextpilot::plotscope::context

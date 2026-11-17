#pragma once

#include <QString>

namespace ps::runtime
{

// 日志落盘。只负责追加写文件，不依赖 Widgets、状态栏或 Dock。
// shell 的 LogManager 在其上增加视图、状态栏和错误弹窗。
class LogService
{
public:
    explicit LogService(const QString &path);

    void append(const QString &line) const;
    QString path() const { return m_path; }

private:
    QString m_path;
};

} // namespace ps::runtime

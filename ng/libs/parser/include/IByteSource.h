#pragma once

#include <QByteArray>
#include <QString>

class QFile;

namespace nextpilot::plotscope::parser
{

// 字节来源：**数据源的前端**——字节从哪来。
//
// 架构定位（见 ARCHITECTURE.md §3.3 / AGENTS.md §3 P0-1）：
//   需求原文：「数据解析，一般只能选择一个前端（文件或流）」。
//   所以一个数据源有且只有一个字节来源，解析器只是它的**下游**。
//
// 为什么要有这一层：
//   归并两条数据通路后，"读字节"这件事必须**只做一次**。
//   过去它散在每个格式驱动里（driver 自己 QFile 自己读自己写 Dataset），
//   于是每加一个格式就要写一份读文件的代码，且录制旁路无法覆盖它。
//   把读取抽成接口后：
//     · 文件与流用同一套编排；
//     · 录制旁路只需要一个"tee"，就能在读的同时分流到录制器；
//     · 解析器只管"字节 → 批"，彻底不知道来源。
//
// 约束：
//   - 常驻内存 = 一次 read 的缓冲，与文件大小/流长度**无关**；
//   - 实现放在 core（只依赖 Qt6::Core），CLI 与 GUI 共用；
//   - 具体实现（文件 / UDP / 串口）由扩展或宿主提供。
class IByteSource
{
public:
    virtual ~IByteSource() = default;

    // 读一批字节到 buf。返回实际读取的字节数；0 表示暂时没有数据
    // （流）或已到末尾（文件）。出错返回负数。
    virtual qint64 read(char *buf, qint64 maxBytes) = 0;

    // 是否已到末尾（文件语义）。流一般返回 false。
    virtual bool atEnd() const = 0;

    // 总字节数；不确定（流）返回 0。
    virtual qint64 totalBytes() const = 0;

    // 来源标识（路径 / 端点），用于显示与错误信息。
    virtual QString origin() const = 0;

    // 便捷接口：按缓冲大小分批读。缓冲是**唯一**的常驻内存，与总量无关。
    qint64 readChunk(QByteArray &buffer, qint64 maxBytes = 256 * 1024)
    {
        buffer.resize(static_cast<int>(maxBytes));
        const qint64 n = read(buffer.data(), maxBytes);
        if (n < 0)
            return n;
        buffer.resize(static_cast<int>(n));
        return n;
    }
};

// 文件字节来源：一次只驻留一个缓冲。
class FileByteSource final : public IByteSource
{
public:
    // 打开失败时 valid() 为 false，error() 给出原因。
    explicit FileByteSource(const QString &path);
    ~FileByteSource() override;

    bool valid() const { return m_valid; }
    QString error() const { return m_error; }

    qint64 read(char *buf, qint64 maxBytes) override;
    bool atEnd() const override;
    qint64 totalBytes() const override { return m_total; }
    QString origin() const override { return m_path; }

private:
    QString m_path;
    QString m_error;
    qint64 m_total = 0;
    bool m_valid = false;
        QFile *m_file = nullptr;
};

// 内存字节来源：给测试与"已有一段字节"的场景用（例如录制回放的小片段）。
class MemoryByteSource final : public IByteSource
{
public:
    explicit MemoryByteSource(QByteArray data, QString origin = QString());

    qint64 read(char *buf, qint64 maxBytes) override;
    bool atEnd() const override { return m_pos >= m_data.size(); }
    qint64 totalBytes() const override { return m_data.size(); }
    QString origin() const override { return m_origin; }

private:
    QByteArray m_data;
    QString m_origin;
    qint64 m_pos = 0;
};

} // namespace nextpilot::plotscope::parser
// B7 过渡别名：core 消亡前全仓仍以 core::Xxx 取用。簇 8 删 core 时移除。
namespace nextpilot::plotscope::core
{
using parser::IByteSource;
using parser::FileByteSource;
using parser::MemoryByteSource;
} // namespace nextpilot::plotscope::core


#include "IByteSource.h"

#include <QFile>
#include <QFileInfo>

namespace nextpilot::plotscope::core
{

FileByteSource::FileByteSource(const QString &path)
    : m_path(path)
{
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile())
    {
        m_error = QStringLiteral("文件不存在: %1").arg(path);
        return;
    }
    m_file = new QFile(path);
    if (!m_file->open(QIODevice::ReadOnly))
    {
        m_error = QStringLiteral("无法打开: %1").arg(path);
        delete m_file;
        m_file = nullptr;
        return;
    }
    m_total = fi.size();
    m_valid = true;
}

FileByteSource::~FileByteSource()
{
    delete m_file;
}

qint64 FileByteSource::read(char *buf, qint64 maxBytes)
{
    if (!m_file || maxBytes <= 0)
        return 0;
    const qint64 n = m_file->read(buf, maxBytes);
    return n < 0 ? -1 : n;
}

bool FileByteSource::atEnd() const
{
    return !m_file || m_file->atEnd();
}

MemoryByteSource::MemoryByteSource(QByteArray data, QString origin)
    : m_data(std::move(data)), m_origin(std::move(origin))
{
}

qint64 MemoryByteSource::read(char *buf, qint64 maxBytes)
{
    if (maxBytes <= 0 || m_pos >= m_data.size())
        return 0;
    const qint64 remain = m_data.size() - m_pos;
    const qint64 n = qMin(remain, maxBytes);
    memcpy(buf, m_data.constData() + m_pos, static_cast<size_t>(n));
    m_pos += n;
    return n;
}

} // namespace nextpilot::plotscope::core

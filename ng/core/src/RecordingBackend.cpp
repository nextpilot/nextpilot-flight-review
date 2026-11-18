#include "RecordingBackend.h"

#include <QHash>

namespace ng::core
{

namespace
{
// 函数内 static：避免静态初始化顺序问题。
struct Registry
{
    QHash<QString, RecordingBackend> backends;
    QStringList order;
};

Registry &registry()
{
    static Registry r;
    return r;
}
} // namespace

bool RecordingBackendRegistry::registerBackend(const RecordingBackend &backend)
{
    if (backend.id.isEmpty())
        return false;
    Registry &r = registry();
    if (r.backends.contains(backend.id))
        return false; // 重复 id：先到者为准
    r.backends.insert(backend.id, backend);
    r.order.append(backend.id);
    return true;
}

bool RecordingBackendRegistry::unregisterBackend(const QString &id)
{
    Registry &r = registry();
    if (!r.backends.contains(id))
        return false;
    r.backends.remove(id);
    r.order.removeAll(id);
    return true;
}

const RecordingBackend *RecordingBackendRegistry::backend(const QString &id)
{
    const auto it = registry().backends.constFind(id);
    return it == registry().backends.constEnd() ? nullptr : &it.value();
}

QStringList RecordingBackendRegistry::ids()
{
    return registry().order;
}

int RecordingBackendRegistry::count()
{
    return registry().backends.size();
}

void RecordingBackendRegistry::installBuiltinBackends()
{
    // file：落盘，顺序保证、可回放、可随机定位
    RecordingBackend file;
    file.id = BackendId::File;
    file.displayName = QStringLiteral("本地文件");
    file.description = QStringLiteral("写入本地文件；顺序保证、无丢失、可回放");
    file.streaming = false;
    file.seekable = true;
    file.multiplexed = false;
    registerBackend(file);

    // udp：网络投递，天然多路复用（同一组播地址可承载不同录制流）
    RecordingBackend udp;
    udp.id = BackendId::Udp;
    udp.displayName = QStringLiteral("UDP 组播");
    udp.description = QStringLiteral("经 UDP 组播投递；支持多路复用，可能丢包");
    udp.streaming = true;
    udp.seekable = false;
    udp.multiplexed = true;
    registerBackend(udp);
}

void RecordingBackendRegistry::reset()
{
    Registry &r = registry();
    r.backends.clear();
    r.order.clear();
}

} // namespace ng::core

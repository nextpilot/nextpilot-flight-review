#include "RecordingBackend.h"

namespace nextpilot::plotscope::rec
{

// 存储已收编 StaticHub<RecordingBackend>（机制唯一：Registry 原语 + 惰性表）。
// 本文件只剩真正的领域方法：官方内置清单。

void RecordingBackendHub::installBuiltinBackends()
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

} // namespace nextpilot::plotscope::rec

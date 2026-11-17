#pragma once

#include <QString>
#include <QVector>

// 宿主数据访问契约：datasource/plotting 实现并注册，任何消费者（含脚本）只调用。
// 这里刻意不带 scripting 字样——实现方不需要知道谁在用它。
// 约束（见 roadmap/script-automation.md §3.3）：不放 QWidget*，不传 MainWindow*。

namespace ps::host
{

struct Result
{
    bool ok = true;
    QString code;    // enoent/eformat/etimeout/ecanceled/esrcgone/eneedhost/recurse...
    QString message;

    static Result failure(const QString &code, const QString &message)
    {
        return {false, code, message};
    }
};

// 作业句柄：-1 表示无效。源 id 从 0 起，所以不能用 0 当无效值。
struct JobHandle
{
    int id = -1;
    bool valid() const { return id >= 0; }
};

struct SourceInfo
{
    int id = 0;
    QString name;
    QString alias;
    QString path;
    QString format;
    bool live = false;
    bool paused = false;
};

struct ChannelInfo
{
    QString name;
    QString unit;
    quint64 count = 0;
    double t0 = 0.0;
    double t1 = 0.0;
    double vMin = 0.0;
    double vMax = 0.0;
};

struct LiveStatus
{
    bool connected = false;
    bool recording = false;
    QString recordPath;
    qint64 dropped = 0;
    qint64 samples = 0;
    bool overflow = false;
};

class HostDataService
{
public:
    virtual ~HostDataService() = default;

    // 读
    virtual QVector<SourceInfo> sources() = 0;
    virtual QVector<ChannelInfo> channels(int sourceId) = 0;
    // 按窗抽样，maxPoints <= 0 时由实现取默认（约 2×像素宽）
    virtual Result slice(int sourceId, const QString &channel, double t0, double t1, int maxPoints,
                         QVector<double> *t, QVector<double> *v) = 0;
    virtual LiveStatus liveStatus(int sourceId) = 0;

    // 全量序列的只读视图：返回内核内部指针，不拷贝、不分配。
    // 指针随数据集生命周期失效（关闭/重载/新增通道后不得再用）。
    // 供计算库直接吃内核内存，避免把千万点搬进 Lua 堆。
    struct SeriesView
    {
        const double *time = nullptr;
        const double *value = nullptr;
        quint32 count = 0;
    };
    virtual Result seriesView(int sourceId, const QString &channel, SeriesView *out) = 0;

    // 作业
    virtual JobHandle loadFile(const QString &path, const QString &loaderOptions) = 0;
    virtual Result closeSource(int sourceId) = 0;
    virtual JobHandle reloadSource(int sourceId) = 0;
    virtual JobHandle exportCsv(int sourceId, const QString &path, bool overwrite) = 0;

    // 实时与录制
    virtual JobHandle connectLive(const QString &extensionId, const QString &configJson) = 0;
    virtual Result disconnectLive(int sourceId) = 0;
    virtual Result setPaused(int sourceId, bool paused) = 0;
    virtual Result startRecording(int sourceId, const QString &format, const QString &path) = 0;
    virtual Result stopRecording(int sourceId) = 0;
};

// 全局单例槽：宿主在装配阶段注册，未注册时脚本调用报 eneedhost。
void setHostService(HostDataService *service);
HostDataService *hostService();

} // namespace ps::host

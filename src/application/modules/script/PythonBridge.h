#pragma once

#include "Result.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QVariantMap>
#include <QVector>

class QEventLoop;

namespace nextpilot::plotscope::script
{

using nextpilot::plotscope::foundation::Result;

// ============================================================================
// PythonBridge：Python 子进程桥客户端。
//
// 宿主 <—JSON-line over stdin/stdout—> python -m nextpilot_bridge（常驻）。
// 大数组不走 JSON：宿主写 f64 文件（/dev/shm 旁路）→ 元数据握手 → Python
// np.memmap 映射同一块内存。崩溃/超时由本类兜底：kill 子进程，下次请求
// 自动重启（脚本侧只看到一条错误，主进程永不受牵连）。
//
// 同步等待走 processEvents 分片（与调试器暂停期同模式）——UI 不冻结，
// 但桥调用不可重入（v1 约束，文档记录）。
// ============================================================================
class PythonBridge final : public QObject
{
    Q_OBJECT
public:
    explicit PythonBridge(QObject *parent = nullptr);
    ~PythonBridge() override;

    // 解释器：默认 PATH 上的 python3；PLOTSCOPE_PYTHON 环境变量可覆盖
    // （设置页可后续接 Workspace settings，v1 先读环境变量）。
    void setInterpreter(const QString &py) { m_python = py; }
    QString interpreter() const { return m_python; }

    bool running() const { return m_proc && m_proc->state() == QProcess::Running; }

    // 启动子进程（幂等：已运行直接成功）。失败返回明确错误（找不到解释器等）。
    Result<void> ensureStarted();

    // 主动关闭（析构也会调）。
    void shutdown();

    // eval：求值一个 Python 表达式（globals 保留——import 过的模块跨请求可见）。
    Result<QVariant> eval(const QString &code, int timeoutMs = 30000);

    // exec：执行一段源码（末条表达式语句的值作为返回）。
    // cols: {列名: {path, rows}} —— 宿主预写的 f64 文件，组装成 ds 注入；
    // outDir: 脚本写回列（ds['x']=...）的 .bin 落盘目录。
    // 返回 {value, out(捕获的 stdout), new:{列名:{path,rows}}}。
    Result<QVariantMap> exec(const QString &source, const QVariantMap &cols,
                             const QString &outDir, int timeoutMs = 60000);

    // pull：把全局里的数组落到 f64 文件旁路（宿主随后读回）。
    Result<QVariantMap> pull(const QString &name, const QString &path, int timeoutMs = 30000);

    // put：把宿主 f64 文件注册进 Python 全局（np.memmap 只读视图）。
    Result<QVariantMap> put(const QString &name, const QString &path, int rows,
                            int timeoutMs = 30000);

    // 最近一次子进程 stderr 尾部（诊断/错误信息用）。
    QString lastStderr() const { return m_stderrTail; }

private:
    Result<QVariantMap> request(const QVariantMap &req, int timeoutMs);
    void startLocked();

    QString m_python; // 空则用默认发现逻辑
    QPointer<QProcess> m_proc;
    int m_nextId = 1;
    QMap<int, QVariantMap> m_responses; // id -> 响应（readyRead 解析）
    QByteArray m_readBuf;
    QString m_stderrTail;
};

} // namespace nextpilot::plotscope::script

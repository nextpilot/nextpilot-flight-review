#include "PythonBridge.h"

#include "ErrorCodes.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>

using namespace nextpilot::plotscope::foundation;

namespace nextpilot::plotscope::script
{

namespace
{
// 桥模块落盘目录：qrc 资源解压到临时目录，PYTHONPATH 指过去（python -m 启动）。
QString bridgeModuleDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return base + QStringLiteral("/nextpilot_bridge");
}

bool extractBridgeModule(QString *err)
{
    const QDir dir(bridgeModuleDir());
    if (!dir.exists() && !QDir().mkpath(bridgeModuleDir()))
    {
        if (err)
            *err = QStringLiteral("无法创建桥模块目录 %1").arg(bridgeModuleDir());
        return false;
    }
    const QString target = bridgeModuleDir() + QStringLiteral("/nextpilot_bridge.py");
    QFile src(QStringLiteral(":/script/nextpilot_bridge.py"));
    if (!src.open(QIODevice::ReadOnly))
    {
        if (err)
            *err = QStringLiteral("内嵌桥资源缺失（qrc 未编译进库？）");
        return false;
    }
    QFile dst(target);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        dst.write(src.readAll()) < 0)
    {
        if (err)
            *err = QStringLiteral("桥模块写入失败: %1").arg(target);
        return false;
    }
    return true;
}
} // namespace

PythonBridge::PythonBridge(QObject *parent)
    : QObject(parent)
{
    setObjectName(QStringLiteral("python-bridge"));
    m_python = qEnvironmentVariable("PLOTSCOPE_PYTHON");
    if (m_python.isEmpty())
        m_python = QStringLiteral("python3");
}

PythonBridge::~PythonBridge()
{
    shutdown();
}

void PythonBridge::shutdown()
{
    if (m_proc)
    {
        if (m_proc->state() != QProcess::NotRunning)
        {
            m_proc->kill();
            m_proc->waitForFinished(1000);
        }
        m_proc->deleteLater();
        m_proc.clear();
    }
    m_responses.clear();
    m_readBuf.clear();
}

Result<void> PythonBridge::ensureStarted()
{
    if (running())
        return Result<void>();
    shutdown();

    QString exErr;
    if (!extractBridgeModule(&exErr))
        return fail(err::Runtime, exErr);

    auto *proc = new QProcess(this);
    proc->setProgram(m_python);
    proc->setArguments({QStringLiteral("-m"), QStringLiteral("nextpilot_bridge")});
    proc->setWorkingDirectory(bridgeModuleDir());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONPATH"), bridgeModuleDir());
    // 不让 Python 写 __pycache__（临时目录反复写入无意义）
    env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    QObject::connect(proc, &QProcess::readyReadStandardOutput, proc, [this, proc]() {
        m_readBuf += proc->readAllStandardOutput();
        int nl;
        while ((nl = m_readBuf.indexOf('\n')) >= 0)
        {
            const QByteArray line = m_readBuf.left(nl);
            m_readBuf.remove(0, nl + 1);
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            if (!doc.isObject())
                continue; // 非 JSON 行（Python 警告）丢弃——stdout 是协议专属通道
            const QVariantMap resp = doc.object().toVariantMap();
            bool ok = false;
            const int id = resp.value(QStringLiteral("id")).toInt(&ok);
            if (ok)
                m_responses.insert(id, resp);
        }
    });
    QObject::connect(proc, &QProcess::readyReadStandardError, proc, [this, proc]() {
        m_stderrTail += proc->readAllStandardError();
        const qsizetype max = 4096;
        if (m_stderrTail.size() > max)
            m_stderrTail = m_stderrTail.right(max);
    });

    m_proc = proc;
    proc->start();
    // 启动握手：等子进程活起来（解释器冷启动 30-300ms 量级）
    QElapsedTimer t;
    t.start();
    while (proc->state() == QProcess::Starting && t.elapsed() < 10000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (proc->state() != QProcess::Running)
    {
        const QString msg = QStringLiteral(
                                "Python 桥启动失败（解释器=%1）：找不到 python3 或启动报错；"
                                "可设 PLOTSCOPE_PYTHON 指定解释器。stderr: %2")
                                .arg(m_python, m_stderrTail.trimmed());
        shutdown();
        return fail(err::Runtime, msg);
    }
    return Result<void>();
}

Result<QVariantMap> PythonBridge::request(const QVariantMap &req, int timeoutMs)
{
    if (!running())
    {
        const Result<void> s = ensureStarted();
        if (!s)
            return fail(s.error().code, s.error().message);
    }
    QVariantMap r = req;
    const int id = m_nextId++;
    r.insert(QStringLiteral("id"), id);
    const QJsonDocument doc(QJsonObject::fromVariantMap(r));
    m_proc->write(doc.toJson(QJsonDocument::Compact) + '\n');
    if (!m_proc->waitForBytesWritten(2000))
        return fail(err::Runtime, QStringLiteral("Python 桥写入失败（管道断开）"));

    QElapsedTimer t;
    t.start();
    while (!m_responses.contains(id))
    {
        if (!m_proc || m_proc->state() != QProcess::Running)
        {
            // 子进程崩了/被杀——诊断信息带上 stderr 尾部
            shutdown();
            return fail(err::Runtime,
                        QStringLiteral("Python 桥子进程退出（崩溃或被超时杀死）。stderr: %1")
                            .arg(m_stderrTail.trimmed().right(500)));
        }
        if (t.elapsed() > timeoutMs)
        {
            // 超时强杀 = 强制中断（下次请求自动重启新进程）
            shutdown();
            return fail(err::Runtime,
                        QStringLiteral("Python 执行超时（%1 ms），子进程已终止").arg(timeoutMs));
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
    }

    const QVariantMap resp = m_responses.take(id);
    if (!resp.value(QStringLiteral("ok")).toBool())
        return fail(err::Runtime, resp.value(QStringLiteral("error")).toString());
    return Result<QVariantMap>(resp);
}

Result<QVariant> PythonBridge::eval(const QString &code, int timeoutMs)
{
    QVariantMap req;
    req.insert(QStringLiteral("op"), QStringLiteral("eval"));
    req.insert(QStringLiteral("code"), code);
    const Result<QVariantMap> r = request(req, timeoutMs);
    if (!r)
        return fail(r.error().code, r.error().message);
    return Result<QVariant>(r.value().value(QStringLiteral("value")));
}

Result<QVariantMap> PythonBridge::exec(const QString &source, const QVariantMap &cols,
                                       const QString &outDir, int timeoutMs)
{
    QVariantMap req;
    req.insert(QStringLiteral("op"), QStringLiteral("exec"));
    req.insert(QStringLiteral("source"), source);
    if (!cols.isEmpty())
        req.insert(QStringLiteral("cols"), cols);
    if (!outDir.isEmpty())
        req.insert(QStringLiteral("out_dir"), outDir);
    const Result<QVariantMap> r = request(req, timeoutMs);
    if (!r)
        return fail(r.error().code, r.error().message);
    return Result<QVariantMap>(r.value());
}

Result<QVariantMap> PythonBridge::pull(const QString &name, const QString &path, int timeoutMs)
{
    QVariantMap req;
    req.insert(QStringLiteral("op"), QStringLiteral("pull"));
    req.insert(QStringLiteral("name"), name);
    req.insert(QStringLiteral("path"), path);
    const Result<QVariantMap> r = request(req, timeoutMs);
    if (!r)
        return fail(r.error().code, r.error().message);
    return Result<QVariantMap>(r.value());
}

Result<QVariantMap> PythonBridge::put(const QString &name, const QString &path, int rows,
                                      int timeoutMs)
{
    QVariantMap req;
    req.insert(QStringLiteral("op"), QStringLiteral("put"));
    req.insert(QStringLiteral("name"), name);
    req.insert(QStringLiteral("path"), path);
    req.insert(QStringLiteral("rows"), rows);
    const Result<QVariantMap> r = request(req, timeoutMs);
    if (!r)
        return fail(r.error().code, r.error().message);
    return Result<QVariantMap>(r.value());
}

} // namespace nextpilot::plotscope::script

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace nextpilot::plotscope::host
{

// 协议模板库：.psproto 文本的唯一读写入口（headless 与 GUI 共用同一份目录约定）。
//
// 为什么放在模块层而不是 gui：
//   目录约定（"用户配置目录/protocols"）是业务事实，不是界面事实。GUI 的
//   新建/打开/保存、脚本的批量校验、CLI 的模板列举如果要各写一份路径拼接，
//   迟早出现"GUI 存到 A、脚本读 B"的分裂。这里定一次，两侧都问它。
//
// 边界：只做文件层（列举/读/写/校验前置），不认识 Qt Widgets、不认识对话框。
class ProtocolTemplateStore
{
public:
    // 模板库目录（不存在时按需创建）。AppConfigLocation / protocols。
    static QString directory();

    // 模板文件后缀常量，避免各处硬编码字符串。
    static QString suffix();

    // 列举模板库。返回项：name（不含后缀）/ fileName / path / sizeBytes / modified。
    static QVector<QJsonObject> list(QString *err = nullptr);

    // 读模板文本。拒绝：不存在、超过上限（防误把数据文件当模板读）。
    // requireSuffix=false 供编辑器打开 .json 草稿；模板库自身调用保持 true。
    static bool read(const QString &path, QByteArray *out, QString *err, bool requireSuffix = true);

    // 校验 + 原子写到指定路径（编辑器"另存为任意位置"用）。
    // 与 save 共用同一份"先 validate 再落盘"的实现，避免 GUI 自己写一份裸 QFile::write。
    static bool saveToPath(const QString &path, const QByteArray &content, QString *err);

    // 写模板。落盘前先过 validate（写进去的模板必须是能编译的），
    // 再用 QSaveFile 原子替换，避免半截文件被解析器读到。
    static bool save(const QString &name, const QByteArray &content, QString *savedPath,
                     QString *err);

    // 模板名合法性：非空、无路径分隔符、不含 ..、长度受限。
    static bool validName(const QString &name, QString *err);

    // 读入大小上限（模板是配置，不是数据）。
    static constexpr int kMaxTemplateBytes = 8 * 1024 * 1024;
};

} // namespace nextpilot::plotscope::host

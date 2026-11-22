#pragma once

#include <QJsonObject>
#include <QString>

namespace nextpilot::plotscope::ext
{

// CSV 导入探测：读文件头若干行，推断分隔符/表头/列类型/建议时间列/警告。
//
// 职责：这是**格式知识**，供 CsvFormatParser::probeFile 调用，
// 让 GUI 通用对话框预填字段、渲染预览。只切头部有限行，不物化全量。
//
// 算法平移自老扩展 psCsvProbeFile（src/extensions/.../CsvParser.cpp），
// 但用 Qt 容器 + QJsonObject 重写，不依赖老 C ABI vtable。
namespace CsvImportProbe
{
    // 返回 {delimiter, ambiguous, has_header, columns:[{name,kind}],
    //        preview:[[cell,...]], suggested_time, warnings:[...]}。
    // 失败返回 {"error": "..."}。
    QJsonObject probe(const QString &path);

    // 依据探测结果生成默认选项 JSON：
    //   {delimiter, has_header, skip_rows:0, time:{mode,index?}}
    QByteArray defaultOptions(const QJsonObject &probeResult);
} // namespace CsvImportProbe

} // namespace nextpilot::plotscope::ext

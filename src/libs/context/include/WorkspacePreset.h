#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace nextpilot::plotscope::context
{

// .psw 预设：一份按名字存盘的工作现场 = 源清单 + 画布布局。
//
// 与 SettingsStore 的分工：Store 存用户偏好（跨会话唯一一份），预设存多份
// 可互相比对的工作现场（同一份数据可以存"总览""局部""诊断"三套布局）。
// 因此预设自带文件名与元信息，不挤进 settings 的单键。
//
// 源绑定的两档语义：
//   bound   —— 源条目全量（含 parserParams 原文），打开时按原文重建解析配置；
//   generic —— 只留定位字段，换一台没装同名解析器的机器也能打开，曲线按
//              通道表名匹配现载数据集，匹配不上留空态。
struct WorkspacePreset
{
    static constexpr int kSchemaVersion = 1;

    int version = kSchemaVersion;
    QString kind = QStringLiteral("preset");
    QString name;
    QString savedAt;                              // ISO8601
    QString sourceBinding = QStringLiteral("bound"); // bound / generic
    QJsonArray sources;                           // SourceSpec.toJson() 数组
    QJsonObject plots;                            // PlotDocumentSpec.toJson()

    QJsonObject toJson() const;
    // 解析并校验。version 高于本端支持时失败，并通过 error 说明会丢什么——
    // 静默降级会让用户以为存下的布局完好，实际少了一半。
    static WorkspacePreset fromJson(const QJsonObject &obj, QString *error = nullptr);

    // 转 generic：清空 parserParams，只留 origin/kind/formatHint/parserIds/topicFilter。
    // 恢复时只登记源条目、不自动导入，避免在没有对应解析器的机器上直接失败。
    void makeGeneric();

    // 单条源条目转 generic（供 makeGeneric 与逐条导出复用）。
    static QJsonObject genericSourceEntry(const QJsonObject &source);
};

} // namespace nextpilot::plotscope::context

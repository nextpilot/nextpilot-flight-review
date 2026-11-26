#pragma once

// A8b-M3-a 第 2 笔（registry 层，对位 ContributedView）：编辑器打开目标的纯数据引用（IKXXJ2 正文 §IEditorProvider）。
// 无业务类型：壳据此回答"中央区页签打开的是哪个资源"，不解释 uri 语义。

#include <QString>

namespace nextpilot::plotscope::registry
{

struct ResourceRef
{
    QString uri;   // 资源标识（文件路径 / 自定义 scheme），语义归 owner
    QString mime;  // 如 "text/plain"；与 customEditors.selector.mime 对位
    QString title; // 页签显示名
};

inline bool operator==(const ResourceRef &a, const ResourceRef &b)
{
    return a.uri == b.uri && a.mime == b.mime && a.title == b.title;
}

} // namespace nextpilot::plotscope::registry

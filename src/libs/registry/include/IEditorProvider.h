#pragma once

// A8b-M3-a 第 2 笔（registry 层，对位 IActionProvider）：编辑器提供者契约（vscode.CustomEditorProvider 对位，IKXXJ2 正文）。
// 壳持有表（ViewRegistry 运行层），扩展经 registerEditorProvider 注册；
// 壳只拿不透明控件，不解释业务。
//
// 四语义（正文 §3.0.5）：isDirty / setDirtyCallback / saveAs / readonly——
// 壳据此管脏标记、另存、只读页签；热退出与备份显式不做。

#include "ResourceRef.h"

#include <functional>

class QWidget;

namespace nextpilot::plotscope::registry
{

class IEditorProvider
{
public:
    virtual ~IEditorProvider() = default;

    // 能否打开该资源（selector 声明匹配之外的运行层裁决，扩展自定）。
    virtual bool canOpen(const ResourceRef &ref) const = 0;
    // 创建编辑器控件；壳只拿不透明 QWidget*（只对内置扩展有效，外置扩展归 A5b）。
    virtual QWidget *createEditor(const ResourceRef &ref, QWidget *parent) = 0;
    // 保存；返回 false 表示失败（壳提示，不吞）。
    virtual bool save(const ResourceRef &ref) { return true; }

    // ---- 四语义 ----
    // 脏标记查询：壳渲染页签脏角标。
    virtual bool isDirty() const { return false; }
    // 注册脏态回调：编辑器内容变化时以 dirty 布尔回调壳（供页签刷新）。
    virtual void setDirtyCallback(std::function<void(bool)> cb) { Q_UNUSED(cb); }
    // 另存为：ref 为壳给出的新目标；返回 false 表示失败或该编辑器不支持。
    virtual bool saveAs(const ResourceRef &ref) { return true; }
    // 只读：壳据此渲染只读页签、禁保存。
    virtual bool readonly() const { return false; }
};

} // namespace nextpilot::plotscope::registry

#pragma once

// A8b-M2 裁定 52441633 §三：壳弹"按格式打开"文件对话框需要的格式提示
// （显示名/过滤串）经服务接口读回，壳不直连 IMessageParser（§9 e) grep → 0）。
// 实现放 assembly，内部从唯一扩展注册表的格式驱动拼装；无驱动时返回空串，
// 壳回退"全部 (*)"与格式 id 原文。

#include <QString>

namespace nextpilot::plotscope::workbench
{

class IFormatHints
{
public:
    virtual ~IFormatHints() = default;

    // 该格式的文件对话框过滤串："显示名 (*.a *.b);;全部 (*)"；无驱动返回空串。
    virtual QString fileFilter(const QString &formatId) const = 0;

    // 该格式的展示名；无驱动返回空串（壳回退用 formatId 原文）。
    virtual QString displayName(const QString &formatId) const = 0;
};

// 纯接口（无 QObject 基类）：registerSingleton 存接口地址，getService static_cast 恒安全。
} // namespace nextpilot::plotscope::workbench

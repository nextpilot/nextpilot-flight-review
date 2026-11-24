#pragma once

// 产品身份描述（契约 §19 / §20：一份 spec 定义一个产品）。
//
// 为什么壳不能认产品名：
//   壳（ps_gui）是可复用骨架，同一套壳要能起完整产品与裸壳第二产品。
//   凡是"这个产品叫什么、默认摆哪些面板、装哪些模块"的知识，全部收进本结构，
//   由组合根（exe / 装配层）填好后注入壳；壳代码里**不允许出现产品常量分支**
// （没有 `if (product == "bare")` 这类判断）。
//
// 本文件只放纯数据（值语义 + Region 枚举），无 QWidget、无运行态，headless 可用。

#include "Region.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

// 默认布局里的一条面板占位：哪个逻辑区域放哪个面板工厂类型。
//   · panelType 为空：该区只放空态占位，不请求任何面板；
//   · 工厂未注册（裸壳无模块面板）时 requestPanel 返回 nullptr，壳安全跳过，不算错误；
//   · raiseAfter：装配后把该面板置前（如完整产品默认停在终端页签）。
struct ProductPanelPlacement
{
    Region region = Region::Editor;
    QString panelType;
    QString title;        // 可选标题覆盖；空 = 取面板自注册标题
    bool raiseAfter = false;
};

// 一份产品的完整身份：壳只从这里取身份文案、默认布局与模块清单。
struct ProductSpec
{
    // 产品稳定标识（小写 id，如 "plotscope" / "bare"）；默认文件名等派生值用它。
    QString id;
    // 显示名：窗口标题 / 关于页主名。
    QString displayName;
    // 版本号（空 = 不显示版本）。
    QString version;
    // 报告默认标题（空 = 回落 displayName）。
    QString reportTitle;
    // 默认 Region 布局：壳按此清单依次请求面板；空清单 = 全空态占位。
    QList<ProductPanelPlacement> defaultPanels;
    // 模块清单（mod.* id）：装配层据此装载模块；壳不遍历、不解析。
    QStringList moduleIds;
};

// 完整产品（PlotScope）的身份清单——装配层与测试共用的唯一事实源。
// 放在 libs 层是为了让测试也能拿到"真实产品布局"，避免测试自己复制一份常量。
ProductSpec plotScopeProductSpec();

} // namespace nextpilot::plotscope::registry

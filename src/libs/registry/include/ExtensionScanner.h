#pragma once

// A8b-M1 内置扩展扫描：扫 <root>/<id>/manifest.json，只读 contributes，不执行代码。
//
// 物理形态（IKXXJ2 §3.0.11）：一个扩展 = 一个目录 + 一份 manifest.json
//   { "id": "plotscope.hello", "version": "0.1.0", "main": "main.so",
//     "activationEvents": ["onCommand:hello.say"], "contributes": { ... } }
// - id 全局唯一；同 id 后加载覆盖先加载并记诊断。
// - 只登记 contributes（parseContributes），首个 activationEvent 命中才 dlopen（A5b 的事）。
// - 纯 headless：只依赖 Qt Core + registry，不链 GUI / toolbox / 命令库。

#include "ContributionParser.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace nextpilot::plotscope::registry
{

struct ExtensionScanReport
{
    QList<ContributesDoc> docs; // 成功解析的贡献（含 owner=id）
    QList<ParseIssue> issues;   // 解析/IO 诊断（keyPath = 目录或字段路径）
    QStringList scannedDirs;    // 实际扫过的扩展目录（诊断用）
};

// 扫描 root 下每个含 manifest.json 的子目录。返回全部解析结果，不 mount、
// 不做 owner 去重（去重/冲突由 ToolboxHub::mountContribution 负责，职责单一）。
// 单个扩展失败（缺 manifest / JSON 坏 / contributes 非法）只记 issue、不影响其余。
ExtensionScanReport scanExtensions(const QString &root);

} // namespace nextpilot::plotscope::registry

#pragma once

#include "Dataset.h"

#include <QString>

namespace nextpilot::plotscope::ext::ulog
{

// 流式 ULog 解析器。
//
// 协议解析逻辑（格式表 / 订阅展平 / D 段字段解码 / 参数 / 事件 / dropout）
// 复用自旧 extension: src/extensions/builtin/autopilot/datasources/ulog/UlogParser.cpp，
// 格式知识逐字保留，未重新发明。
//
// 唯一改造是「产出端」：旧解析器把 D 段所有消息攒进 std::vector<double>（全量物化），
// 再由 psBuildDataset 一次性建表；这里把 D 段按 topic 分成固定大小的批，满批即
// Dataset::appendFrame() 流式灌入 Rust kernel，C++ 侧常驻内存 = 单条消息 + 一批，
// 与文件行数无关，满足百万级数据的零副本铁律。
//
// 参数：
//   path  - .ulg 文件路径
//   ds    - 调用方（扩展 Handler）已创建好的空 Dataset，本函数负责填充
//   err   - 出错信息
// 返回 false 表示失败；成功时 ds 内含若干 typed 表（每个订阅 topic 一张）+ 元数据表。
bool parseUlogStream(const QString &path, nextpilot::plotscope::core::Dataset &ds, QString &err);

} // namespace nextpilot::plotscope::ext::ulog

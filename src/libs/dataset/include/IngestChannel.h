#pragma once

// 摄取通道数据结构：IIngest::channels() 的返回元素。
// 从 context::IngestPipeline 内嵌类型上提到 dataset，使 IIngest 的签名只出现 libs
// 类型、且 libs/workbench 不反向依赖 libs/context。

#include <QString>

// PsParserVtable 来自 parser 的纯 C ABI 头（前向声明即可，本结构只存指针）。
struct PsParserVtable;

namespace nextpilot::plotscope::dataset {

struct IngestChannel
{
    QString parserId;
    QString topicFilter; // 可选通道过滤（空 = 全部）
    const PsParserVtable *vtable = nullptr;
    void *ctx = nullptr; // 解析器实例（opaque）
    int datasetId = -1;  // 该通道产出落到的数据集
    bool active = true;
    QString error; // 最近一次错误
};

} // namespace nextpilot::plotscope::dataset

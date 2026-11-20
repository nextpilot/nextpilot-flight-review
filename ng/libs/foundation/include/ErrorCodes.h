#pragma once

#include <QLatin1String>

namespace nextpilot::plotscope::foundation::err
{

// 错误码唯一清单（uORB 式资产：提前备好、列成目录、用到直接取）。
//
// 纪律：
//   1. fail()/Error 的 code 一律取本表常量，禁止手写字符串字面量——
//      清单即文档：这里看不到的码就不该存在；新码先登记本表再使用。
//   2. 码形如 "e<词>"（结构化错误码，脚本/CLI/MCP 按码分支），常量名
//      = 去 e 前缀的驼峰（enoent → err::NoEnt）。
//   3. constexpr QLatin1String：零开销，隐式转 QString 传给 fail()。

// === 通用 ===
inline constexpr QLatin1String Unknown{"eunknown"};   // 未分类错误（兜底）
inline constexpr QLatin1String NoImpl{"enoimpl"};     // 能力未实现
inline constexpr QLatin1String Unsupported{"eunsupported"};   // 能力不支持（如对文件源 pause）
inline constexpr QLatin1String NotSupported{"enotsupported"}; // 同上（历史码，勿新增）
inline constexpr QLatin1String Args{"eargs"};         // 参数缺失/非法
inline constexpr QLatin1String State{"estate"};       // 状态机不允许
inline constexpr QLatin1String Runtime{"eruntime"};   // 运行时错误
inline constexpr QLatin1String NoMem{"enomem"};       // 内存不足
inline constexpr QLatin1String Abi{"eabi"};           // ABI 校验失败

// === 资源定位 ===
inline constexpr QLatin1String NoEnt{"enoent"};       // 对象不存在（数据集/表/通道/文件…）
inline constexpr QLatin1String NoSource{"enosource"}; // 无活动数据源
inline constexpr QLatin1String NoData{"enodata"};     // 无数据
inline constexpr QLatin1String Empty{"eempty"};       // 空样本/空结果
inline constexpr QLatin1String Path{"epath"};         // 路径非法
inline constexpr QLatin1String Page{"epage"};         // 分页越界
inline constexpr QLatin1String Range{"erange"};       // 数值/索引越界
inline constexpr QLatin1String Cap{"ecap"};           // 超出容量上限
inline constexpr QLatin1String Nan{"enan"};           // 非数值（NaN/Inf）

// === 数据与通道 ===
inline constexpr QLatin1String Dataset{"edataset"};   // 数据集定位失败/未指定
inline constexpr QLatin1String Table{"etable"};       // 表定位失败
inline constexpr QLatin1String Column{"ecolumn"};     // 列定位失败
inline constexpr QLatin1String Chan{"echan"};         // 通道定位失败/通道参数错
inline constexpr QLatin1String Data{"edata"};         // 数据内容非法
inline constexpr QLatin1String Derive{"ederive"};     // 派生序列错误
inline constexpr QLatin1String Transform{"etransform"}; // 通道变换配方错误
inline constexpr QLatin1String Filter{"efilter"};     // 过滤器错误
inline constexpr QLatin1String NoFilter{"enofilter"}; // 过滤器不存在
inline constexpr QLatin1String NoFilterOps{"enofilterops"}; // 过滤器器官缺失
inline constexpr QLatin1String NoMarkerOps{"enomarkerops"}; // 标记器官缺失
inline constexpr QLatin1String Time{"etime"};         // 时间轴错误
inline constexpr QLatin1String Shape{"eshape"};       // 数组形状不匹配
inline constexpr QLatin1String Type{"etype"};         // 类型不匹配

// === IO 与持久化 ===
inline constexpr QLatin1String Io{"eio"};             // 读写失败
inline constexpr QLatin1String Read{"eread"};         // 读失败
inline constexpr QLatin1String Write{"ewrite"};       // 写失败
inline constexpr QLatin1String Format{"eformat"};     // 格式错误
inline constexpr QLatin1String Syntax{"esyntax"};     // 语法错误（脚本/表达式/JSON）
inline constexpr QLatin1String Grid{"egrid"};         // 布局网格错误

// === 意图与执行 ===
inline constexpr QLatin1String Intent{"eintent"};     // 意图未注册/不可路由
inline constexpr QLatin1String NoView{"enoview"};     // 视图分发器缺失/无视图
inline constexpr QLatin1String NoEval{"enoeval"};     // 无求值器
inline constexpr QLatin1String Bootstrap{"ebootstrap"}; // 引导失败

// === 脚本 ===
inline constexpr QLatin1String Lua{"elua"};           // Lua 执行错误
inline constexpr QLatin1String NoLua{"enolua"};       // Lua 环境缺失
inline constexpr QLatin1String NoScript{"enoscript"}; // 脚本不存在
inline constexpr QLatin1String NoDesc{"enodesc"};     // 描述缺失
inline constexpr QLatin1String Describe{"edescribe"}; // 脚本自描述缺失

// === 标识与命名 ===
inline constexpr QLatin1String DupId{"edupid"};       // id 重复
inline constexpr QLatin1String BadName{"ebadname"};   // 名称非法
inline constexpr QLatin1String Id{"eid"};             // id 非法/缺失
inline constexpr QLatin1String Name{"ename"};         // 名称缺失
inline constexpr QLatin1String Kind{"ekind"};         // kind 非法
inline constexpr QLatin1String Param{"eparam"};       // 参数对象非法
inline constexpr QLatin1String Opt{"eopt"};           // 选项非法
inline constexpr QLatin1String Key{"ekey"};           // 设置键缺失/非法

// === 协议与底层 ===
inline constexpr QLatin1String Protocol{"eprotocol"}; // 协议错误
inline constexpr QLatin1String NoParser{"enoparser"}; // 无匹配解析器
inline constexpr QLatin1String NoTool{"enotool"};     // 工具不存在
inline constexpr QLatin1String NoClass{"enoclass"};   // 类不存在
inline constexpr QLatin1String NoCreate{"enocreate"}; // 无法创建
inline constexpr QLatin1String Create{"ecreate"};     // 创建失败
inline constexpr QLatin1String Output{"eoutput"};     // 输出错误
inline constexpr QLatin1String Rule{"erule"};         // 规则错误
inline constexpr QLatin1String Rust{"erust"};         // Rust 后端返回错误

} // namespace nextpilot::plotscope::foundation::err

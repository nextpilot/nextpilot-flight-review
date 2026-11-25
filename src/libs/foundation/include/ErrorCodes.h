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
//   3. inline const QLatin1String：隐式转 QString 传给 fail()。
//      不用 constexpr：Qt 6.2 的 QLatin1String(const char*) 构造在 GCC 上依赖
//      __builtin_strlen 可做编译期求值，但 MSVC 的 strlen 非 constexpr，
//      会导致 C2131（Windows CI 实测）。这些码都在运行期使用，改 const 无性能影响。

// === 通用 ===
inline const QLatin1String Unknown{"eunknown"};   // 未分类错误（兜底）
inline const QLatin1String NoImpl{"enoimpl"};     // 能力未实现
inline const QLatin1String Unsupported{"eunsupported"};   // 能力不支持（如对文件源 pause）
inline const QLatin1String NotSupported{"enotsupported"}; // 同上（勿新增）
inline const QLatin1String Args{"eargs"};         // 参数缺失/非法
inline const QLatin1String State{"estate"};       // 状态机不允许
inline const QLatin1String Runtime{"eruntime"};   // 运行时错误
inline const QLatin1String NoMem{"enomem"};       // 内存不足
inline const QLatin1String Abi{"eabi"};           // ABI 校验失败

// === 资源定位 ===
inline const QLatin1String NoEnt{"enoent"};       // 对象不存在（数据集/表/通道/文件…）
inline const QLatin1String NoSource{"enosource"}; // 无活动数据源
inline const QLatin1String NoData{"enodata"};     // 无数据
inline const QLatin1String Empty{"eempty"};       // 空样本/空结果
inline const QLatin1String Path{"epath"};         // 路径非法
inline const QLatin1String Page{"epage"};         // 分页越界
inline const QLatin1String Range{"erange"};       // 数值/索引越界
inline const QLatin1String Cap{"ecap"};           // 超出容量上限
inline const QLatin1String Nan{"enan"};           // 非数值（NaN/Inf）

// === 数据与通道 ===
inline const QLatin1String Dataset{"edataset"};   // 数据集定位失败/未指定
inline const QLatin1String Table{"etable"};       // 表定位失败
inline const QLatin1String Column{"ecolumn"};     // 列定位失败
inline const QLatin1String Chan{"echan"};         // 通道定位失败/通道参数错
inline const QLatin1String Data{"edata"};         // 数据内容非法
inline const QLatin1String Derive{"ederive"};     // 派生序列错误
inline const QLatin1String Transform{"etransform"}; // 通道变换配方错误
inline const QLatin1String Filter{"efilter"};     // 过滤器错误
inline const QLatin1String NoFilter{"enofilter"}; // 过滤器不存在
inline const QLatin1String NoFilterOps{"enofilterops"}; // 过滤器模块缺失
inline const QLatin1String NoMarkerOps{"enomarkerops"}; // 标记模块缺失
inline const QLatin1String Time{"etime"};         // 时间轴错误
inline const QLatin1String Shape{"eshape"};       // 数组形状不匹配
inline const QLatin1String Type{"etype"};         // 类型不匹配

// === IO 与持久化 ===
inline const QLatin1String Io{"eio"};             // 读写失败
inline const QLatin1String Read{"eread"};         // 读失败
inline const QLatin1String Write{"ewrite"};       // 写失败
inline const QLatin1String Format{"eformat"};     // 格式错误
inline const QLatin1String Syntax{"esyntax"};     // 语法错误（脚本/表达式/JSON）
inline const QLatin1String Grid{"egrid"};         // 布局网格错误

// === 意图与执行 ===
inline const QLatin1String Intent{"eintent"};     // 意图未注册/不可路由
inline const QLatin1String NoView{"enoview"};     // 视图分发器缺失/无视图
inline const QLatin1String NoEval{"enoeval"};     // 无求值器
inline const QLatin1String Bootstrap{"ebootstrap"}; // 引导失败

// === 脚本 ===
inline const QLatin1String Lua{"elua"};           // Lua 执行错误
inline const QLatin1String NoLua{"enolua"};       // Lua 环境缺失
inline const QLatin1String NoScript{"enoscript"}; // 脚本不存在
inline const QLatin1String NoDesc{"enodesc"};     // 描述缺失
inline const QLatin1String Describe{"edescribe"}; // 脚本自描述缺失

// === 标识与命名 ===
inline const QLatin1String DupId{"edupid"};       // id 重复
inline const QLatin1String BadName{"ebadname"};   // 名称非法
inline const QLatin1String Id{"eid"};             // id 非法/缺失
inline const QLatin1String Name{"ename"};         // 名称缺失
inline const QLatin1String Kind{"ekind"};         // kind 非法
inline const QLatin1String Param{"eparam"};       // 参数对象非法
inline const QLatin1String Opt{"eopt"};           // 选项非法
inline const QLatin1String Key{"ekey"};           // 设置键缺失/非法

// === 协议与底层 ===
inline const QLatin1String Protocol{"eprotocol"}; // 协议错误
inline const QLatin1String NoParser{"enoparser"}; // 无匹配解析器
inline const QLatin1String NoTool{"enotool"};     // 工具不存在
inline const QLatin1String NoClass{"enoclass"};   // 类不存在
inline const QLatin1String NoCreate{"enocreate"}; // 无法创建
inline const QLatin1String Create{"ecreate"};     // 创建失败
inline const QLatin1String Output{"eoutput"};     // 输出错误
inline const QLatin1String Rule{"erule"};         // 规则错误
inline const QLatin1String Rust{"erust"};         // Rust 后端返回错误

} // namespace nextpilot::plotscope::foundation::err

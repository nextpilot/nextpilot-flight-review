#pragma once

#include "ps_parser_spi.h"

/*
 * ROS2 bag（.db3 / SQLite3）解析器（SPI 视图）。
 *
 * 格式知识来自 main 的 Rosbag2DataLoader.cpp：
 *   SQLite 表 topics(id,name,type) + messages(topic_id,timestamp,data)；
 *   消息体是 CDR（ROS2 序列化），按消息类型解码成字段。
 * 这里只把出口改掉：main 把全部 topic 拍平成曲线（ros.topic.字段），
 * ng 按 **topic 出一张表**（表名 = topic 去掉前导 `/`、`/`→`_`，列 = 字段）。
 *
 * 与 MDF 一样是随机访问格式：streaming=0，feed() 只累积字节，
 * finish() 一次性解析。SQLite 不落临时文件——优先 sqlite3_deserialize
 * 把内存字节当只读库；该构建不支持时回退写临时文件再打开。
 */
namespace nextpilot::plotscope::ext::rosbag2
{

const PsParserInfo &parserInfo();
const PsParserVtable *parserVtable();

// PS_REGISTER_PARSER 用的静态自注册类。
class Rosbag2ParserSpi
{
public:
    static const PsParserInfo &parserInfo();
    static const PsParserVtable *parserVtable();
};

} // namespace nextpilot::plotscope::ext::rosbag2

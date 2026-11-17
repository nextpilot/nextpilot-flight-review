#pragma once

#include "ps_extension_api.h"
#include "ps_kernel.h"

#include <QByteArray>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/* 扩展侧解析结果的通用数据结构：解析器只管产出通道，不碰宿主 UI。 */
struct PsChannelData
{
    std::string name;
    std::string unit;
    std::string table;       /* 所属 topic 表；空则走旧单通道接口 */
    std::shared_ptr<const std::vector<double>> axis; /* 同表共享的时间轴 */
    std::vector<double> t;   /* 旧接口兼容；设置了 axis 时不再使用 */
    std::vector<double> v;
    uint32_t value_type = PS_VALUE_F64; /* PsValueType；v 会按该类型写入 Dataset */
};

/* 把通道列表交给内核通用 dataset API，扩展不直接构造 PsDataset。 */
PsDataset *psBuildDataset(std::vector<PsChannelData> &channels, std::string &err,
                          const PsHostApi *host);

/* 解析进度：load 入口先绑定 host，解析器在循环里按 done/total 上报。
 * 宿主未挂进度接收器时为空操作；同一百分比只发一次。 */
void psLoaderSetHost(const PsHostApi *host);
void psLoaderProgress(uint64_t done, uint64_t total);

void psWriteErr(char *err, uint32_t cap, const std::string &msg);
int32_t psWriteJson(const QByteArray &json, char *out, uint32_t cap);
int32_t psWriteText(const std::string &text, char *out, uint32_t cap);

bool psReadFile(const char *path, std::vector<uint8_t> &buf, std::string &err);
bool psReadTextFile(const char *path, std::string &text, std::string &err);

/* 表头单位：`acc_x [m/s^2]` / `battery_v(V)` → 括号内容。 */
std::string psUnitFromHeader(const std::string &header);

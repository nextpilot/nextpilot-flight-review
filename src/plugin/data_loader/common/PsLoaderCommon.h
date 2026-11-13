#pragma once

#include "ps_plugin_api.h"
#include "ps_kernel.h"

#include <QByteArray>

#include <cstdint>
#include <string>
#include <vector>

/* 插件侧解析结果的通用数据结构：解析器只管产出通道，不碰宿主 UI。 */
struct PsChannelData
{
    std::string name;
    std::string unit;
    std::vector<double> t;
    std::vector<double> v;
};

/* 把通道列表交给内核通用 dataset API，插件不直接构造 PsDataset。 */
PsDataset *psBuildDataset(const std::vector<PsChannelData> &channels, std::string &err,
                          const PsHostApi *host);

void psWriteErr(char *err, uint32_t cap, const std::string &msg);
int32_t psWriteJson(const QByteArray &json, char *out, uint32_t cap);
int32_t psWriteText(const std::string &text, char *out, uint32_t cap);

bool psReadFile(const char *path, std::vector<uint8_t> &buf, std::string &err);
bool psReadTextFile(const char *path, std::string &text, std::string &err);

/* 表头单位：`acc_x [m/s^2]` / `battery_v(V)` → 括号内容。 */
std::string psUnitFromHeader(const std::string &header);

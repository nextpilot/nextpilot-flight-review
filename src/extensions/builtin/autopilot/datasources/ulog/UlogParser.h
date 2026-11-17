#pragma once

#include "PsLoaderCommon.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// ULog L/C 段：运行期文本事件，每条都有时间、级别；C 段额外带 tag。
struct UlogEvent
{
    double t = 0.0;
    uint8_t level = 0;
    uint16_t tag = 0;
    bool tagged = false;
    std::string text;
};

// ULog 解析结果。字段按“原始 / 衍生”分开：
// - 直接来自文件：formats/subscriptions/channels/params/defaultParams/facts/events/dropouts
// - 解析时派生：changedParams[i].first 用最近一条带时间戳消息补（P 段本身没有时间戳）
struct UlogLog
{
    uint64_t startTimestampUs = 0;
    uint8_t fileVersion = 0;
    uint8_t compatFlags[8] = {};
    uint8_t incompatFlags[8] = {};
    std::vector<uint64_t> appendedOffsets;
    bool hasSync = false;
    uint32_t syncCount = 0;
    std::vector<PsChannelData> channels;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<std::pair<std::string, std::string>> defaultParams;
    // I/M 段：文件级键值事实，不是运行日志；M 段同 key 可多条。
    std::vector<std::pair<std::string, std::string>> facts;
    // P 段的参数变更；P 不带时间，时间由解析器按最近消息时间补，属衍生字段。
    std::vector<std::pair<double, std::pair<std::string, std::string>>> changedParams;
    std::vector<UlogEvent> events;
    std::vector<std::pair<double, uint16_t>> dropouts;
    std::vector<std::string> formats;
    struct SubscriptionInfo { uint16_t msgId; std::string topic; uint8_t multiId; };
    std::vector<SubscriptionInfo> subscriptions;
    std::vector<std::string> missingFormats;
};

/* PX4/ArduPilot ULog v1：格式表 + 订阅 + 数据消息，展平规则对齐 PJ。 */
bool psParseUlog(const char *path, UlogLog &log, std::string &err);

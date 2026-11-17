#pragma once

#include <string>
#include <vector>

struct CsvColumn
{
    std::string name;
    std::string kind; // numeric / date_only / time_only / text
};

struct CsvProbe
{
    char delimiter = ',';
    bool ambiguous = false;
    bool hasHeader = true;
    std::vector<CsvColumn> columns;
    std::vector<std::vector<std::string>> preview;
    int suggestedTime = -1;
    std::vector<std::string> warnings;
};

struct CsvOptions
{
    char delimiter = ',';
    bool hasHeader = true;
    int skipRows = 0;
    int timeMode = 0; // 0 row, 1 column, 2 combined
    int timeIndex = 0;
    int dateIndex = 0;
    int clockIndex = 1;
};

struct CsvTable
{
    std::vector<std::string> headers;         // [0] 是时间轴
    std::vector<std::vector<double>> columns; // [0] 是秒
    std::vector<uint32_t> value_types;        // 与 columns 对齐，PsValueType
};

/* CSV 扩展的探测与解析都在扩展内完成。 */
bool psCsvProbeFile(const char *path, char forcedDelimiter, CsvProbe &out, std::string &err);
bool psCsvParseFile(const char *path, const CsvOptions &opt, CsvTable &out, std::string &err);

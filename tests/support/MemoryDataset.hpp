#pragma once

// 测试用 C++ typed 数据集：只实现一致性用例需要的最小模型。
// 生产仍以 Rust ps-ndf 为唯一权威；这里用于和 C ABI 行为对拍。
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace ps::test {

struct MemoryColumn
{
    std::string name;
    std::string unit;
    std::vector<double> values;
    std::vector<bool> nulls;
};

struct MemoryTable
{
    std::string name;
    std::vector<int64_t> time_us;
    std::vector<uint64_t> seq;
    std::map<std::string, MemoryColumn> columns;
    uint64_t next_seq = 0;
};

class MemoryDataset
{
public:
    MemoryTable &table(const std::string &name)
    {
        auto it = tables_.find(name);
        if (it == tables_.end())
            throw std::runtime_error("table not found");
        return it->second;
    }

    const MemoryTable &table(const std::string &name) const
    {
        auto it = tables_.find(name);
        if (it == tables_.end())
            throw std::runtime_error("table not found");
        return it->second;
    }

    MemoryTable &add_table(const std::string &name)
    {
        if (tables_.count(name))
            throw std::runtime_error("duplicate table");
        MemoryTable t;
        t.name = name;
        return tables_.emplace(name, std::move(t)).first->second;
    }

    void add_column(const std::string &table_name, const MemoryColumn &column)
    {
        MemoryTable &t = table(table_name);
        if (!t.time_us.empty() && column.values.size() != t.time_us.size())
            throw std::runtime_error("column length mismatch");
        if (column.nulls.size() != column.values.size())
            throw std::runtime_error("null length mismatch");
        if (t.columns.count(column.name))
            throw std::runtime_error("duplicate column");
        t.columns.emplace(column.name, column);
    }

    void append_frame(const std::string &table_name, const std::vector<int64_t> &time_us,
                      const std::vector<std::pair<std::string, std::vector<double>>> &values,
                      const std::vector<std::vector<bool>> &nulls = {})
    {
        MemoryTable &t = table(table_name);
        if (time_us.empty())
            throw std::runtime_error("empty frame");
        if (!t.time_us.empty() && time_us.front() < t.time_us.back())
            throw std::runtime_error("time is not monotonic");
        t.time_us.insert(t.time_us.end(), time_us.begin(), time_us.end());
        for (size_t i = 0; i < time_us.size(); ++i)
            t.seq.push_back(t.next_seq++);
        for (size_t c = 0; c < values.size(); ++c)
        {
            auto it = t.columns.find(values[c].first);
            if (it == t.columns.end())
                throw std::runtime_error("column not found");
            if (values[c].second.size() != time_us.size())
                throw std::runtime_error("frame column length mismatch");
            it->second.values.insert(it->second.values.end(), values[c].second.begin(),
                                    values[c].second.end());
            std::vector<bool> n(time_us.size(), false);
            if (c < nulls.size())
                n = nulls[c];
            it->second.nulls.insert(it->second.nulls.end(), n.begin(), n.end());
        }
    }

    double value_at(const std::string &table_name, const std::string &column, size_t row) const
    {
        const MemoryTable &t = table(table_name);
        auto it = t.columns.find(column);
        if (it == t.columns.end() || row >= it->second.values.size())
            return std::numeric_limits<double>::quiet_NaN();
        if (it->second.nulls[row])
            return std::numeric_limits<double>::quiet_NaN();
        return it->second.values[row];
    }

    bool is_null(const std::string &table_name, const std::string &column, size_t row) const
    {
        const MemoryTable &t = table(table_name);
        auto it = t.columns.find(column);
        return it == t.columns.end() || row >= it->second.nulls.size() || it->second.nulls[row];
    }

private:
    std::map<std::string, MemoryTable> tables_;
};

} // namespace ps::test

#pragma once

// Arrow 导入结果 -> Dataset::appendFrame 的薄桥接。
// 只做字段同构映射：Arrow 列值/时间(null 已归一到 ns)直接指向 Batch 缓冲，
// appendFrame 由 kernel 立即复制，故 Batch 只需在调用期间存活。
#include "ArrowChannel.h"
#include "Dataset.h"

#include <QVector>
#include <cstdint>
#include <cstring>
#include <string>

namespace dataset = nextpilot::plotscope::dataset;

namespace ps::arrow {

inline dataset::ValueType toValueType(Type t) {
    switch (t) {
        case Type::F64: return dataset::ValueType::F64;
        case Type::F32: return dataset::ValueType::F32;
        case Type::I64: case Type::Timestamp: return dataset::ValueType::I64;
        case Type::U64: return dataset::ValueType::U64;
        case Type::I32: return dataset::ValueType::I32;
        case Type::U32: return dataset::ValueType::U32;
        case Type::Bool: return dataset::ValueType::Bool;
    }
    return dataset::ValueType::F64;
}

// 生成 appendFrame 所需的时间轴：用时间列值（已归一 ns），或行索引 0..rows-1。
inline QVector<qint64> timeAxisFrom(const Batch &batch, int timestamp_column) {
    QVector<qint64> out;
    out.resize(static_cast<qsizetype>(batch.rows));
    if (timestamp_column < 0) {
        for (qint64 r = 0; r < batch.rows; ++r) out[static_cast<qsizetype>(r)] = r;
        return out;
    }
    const auto &col = batch.columns[static_cast<size_t>(timestamp_column)];
    for (qint64 r = 0; r < batch.rows; ++r) {
        int64_t ns = 0;
        std::memcpy(&ns, col.values.data() + static_cast<size_t>(r) * 8, 8);
        out[static_cast<qsizetype>(r)] = ns;
    }
    return out;
}

// 把 Batch 的列映射为 FrameColumn（不含时间列，避免重复灌入）。
// 返回的 FrameColumn 指向 batch 内缓冲，batch 必须在 appendFrame 调用期间存活。
inline QVector<dataset::FrameColumn> toFrameColumns(const Batch &batch, int timestamp_column) {
    QVector<dataset::FrameColumn> out;
    out.reserve(static_cast<qsizetype>(batch.columns.size()));
    for (size_t c = 0; c < batch.columns.size(); ++c) {
        if (static_cast<int>(c) == timestamp_column) continue;
        const auto &col = batch.columns[c];
        dataset::FrameColumn f;
        f.name = QString::fromUtf8(col.name.c_str());
        f.unit = QStringLiteral("");
        f.type = toValueType(col.type);
        f.values = col.values.data();
        f.nulls = col.validity.empty() ? nullptr
                                       : reinterpret_cast<const quint8 *>(col.validity.data());
        out.push_back(f);
    }
    return out;
}

}  // namespace ps::arrow

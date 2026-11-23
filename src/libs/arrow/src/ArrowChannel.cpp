#include "ArrowChannel.h"

#include <cstring>
#include <limits>
#include <vector>

#include "nanoarrow/nanoarrow.h"
#include "nanoarrow/nanoarrow_ipc.h"

namespace ps::arrow {
namespace {

constexpr int64_t kNsPerSec = 1'000'000'000LL;

int typeBytes(Type t) {
    switch (t) {
        case Type::F64: case Type::I64: case Type::U64: case Type::Timestamp: return 8;
        case Type::F32: case Type::I32: case Type::U32: return 4;
        case Type::Bool: return 1;
    }
    return 0;
}

bool mapArrowType(const ArrowSchemaView &view, Type &type, int &unit) {
    switch (view.type) {
        case NANOARROW_TYPE_DOUBLE:  type = Type::F64; return true;
        case NANOARROW_TYPE_FLOAT:   type = Type::F32; return true;
        case NANOARROW_TYPE_INT64:   type = Type::I64; return true;
        case NANOARROW_TYPE_INT32:   type = Type::I32; return true;
        case NANOARROW_TYPE_UINT64:  type = Type::U64; return true;
        case NANOARROW_TYPE_UINT32:  type = Type::U32; return true;
        case NANOARROW_TYPE_BOOL:    type = Type::Bool; return true;
        case NANOARROW_TYPE_TIMESTAMP:
            type = Type::Timestamp;
            switch (view.time_unit) {
                case NANOARROW_TIME_UNIT_SECOND: unit = 0; return true;
                case NANOARROW_TIME_UNIT_MILLI:  unit = 1; return true;
                case NANOARROW_TIME_UNIT_MICRO:  unit = 2; return true;
                case NANOARROW_TIME_UNIT_NANO:   unit = 3; return true;
                default: return false;
            }
        default: return false;
    }
}

int readColumn(const ArrowArrayView &view, Type type, int unit, bool to_ns,
               std::vector<std::byte> &values, std::vector<uint8_t> &validity,
               std::string &err) {
    const int64_t n = view.length;
    const int w = typeBytes(type);
    values.assign(static_cast<size_t>(n) * static_cast<size_t>(w), std::byte{0});
    validity.assign(static_cast<size_t>((n + 7) / 8), uint8_t{0});
    bool any_null = false;

    for (int64_t i = 0; i < n; ++i) {
        if (ArrowArrayViewIsNull(&view, i)) {
            validity[static_cast<size_t>(i / 8)] |= uint8_t{1} << (i % 8);
            any_null = true;
            continue;
        }
        std::byte *dst = values.data() + static_cast<size_t>(i) * static_cast<size_t>(w);
        switch (type) {
            case Type::F64: {
                double v = ArrowArrayViewGetDoubleUnsafe(&view, i);
                std::memcpy(dst, &v, 8);
                break;
            }
            case Type::F32: {
                float v = static_cast<float>(ArrowArrayViewGetDoubleUnsafe(&view, i));
                std::memcpy(dst, &v, 4);
                break;
            }
            case Type::I64: {
                int64_t v = ArrowArrayViewGetIntUnsafe(&view, i);
                std::memcpy(dst, &v, 8);
                break;
            }
            case Type::Timestamp: {
                int64_t v = ArrowArrayViewGetIntUnsafe(&view, i);
                if (to_ns && normalizeToNs(v, unit, v) != 0) {
                    err = "时间戳归一到纳秒时溢出或单位非法";
                    return -1;
                }
                std::memcpy(dst, &v, 8);
                break;
            }
            case Type::I32: {
                int32_t v = static_cast<int32_t>(ArrowArrayViewGetIntUnsafe(&view, i));
                std::memcpy(dst, &v, 4);
                break;
            }
            case Type::U64: {
                uint64_t v = ArrowArrayViewGetUIntUnsafe(&view, i);
                std::memcpy(dst, &v, 8);
                break;
            }
            case Type::U32: {
                uint32_t v = static_cast<uint32_t>(ArrowArrayViewGetUIntUnsafe(&view, i));
                std::memcpy(dst, &v, 4);
                break;
            }
            case Type::Bool: {
                const uint8_t bit = ArrowArrayViewGetIntUnsafe(&view, i) != 0 ? 1 : 0;
                std::memcpy(dst, &bit, 1);
                break;
            }
        }
    }
    if (!any_null) validity.clear();
    return 0;
}

struct SpanInputData {
    const uint8_t *data;
    int64_t size;
    int64_t offset;
};

ArrowErrorCode spanRead(ArrowIpcInputStream *stream, uint8_t *buf, int64_t cap,
                        int64_t *read_out, ArrowError *) {
    auto *s = static_cast<SpanInputData *>(stream->private_data);
    const int64_t avail = s->size - s->offset;
    const int64_t n = cap < avail ? cap : avail;
    if (n > 0) {
        std::memcpy(buf, s->data + s->offset, static_cast<size_t>(n));
        s->offset += n;
    }
    *read_out = n;
    return NANOARROW_OK;
}

void spanRelease(ArrowIpcInputStream *stream) {
    delete static_cast<SpanInputData *>(stream->private_data);
    stream->private_data = nullptr;
    stream->release = nullptr;
}

ArrowErrorCode initColumnSchema(ArrowSchema *schema, const Column &col) {
    if (col.type == Type::Timestamp) {
        ArrowSchemaInit(schema);
        return ArrowSchemaSetTypeDateTime(schema, NANOARROW_TYPE_TIMESTAMP,
                                          NANOARROW_TIME_UNIT_NANO, nullptr);
    }
    ArrowType arrow_type = NANOARROW_TYPE_DOUBLE;
    switch (col.type) {
        case Type::F64: arrow_type = NANOARROW_TYPE_DOUBLE; break;
        case Type::F32: arrow_type = NANOARROW_TYPE_FLOAT; break;
        case Type::I64: arrow_type = NANOARROW_TYPE_INT64; break;
        case Type::I32: arrow_type = NANOARROW_TYPE_INT32; break;
        case Type::U64: arrow_type = NANOARROW_TYPE_UINT64; break;
        case Type::U32: arrow_type = NANOARROW_TYPE_UINT32; break;
        case Type::Bool: arrow_type = NANOARROW_TYPE_BOOL; break;
        case Type::Timestamp: break;
    }
    return ArrowSchemaInitFromType(schema, arrow_type);
}

ArrowErrorCode appendValue(ArrowArray *array, const Column &col, int64_t row, bool is_null) {
    if (is_null) return ArrowArrayAppendNull(array, 1);
    const int w = typeBytes(col.type);
    const auto *src = reinterpret_cast<const unsigned char *>(col.values.data()) + row * w;
    switch (col.type) {
        case Type::F64: {
            double v = 0;
            std::memcpy(&v, src, 8);
            return ArrowArrayAppendDouble(array, v);
        }
        case Type::Timestamp: case Type::I64: {
            int64_t v = 0;
            std::memcpy(&v, src, 8);
            return ArrowArrayAppendInt(array, v);
        }
        case Type::I32: {
            int32_t v = 0;
            std::memcpy(&v, src, 4);
            return ArrowArrayAppendInt(array, v);
        }
        case Type::U64: {
            uint64_t v = 0;
            std::memcpy(&v, src, 8);
            return ArrowArrayAppendUInt(array, v);
        }
        case Type::U32: {
            uint32_t v = 0;
            std::memcpy(&v, src, 4);
            return ArrowArrayAppendUInt(array, v);
        }
        case Type::F32: {
            float f = 0;
            std::memcpy(&f, src, 4);
            return ArrowArrayAppendDouble(array, static_cast<double>(f));
        }
        case Type::Bool: {
            return ArrowArrayAppendInt(array, *src != 0 ? 1 : 0);
        }
    }
    return NANOARROW_OK;
}

}  // namespace

int normalizeToNs(int64_t value, int unit, int64_t &out) {
    int64_t factor = 0;
    switch (unit) {
        case 0: factor = kNsPerSec; break;
        case 1: factor = 1'000'000LL; break;
        case 2: factor = 1'000LL; break;
        case 3: out = value; return 0;
        default: return -1;
    }
    if (value > std::numeric_limits<int64_t>::max() / factor ||
        value < std::numeric_limits<int64_t>::min() / factor) {
        return -1;
    }
    out = value * factor;
    return 0;
}

int parseTimeUnit(const std::string &name) {
    if (name == "s" || name == "sec" || name == "second") return 0;
    if (name == "ms" || name == "milli" || name == "millisecond") return 1;
    if (name == "us" || name == "usec" || name == "micro" || name == "microsecond") return 2;
    if (name == "ns" || name == "nsec" || name == "nano" || name == "nanosecond") return 3;
    return -1;
}

int importIpc(const uint8_t *data, size_t size, const ImportOptions &options,
              Batch &out, std::string &err) {
    if (data == nullptr || size == 0) {
        err = "Arrow IPC 数据为空";
        return -1;
    }
    ArrowIpcInputStream input;
    input.read = spanRead;
    input.release = spanRelease;
    input.private_data = new SpanInputData{data, static_cast<int64_t>(size), 0};

    ArrowArrayStream stream;
    ArrowIpcArrayStreamReaderOptions ropts{};
    ropts.field_index = -1;
    ropts.use_shared_buffers = 0;
    if (ArrowIpcArrayStreamReaderInit(&stream, &input, &ropts) != NANOARROW_OK) {
        err = "初始化 Arrow IPC reader 失败";
        return -1;
    }

    ArrowSchema schema;
    if (stream.get_schema(&stream, &schema) != NANOARROW_OK) {
        err = "读取 Arrow schema 失败";
        stream.release(&stream);
        return -1;
    }
    const int64_t ncol = schema.n_children;
    if (ncol <= 0) {
        err = "Arrow schema 无列";
        schema.release(&schema);
        stream.release(&stream);
        return -1;
    }
    if (options.timestamp_column >= ncol) {
        err = "时间列索引越界";
        schema.release(&schema);
        stream.release(&stream);
        return -1;
    }

    std::vector<Type> types(static_cast<size_t>(ncol));
    std::vector<int> units(static_cast<size_t>(ncol), 3);
    std::vector<bool> is_time(static_cast<size_t>(ncol), false);
    out.columns.clear();
    out.rows = 0;
    for (int64_t c = 0; c < ncol; ++c) {
        ArrowSchemaView sview;
        if (ArrowSchemaViewInit(&sview, schema.children[c], nullptr) != NANOARROW_OK) {
            err = "解析 Arrow 列 schema 失败";
            schema.release(&schema);
            stream.release(&stream);
            return -1;
        }
        Type t = Type::F64;
        if (!mapArrowType(sview, t, units[static_cast<size_t>(c)])) {
            err = "不支持的 Arrow 列类型";
            schema.release(&schema);
            stream.release(&stream);
            return -1;
        }
        types[static_cast<size_t>(c)] = t;
        Column col;
        col.name = schema.children[c]->name ? schema.children[c]->name : "";
        col.type = (t == Type::Timestamp) ? Type::I64 : t;
        col.time_unit = units[static_cast<size_t>(c)];
        out.columns.push_back(std::move(col));
    }
    if (options.timestamp_column >= 0) {
        if (types[static_cast<size_t>(options.timestamp_column)] != Type::Timestamp) {
            err = "指定的时间列不是 TIMESTAMP 类型";
            schema.release(&schema);
            stream.release(&stream);
            return -1;
        }
        is_time[static_cast<size_t>(options.timestamp_column)] = true;
    }

    ArrowArrayView aview;
    if (ArrowArrayViewInitFromSchema(&aview, &schema, nullptr) != NANOARROW_OK) {
        err = "初始化 Arrow 数组视图失败";
        schema.release(&schema);
        stream.release(&stream);
        return -1;
    }

    while (true) {
        ArrowArray array;
        if (stream.get_next(&stream, &array) != NANOARROW_OK) {
            err = "读取 Arrow record batch 失败";
            ArrowArrayViewReset(&aview);
            schema.release(&schema);
            stream.release(&stream);
            return -1;
        }
        if (array.release == nullptr) break;  // EOS
        if (ArrowArrayViewSetArray(&aview, &array, nullptr) != NANOARROW_OK) {
            err = "解析 Arrow record batch 失败";
            array.release(&array);
            ArrowArrayViewReset(&aview);
            schema.release(&schema);
            stream.release(&stream);
            return -1;
        }
        const int64_t rows = aview.length;
        for (int64_t c = 0; c < ncol; ++c) {
            const bool to_ns = is_time[static_cast<size_t>(c)];
            std::vector<std::byte> vals;
            std::vector<uint8_t> val;
            if (readColumn(*aview.children[c], types[static_cast<size_t>(c)],
                           units[static_cast<size_t>(c)], to_ns, vals, val, err) != 0) {
                array.release(&array);
                ArrowArrayViewReset(&aview);
                schema.release(&schema);
                stream.release(&stream);
                return -1;
            }
            auto &dst = out.columns[static_cast<size_t>(c)].values;
            dst.insert(dst.end(), vals.begin(), vals.end());
            auto &dv = out.columns[static_cast<size_t>(c)].validity;
            if (!val.empty()) {
                if (dv.empty()) dv.assign(static_cast<size_t>((out.rows + 7) / 8), uint8_t{0});
                // 按位追加本批 validity 到全局位图。
                std::vector<uint8_t> merged(static_cast<size_t>((out.rows + rows + 7) / 8), uint8_t{0});
                for (size_t b = 0; b < merged.size() * 8; ++b) {
                    bool null = false;
                    if (b < static_cast<size_t>(out.rows)) {
                        null = (dv[b / 8] >> (b % 8)) & 1;
                    } else {
                        const size_t j = b - static_cast<size_t>(out.rows);
                        null = !val.empty() && ((val[j / 8] >> (j % 8)) & 1);
                    }
                    if (null) merged[b / 8] |= uint8_t{1} << (b % 8);
                }
                dv = std::move(merged);
            }
        }
        out.rows += rows;
        array.release(&array);
    }

    ArrowArrayViewReset(&aview);
    schema.release(&schema);
    stream.release(&stream);
    return 0;
}

int exportIpc(const Batch &batch, std::vector<uint8_t> &out, std::string &err) {
    if (batch.columns.empty()) {
        err = "导出的列批为空";
        return -1;
    }
    const int64_t ncol = static_cast<int64_t>(batch.columns.size());

    ArrowSchema schema;
    ArrowSchemaInit(&schema);
    if (ArrowSchemaSetTypeStruct(&schema, ncol) != NANOARROW_OK) {
        err = "构造 Arrow schema 失败";
        schema.release(&schema);
        return -1;
    }
    for (int64_t c = 0; c < ncol; ++c) {
        if (initColumnSchema(schema.children[c], batch.columns[static_cast<size_t>(c)]) != NANOARROW_OK ||
            ArrowSchemaSetName(schema.children[c], batch.columns[static_cast<size_t>(c)].name.c_str()) != NANOARROW_OK) {
            err = "构造 Arrow 列 schema 失败";
            schema.release(&schema);
            return -1;
        }
    }

    ArrowArray array;
    if (ArrowArrayInitFromSchema(&array, &schema, nullptr) != NANOARROW_OK) {
        err = "构造 Arrow 数组失败";
        schema.release(&schema);
        return -1;
    }
    for (int64_t c = 0; c < ncol; ++c) {
        auto *child = array.children[c];
        if (ArrowArrayStartAppending(child) != NANOARROW_OK) {
            err = "初始化 Arrow 列追加失败";
            array.release(&array);
            schema.release(&schema);
            return -1;
        }
        const Column &col = batch.columns[static_cast<size_t>(c)];
        for (int64_t r = 0; r < batch.rows; ++r) {
            const bool is_null = !col.validity.empty() && ((col.validity[static_cast<size_t>(r / 8)] >> (r % 8)) & 1);
            if (appendValue(child, col, r, is_null) != NANOARROW_OK) {
                err = "追加 Arrow 值失败";
                array.release(&array);
                schema.release(&schema);
                return -1;
            }
        }
    }
    // struct 数组顶层 length 不会由 nanoarrow 从 children 推断，需显式设置。
    array.length = batch.rows;
    if (ArrowArrayFinishBuildingDefault(&array, nullptr) != NANOARROW_OK) {
        err = "完成 Arrow 数组构建失败";
        array.release(&array);
        schema.release(&schema);
        return -1;
    }

    // 由构建好的 schema + array 生成 array view，直接写 schema 与一个 batch。
    ArrowArrayView aview;
    ArrowArrayViewInitFromSchema(&aview, &schema, nullptr);
    if (ArrowArrayViewSetArray(&aview, &array, nullptr) != NANOARROW_OK) {
        err = "构造 Arrow 数组视图失败";
        ArrowArrayViewReset(&aview);
        array.release(&array);
        schema.release(&schema);
        return -1;
    }

    ArrowBuffer buf;
    ArrowBufferInit(&buf);
    ArrowIpcOutputStream ostream;
    if (ArrowIpcOutputStreamInitBuffer(&ostream, &buf) != NANOARROW_OK) {
        err = "初始化 Arrow IPC 输出流失败";
        ArrowArrayViewReset(&aview);
        array.release(&array);
        schema.release(&schema);
        ArrowBufferReset(&buf);
        return -1;
    }
    ArrowIpcWriter writer;
    if (ArrowIpcWriterInit(&writer, &ostream) != NANOARROW_OK) {
        err = "初始化 Arrow IPC writer 失败";
        ArrowArrayViewReset(&aview);
        array.release(&array);
        schema.release(&schema);
        ArrowBufferReset(&buf);
        return -1;
    }
    ArrowError error;
    ArrowErrorInit(&error);
    int rc = ArrowIpcWriterWriteSchema(&writer, &schema, &error);
    if (rc == NANOARROW_OK) rc = ArrowIpcWriterWriteArrayView(&writer, &aview, &error);
    if (rc == NANOARROW_OK) rc = ArrowIpcWriterWriteArrayView(&writer, nullptr, &error);  // EOS
    ArrowIpcWriterReset(&writer);
    ArrowArrayViewReset(&aview);
    array.release(&array);
    schema.release(&schema);
    if (rc != NANOARROW_OK) {
        err = std::string("写 Arrow IPC 流失败：") + error.message;
        ArrowBufferReset(&buf);
        return -1;
    }
    out.assign(buf.data, buf.data + buf.size_bytes);
    ArrowBufferReset(&buf);
    return 0;
}

}  // namespace ps::arrow

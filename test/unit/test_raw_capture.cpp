#include "PsCaptureFormat.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "ps_raw_capture_test.raw";
    std::filesystem::remove(path);

    ps::capture::Header header;
    header.source_id = "test_source";
    header.plugin_id = "test_plugin";
    header.config_json = R"({"port":14550})";
    header.schema_blob = "schema-bytes";
    header.created_at_ns = 123456789;

    ps::capture::RawWriter writer;
    std::string err;
    assert(writer.open(path.string(), header, err));
    assert(err.empty());

    ps::capture::Record first;
    first.timestamp_ns = 100;
    first.topic = "mavlink";
    first.encoding = "mavlink";
    first.part_count = 1;
    first.part_lengths = {3};
    first.payload = {1, 2, 3};
    assert(writer.write(first, err));

    ps::capture::Record second;
    second.timestamp_ns = 200;
    second.topic = "oem7";
    second.encoding = "oem7";
    second.part_count = 2;
    second.part_lengths = {1, 2};
    second.payload = {4, 5, 6};
    assert(writer.write(second, err));
    assert(writer.close(err));

    assert(ps::capture::isRawFile(path.string()));

    {
    ps::capture::RawReader reader;
    assert(reader.open(path.string(), err));
    assert(reader.header().source_id == "test_source");
    assert(reader.header().schema_blob == "schema-bytes");
    assert(reader.recordCount() == 2);

    ps::capture::Record got;
    assert(reader.next(got, err));
    assert(got.timestamp_ns == 100);
    assert(got.payload.size() == 3 && got.payload[0] == 1 && got.payload[2] == 3);

    assert(reader.next(got, err));
    assert(got.timestamp_ns == 200);
    assert(got.part_count == 2);
    assert(got.part_lengths[0] == 1 && got.part_lengths[1] == 2);
    assert(got.payload.size() == 3 && got.payload[1] == 5);

    assert(!reader.next(got, err));
    assert(err.empty());
    assert(reader.atEnd());

    assert(reader.seek(200, err));
    assert(reader.next(got, err));
    assert(got.timestamp_ns == 200);
    assert(reader.seek(100, err));
    assert(reader.next(got, err));
    assert(got.timestamp_ns == 100);

    }

    std::filesystem::remove(path);

    const auto legacy = std::filesystem::temp_directory_path() / "ps_raw_legacy_test.bin";
    {
        std::ofstream f(legacy, std::ios::binary);
        f.write("legacy", 6);
    }
    assert(!ps::capture::isRawFile(legacy.string()));
    std::filesystem::remove(legacy);

    std::puts("raw capture ok");
    return 0;
}

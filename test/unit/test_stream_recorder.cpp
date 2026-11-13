#include "live/record/StreamRecorder.h"

#include "McapReader.h"
#include "PsCaptureFormat.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // CSV：按点写，表头保持旧格式。
    {
        StreamRecorder recorder;
        QString path;
        QString err;
        if (!recorder.start(StreamRecorder::Format::Csv, "sim", "sim", "{}", &path, &err)) { fprintf(stderr, "csv start: %s\n", err.toUtf8().constData()); return 1; }
        const double t[3] = {0.0, 0.1, 0.2};
        const double v[3] = {1.0, 2.0, 3.0};
        recorder.writeChannelBatch("sim.roll", "deg", t, v, 3);
        recorder.stop();
        assert(QFile::exists(path));

        std::ifstream in(path.toStdString());
        std::string line;
        bool sawHeader = false;
        int rows = 0;
        while (std::getline(in, line))
        {
            if (line == "time,channel,value")
                sawHeader = true;
            else if (!line.empty())
                ++rows;
        }
        assert(sawHeader);
        assert(rows == 3);
        std::filesystem::remove(path.toStdString());
    }

    // MCAP：每个点一条 json message，topic 就是通道名。
    {
        StreamRecorder recorder;
        QString path;
        QString err;
        if (!recorder.start(StreamRecorder::Format::Mcap, "sim", "sim", "{}", &path, &err)) { fprintf(stderr, "mcap start: %s\n", err.toUtf8().constData()); return 1; }
        const double t[2] = {0.0, 0.5};
        const double v[2] = {4.5, 9.0};
        recorder.writeChannelBatch("sim.roll", "deg", t, v, 2);
        recorder.stop();
        assert(QFile::exists(path));

        ps::mcap::Reader reader;
        std::string re;
        assert(reader.open(path.toStdString(), re));

        ps::mcap::Message msg;
        int count = 0;
        while (reader.next(msg, re))
        {
            const std::string payload(msg.data.begin(), msg.data.end());
            assert(payload.find("\"value\"") != std::string::npos);
            ++count;
        }
        assert(count == 2);
        assert(reader.channels().size() == 1);
        assert(reader.channels().begin()->second.topic == "sim.roll");
        assert(reader.channels().begin()->second.message_encoding == "json");
        std::filesystem::remove(path.toStdString());
    }

    // RAW：PSRAW1 记录原始字节，encoding 供回放路由。
    {
        StreamRecorder recorder;
        QString path;
        QString recErr;
        if (!recorder.start(StreamRecorder::Format::Raw, "mavlink", "mavlink", "{}", &path, &recErr)) { fprintf(stderr, "raw start: %s\n", recErr.toUtf8().constData()); return 1; }
        const uint8_t first[3] = {1, 2, 3};
        const uint8_t second[2] = {4, 5};
        recorder.writeRaw("mavlink", "mavlink", 100, first, 3);
        recorder.writeRaw("mavlink", "mavlink", 200, second, 2);
        recorder.stop();
        assert(QFile::exists(path));

        std::string err;
        ps::capture::RawReader reader;
        assert(reader.open(path.toStdString(), err));
        assert(reader.recordCount() == 2);
        ps::capture::Record record;
        assert(reader.next(record, err));
        assert(record.encoding == "mavlink");
        assert(record.payload.size() == 3 && record.payload[2] == 3);
        assert(reader.next(record, err));
        assert(record.timestamp_ns == 200);
        assert(record.payload.size() == 2 && record.payload[1] == 5);
        std::filesystem::remove(path.toStdString());
    }

    return 0;
}

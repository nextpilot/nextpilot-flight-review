#include "record/StreamRecorder.h"

#include "McapReader.h"
#include "PsCaptureFormat.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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

        {
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
        }
        std::filesystem::remove(path.toStdString());
    }

    // RAW：PSRAW1 记录原始字节，encoding 供回放路由；database 随头存 schema_blob。
    {
        const QString dbPath = QDir(QDir::tempPath()).filePath(QStringLiteral("ps_stream_db.json"));
        QFile db(dbPath);
        if (!db.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 1;
        db.write("{\"schema\":\"oem7\"}");
        db.close();
        QJsonObject cfg;
        cfg.insert(QStringLiteral("database"), dbPath);
        const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

        StreamRecorder recorder;
        QString path;
        QString recErr;
        if (!recorder.start(StreamRecorder::Format::Raw, "mavlink", "mavlink", cfgJson, &path, &recErr)) { fprintf(stderr, "raw start: %s\n", recErr.toUtf8().constData()); return 1; }
        const uint8_t first[3] = {1, 2, 3};
        const uint8_t second[2] = {4, 5};
        recorder.writeRaw("mavlink", "mavlink", 100, first, 3);
        recorder.writeRaw("mavlink", "mavlink", 200, second, 2);
        recorder.stop();
        assert(QFile::exists(path));

        {
            std::string err;
            ps::capture::RawReader reader;
            assert(reader.open(path.toStdString(), err));
            assert(reader.recordCount() == 2);
            assert(reader.header().schema_blob == "{\"schema\":\"oem7\"}");
            ps::capture::Record record;
            assert(reader.next(record, err));
            assert(record.encoding == "mavlink");
            assert(record.payload.size() == 3 && record.payload[2] == 3);
            assert(reader.next(record, err));
            assert(record.timestamp_ns == 200);
            assert(record.payload.size() == 2 && record.payload[1] == 5);
        }
        std::filesystem::remove(path.toStdString());
        std::filesystem::remove(dbPath.toStdString());
    }

    // MCAP 显式 sequence：导出路径要求保留表内 seq。
    {
        const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("ps_mcap_seq.mcap"));
        McapWriter writer;
        std::string err;
        assert(writer.open(path.toStdString(), "plotscope", err));
        assert(writer.writeJson("imu", 1000, "{\"v\":1}", err, 7));
        assert(writer.close(err));
        ps::mcap::Reader reader;
        assert(reader.open(path.toStdString(), err));
        ps::mcap::Message msg;
        assert(reader.next(msg, err));
        assert(msg.sequence == 7);
        std::filesystem::remove(path.toStdString());
    }

    return 0;
}

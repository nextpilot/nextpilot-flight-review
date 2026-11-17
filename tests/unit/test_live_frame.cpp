#include "LiveFrameSink.h"

#include <QCoreApplication>
#include <QString>

#include <cassert>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    LiveFrameSink session(QStringLiteral("test"), QStringLiteral("测试"));

    const int64_t time_us[2] = {1700000000000000LL, 1700000000010000LL};
    const double roll[2] = {1.5, 2.5};
    const float pitch[2] = {3.0f, 4.0f};
    const uint8_t pitch_nulls[1] = {0b10}; // 第 2 行 null
    PsFrameColumn cols[2] = {
        { "roll", "deg", PS_VALUE_F64, roll, nullptr },
        { "pitch", "deg", PS_VALUE_F32, pitch, pitch_nulls },
    };
    PsFrameBatch batch{ "imu", time_us, nullptr, 2, 2, cols };
    assert(session.sink()->frame_batch(session.sink()->ctx, &batch) == 0);
    assert(ps_dataset_table_rows(session.dataset(), "imu") == 2);

    const QVector<const PsChannelInfo *> channels = session.channels();
    assert(channels.size() == 2);
    for (const PsChannelInfo *ch : channels)
    {
        assert(ch && ch->point_count == 2);
        assert(std::abs(ch->time[0] - 1700000000.0) < 1e-6);
        assert(std::abs(ch->time[1] - 1700000000.01) < 1e-6);
    }

    bool sawRoll = false, sawPitch = false;
    for (const PsChannelInfo *ch : channels)
    {
        const QString name = QString::fromUtf8(ch->name);
        if (name == QStringLiteral("imu.roll"))
        {
            sawRoll = true;
            assert(std::abs(ch->value[0] - 1.5) < 1e-9);
            assert(std::abs(ch->value[1] - 2.5) < 1e-9);
        }
        else if (name == QStringLiteral("imu.pitch"))
        {
            sawPitch = true;
            assert(std::abs(ch->value[0] - 3.0) < 1e-6);
            assert(std::isnan(ch->value[1]));
        }
    }
    assert(sawRoll && sawPitch);

    // 同一张表的所有列必须共用一份时间轴，不能把时间轴复制到每列。
    assert(channels[0]->time == channels[1]->time);

    // typed 视口 LOD 使用 UTC 微秒；2 像素窗口应能覆盖两行。
    double lod[6] = {0};
    const uint32_t lod_n = ps_dataset_lod_minmax(session.dataset(), "imu", "roll",
                                                 1700000000000000LL, 1700000000020000LL, 2,
                                                 lod, 2);
    assert(lod_n >= 1);
    assert(std::abs(lod[0] - 1700000000.0) < 1e-6);

    // 空 batch 必须被拒绝。
    PsFrameBatch empty{ "imu", nullptr, nullptr, 0, 2, cols };
    assert(session.sink()->frame_batch(session.sink()->ctx, &empty) != 0);

    // 小预算 ring：typed Dataset 只保留最后 5 行。
    assert(ps_dataset_set_ring_budget(session.dataset(), nullptr, 125, nullptr, 0) == 0);
    int64_t ring_t[10];
    double ring_v[10];
    for (int i = 0; i < 10; ++i)
    {
        ring_t[i] = 1700000000000000LL + i * 1000;
        ring_v[i] = i;
    }
    PsFrameColumn ring_col{ "value", "x", PS_VALUE_F64, ring_v, nullptr };
    PsFrameBatch ring_batch{ "ring", ring_t, nullptr, 10, 1, &ring_col };
    assert(session.sink()->frame_batch(session.sink()->ctx, &ring_batch) == 0);
    assert(ps_dataset_table_rows(session.dataset(), "ring") == 5);

    fprintf(stdout, "live frame_batch ok\n");
    return 0;
}
